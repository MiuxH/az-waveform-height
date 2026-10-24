/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after extras.c. All EP147 objects are accessed on the message thread
 * through existing destructor-tracked identities. Source geometry stays native;
 * the compositor renders native amplitude cells at its destination height. */
#include "../common/azdeckinfo.h"
#include "player_canvas.h"
#include <time.h>
static void mp_image_from_memory(uintptr_t *, const void *, int);
static uintptr_t g_pv_canvas;
static azstem_deck_snapshot_fn g_pv_snapshot;
static azstem_deck_beat_phase_fn g_pv_beat;
static int g_pv_saved_mode;
static _Atomic int g_pv_draw_width = 1280, g_pv_fx_rect[4];
static _Atomic int g_pv_header_dirty=1;
static _Atomic int g_pv_tile_rect[4][4];
static _Atomic long g_pv_tile_ms[4];
static void pv_prepare(void);
static _Atomic long g_pv_draw_ms;
static _Atomic long g_pv_info_until;
static _Atomic int g_pv_native_info;
static _Atomic int g_pv_info_floor;
static _Atomic unsigned g_pv_phase_mask, g_pv_elapsed_mask;
static _Atomic unsigned g_pv_panel_hidden;
static _Atomic int g_pv_sheet_deck=-1, g_pv_sheet_kind;
static _Atomic int g_pv_zoom[4], g_pv_center[4]={-1,-1,-1,-1};
static _Atomic int g_pv_loaded[4],g_pv_shared_modal;
static _Atomic int g_pv_preview[4]={-1,-1,-1,-1};
static _Atomic int g_pv_mark[4]={-1,-1,-1,-1};
static _Atomic int g_pv_mark_candidate[4]={-1,-1,-1,-1};
static _Atomic unsigned g_pv_seek_mask;
static _Atomic int g_pv_grid_request=-1, g_pv_enlarged=-1;
static _Atomic unsigned g_pv_enlarged_anchor;
static _Atomic unsigned g_pv_gesture_epoch;
static int g_pv_context_count=-1;
static unsigned g_pv_context_focus;
static unsigned g_pv_track_identity[4];
static int pv_active_count(void) { return __atomic_load_n(&g_pv_enlarged,__ATOMIC_ACQUIRE)>=0?1:pv_count(az_get(AZF_PLAYER_VIEW_COUNT)); }
static int pv_active_focus(void) {
    int d=__atomic_load_n(&g_pv_enlarged,__ATOMIC_ACQUIRE);
    return d>=0?d:__atomic_load_n(&g_pv_focus,__ATOMIC_ACQUIRE);
}
static int g_pv_touch_owned;
static unsigned g_pv_owned_contact_epoch;
static _Atomic long g_pv_frames, g_pv_missing;
/* Real work counters: separate compositor cost from the frame clock in captures. */
static _Atomic long g_pv_collect_us_total,g_pv_render_us_total,g_pv_paint_us_total;
static _Atomic long g_pv_tick_us_total,g_pv_tick_n,g_pv_footer_skipped;
static long pv_now_us(void) {
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1000000L+ts.tv_nsec/1000;
}
static _Atomic int g_pv_overview_sources[4]; /* 0 missing, 1 raw RGB, 2 RGB image, 3 blue image, 4 3-band image, 5 analysis */
static unsigned char g_pv_overview[4][OV_MAX * OV_STRIDE]; /* message thread only */
#ifndef PV_LIBRARY_STUB
#include "player_library.c"
#endif
static struct pv_analysis *g_pv_analysis_hold[4][3]; /* one frame borrow, message thread */
static struct pv_beats *g_pv_grid_hold[4];
static void pv_analysis_clear(void) {
    if (!on_msg_thread()) return;
    for (int d=0;d<4;d++) for (int k=0;k<3;k++) {
        pv_analysis_release(g_pv_analysis_hold[d][k]); g_pv_analysis_hold[d][k]=NULL;
    }
    for(int d=0;d<4;d++){pv_beats_release(g_pv_grid_hold[d]);g_pv_grid_hold[d]=NULL;}
}
static int pv_on(void) { return az_on(AZF_PLAYER_VIEW) && az_active(AZF_PLAYER_VIEW); }
static int pv_image(uintptr_t holder, struct pv_image *im, int max_w, int max_h) {
    uintptr_t p = 0, vt = 0;
    memset(im, 0, sizeof *im);
    if (!own_q(holder, &p) || !p || !own_q(p, &vt) || vt != DM_SPD_VTABLE) return 0;
    int format = *(int *) (p + DM_IPD_FORMAT), w = *(int *) (p + DM_IPD_W), h = *(int *) (p + DM_IPD_H);
    int ps = *(int *) (p + DM_SPD_PSTR), ls = *(int *) (p + DM_SPD_LSTR);
    const unsigned char *data = *(const unsigned char **) (p + DM_SPD_DATA);
    if ((format != 1 && format != 2) || ps != (format == 1 ? 3 : 4) || w < 1 || w > max_w || h < 1 || h > max_h ||
        ls < w * ps || ls > w * ps + 64 || !data) return 0;
    *im = (struct pv_image) {data, w, h, ls, ps};
    return 1;
}
/* All RTTI metadata comes from EP147's mapped image. The Component belongs
 * to a live, destructor-tracked overview family on the message thread. */
static int pv_class_is(uintptr_t obj, const char *want) {
    uintptr_t vt = 0, ti = 0, name = 0;
    if (!own_q(obj, &vt) || !img_q(vt - 8, &ti) || !img_q(ti + 8, &name)) return 0;
    size_t n = strlen(want) + 1;
    for (size_t off = 0; off < n; off += 8) {
        uintptr_t word = 0;
        size_t bytes = n - off < 8 ? n - off : 8;
        if (!img_q(name + off, &word) || memcmp(&word, want + off, bytes)) return 0;
    }
    return 1;
}
#include "player_status_native.c"
static int pv_children(uintptr_t comp, uintptr_t *kids) {
    if (!ht_ptr_ok(comp) || !own_q(comp + 0x40, kids)) return 0;
    int n = *(const int *) (comp + 0x50);
    return n > 0 && n <= 16 && ht_ptr_ok(*kids) ? n : 0;
}
/* Look up a real visible child rather than opening a hole for the sliding FX
 * container, which also covers the native waveform and its expand arrow. */
static uintptr_t pv_child_class(uintptr_t parent, const char *name, int depth) {
    uintptr_t kids = 0;
    if (!ht_ptr_ok(parent) || !own_q(parent + 0x40, &kids)) return 0;
    int n = *(const int *) (parent + 0x50);
    if (n < 1 || n > 64 || !ht_ptr_ok(kids)) return 0;
    for (int i = 0; i < n; i++) {
        uintptr_t child = 0;
        int rect[4];
        if (!own_q(kids + 8 * i, &child) || !ht_ptr_ok(child)) continue;
        /* Most children have the wrong class. Avoid walking their full parent
         * chains just to reject the RTTI afterwards. */
        if (pv_class_is(child, name) && ht_screen_rect(child, rect)) return child;
    }
    if (depth > 0) {
        for (int i = 0; i < n; i++) {
            uintptr_t child = 0;
            int rect[4];
            if (!own_q(kids + 8 * i, &child) || !ht_ptr_ok(child) || !ht_screen_rect(child, rect)) continue;
            uintptr_t found = pv_child_class(child, name, depth - 1);
            if (found) return found;
        }
    }
    return 0;
}
#include "player_controls_native.c"
#include "player_needle_native.c"
static int pv_shared_modal(uintptr_t view) {
    uintptr_t parent=view;
    int open=0;
    for(int level=0;parent && level<3 && !open;level++) {
        open=pv_child_class(parent,"N3gui8shortcut12ShortcutViewE",1)!=0 ||
             pv_child_class(parent,"N3gui7utility11UtilityViewE",1)!=0;
        uintptr_t next=0;
        if(!own_q(parent+0x18,&next) || !ht_ptr_ok(next) || next==parent)break;
        parent=next;
    }
    __atomic_store_n(&g_pv_shared_modal,open,__ATOMIC_RELEASE);return open;
}
static int g_pv_touch_deck = -1;
static void pv_cancel_inputs(void) {
    __atomic_store_n(&g_pv_header_dirty,1,__ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_controls_refresh,1,__ATOMIC_RELEASE);
    __atomic_fetch_add(&g_pv_gesture_epoch,1,__ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_sheet_deck,-1,__ATOMIC_RELEASE);
    pv_analysis_clear();
    __atomic_store_n(&g_pv_needle_cancelled,1,__ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_needle_down,0,__ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_needle_pending,0,__ATOMIC_RELEASE);
    pv_needle_tick(0);
    __atomic_store_n(&g_pv_input_active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_input_seen_ms, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_controls_req, 0, __ATOMIC_RELEASE);
    for (int d = 0; d < 4; d++) {
        __atomic_store_n(&g_pv_mark_candidate[d],-1,__ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_pad_req[d], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_pad_down[d], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_panel_req[d], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_mode_req[d], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_button_req[d],0,__ATOMIC_RELEASE);
    }
}
static int pv_info_open(uintptr_t view) {
    uintptr_t host = 0;
    int attached = own_q(view + 0x18, &host) && ht_ptr_ok(host);
    int open = attached && pm_trackinfo_open(view);
    uintptr_t parent = attached ? view : 0;
    /* INFO can be a WaveformView child or a sibling on the shared screen host.
     * Inspect only direct children of at most three live ancestors. */
    for (int level = 0; !open && parent && level < 3; level++) {
        open = pv_child_class(parent, "N3gui10InfoWidgetE", 0) != 0 ||
               pv_child_class(parent, "N3gui14LinkInfoWidgetE", 0) != 0;
        uintptr_t next = 0;
        if (!own_q(parent + 0x18, &next) || !ht_ptr_ok(next) || next == parent) break;
        parent = next;
    }
    __atomic_store_n(&g_pv_native_info, open, __ATOMIC_RELEASE);
    if (open) __atomic_store_n(&g_pv_info_until, 0, __ATOMIC_RELEASE);
    else __atomic_store_n(&g_pv_info_floor, 0, __ATOMIC_RELEASE);
    return open;
}
/* Only a measured InfoWidget gives us permission to replace the region below
 * it. LinkInfoWidget itself spans the screen and is not the metadata panel. */
static int pv_info_bottom(uintptr_t view) {
    uintptr_t parent = view;
    for (int level = 0; parent && level < 3; level++) {
        uintptr_t panel = pv_child_class(parent, "N3gui10InfoWidgetE", 1);
        int rect[4];
        if (panel && ht_screen_rect(panel, rect) && rect[0] >= 0 && rect[1] >= 70 &&
            rect[2] > 0 && rect[0] + rect[2] <= 1280 && rect[3] > 0 &&
            rect[1] + rect[3] >= 300 && rect[1] + rect[3] <= 650) return rect[1] + rect[3];
        uintptr_t next = 0;
        if (!own_q(parent + 0x18, &next) || !ht_ptr_ok(next) || next == parent) break;
        parent = next;
    }
    return 0;
}
static int pv_info_pending(void) {
    return now_ms() < __atomic_load_n(&g_pv_info_until, __ATOMIC_ACQUIRE);
}
/* 1.30/2.00 each embed Blue400, RGB1200 and 3Band widgets in the same
 * player_info_waveform::WaveformWidget. The RGB shared object is empty while
 * the 3-band sibling draws. Walk the live Component tree, choose the visible
 * sibling, and borrow its ImageComponent's completed raster instead.
 *
 * Verified in 2.00: tile ctor 0x1d82a20, ImageComponent ctor 0x25c5480,
 * paint 0x259fa00: juce::Image at ImageComponent+0xd8. 1.30's corresponding
 * ImageComponent ctor is 0x2457fe0, with the same image field. No new hook,
 * vtable patch, guessed cross-firmware address, or native image write. */
static int pv_cached_overview(uintptr_t rgb, struct pv_image *out) {
    uintptr_t delta = 0, parent = 0, siblings = 0;
    if (!on_msg_thread() || !img_q(OV_WIDGET_VT - 0x18, &delta) || delta != 0x170 ||
        !own_q(rgb + delta + 0x18, &parent) ||
        !pv_class_is(parent, "N3gui20player_info_waveform14WaveformWidgetE")) return 0;
    int n = pv_children(parent, &siblings);
    for (int i = 0; i < n; i++) {
        uintptr_t comp = 0, images = 0;
        int rect[4];
        if (!own_q(siblings + 8 * i, &comp) || !ht_ptr_ok(comp) || !ht_screen_rect(comp, rect)) continue;
        int kind = pv_class_is(comp, "N3gui31OverviewWaveform1200PointWidgetE") ? 2 : pv_class_is(comp, "N3gui30OverviewWaveform400PointWidgetE") ? 3
                                                                                  : pv_class_is(comp, "N3gui27OverviewWaveform3BandWidgetE")      ? 4
                                                                                                                                                  : 0;
        if (!kind) continue;
        int ni = pv_children(comp, &images);
        for (int j = 0; j < ni; j++) {
            uintptr_t image = 0;
            if (!own_q(images + 8 * j, &image) || !ht_ptr_ok(image) ||
                !pv_class_is(image, "N4juce14ImageComponentE") || !ht_screen_rect(image, rect)) continue;
            if (pv_image(image + 0xd8, out, 2048, 256)) return kind;
        }
    }
    return 0;
}
static void pv_tick(void) {
    if (!on_msg_thread()) return;
    long tick_at=pv_now_us();
    __atomic_store_n(&g_pv_shared_modal,0,__ATOMIC_RELEASE);
    /* Settings are edited on SOURCE. Do not relayout or repaint its underlying
     * WaveformView while the menu owns the screen: that native work can refresh
     * the waveform visibility signal and close the docked SOURCE menu. Keep the
     * tracked restore identity until the menu closes, including rapid toggles. */
    if (__atomic_load_n(&g_menu_open, __ATOMIC_ACQUIRE)) {
        pv_cancel_inputs();
        __atomic_store_n(&g_pv_draw_ms, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_native_info, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_info_floor, 0, __ATOMIC_RELEASE);
        return;
    }
    uintptr_t v = wref_get(&g_4wv_view);
    if (!v || !pl_view_ok(v)) {
        pv_cancel_inputs();
        wref_set(&g_pv_mode_view, 0);
        __atomic_store_n(&g_pv_draw_ms, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_native_info, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_info_floor, 0, __ATOMIC_RELEASE);
        return;
    }
    if (__atomic_load_n(&g_pv_enlarged,__ATOMIC_ACQUIRE)>=0 &&
        __atomic_load_n(&g_pv_enlarged_anchor,__ATOMIC_ACQUIRE)!=__atomic_load_n(&g_pv_focus_seq,__ATOMIC_ACQUIRE)) {
        pv_cancel_inputs();
        __atomic_store_n(&g_pv_enlarged,-1,__ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_sheet_deck,-1,__ATOMIC_RELEASE);
    }
    uintptr_t host = 0;
    if (!own_q(v + 0x18, &host) || !ht_ptr_ok(host)) {
        pv_cancel_inputs();
        /* SOURCE/BROWSE detach the retained WaveformView without necessarily
         * clearing its visible flag. Preserve only the pending mode restore. */
        __atomic_store_n(&g_pv_draw_ms, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_native_info, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_info_floor, 0, __ATOMIC_RELEASE);
        for (int d = 0; d < 4; d++) __atomic_store_n(&g_pv_tile_ms[d], 0, __ATOMIC_RELEASE);
        return;
    }
    /* An INFO request and the resulting native panel suspend forced repaint.
     * Repainting the hidden WaveformView can re-arm its visibility signal and
     * dismiss/cover a native popup, just as it did to SOURCE's Mod Menu. */
    int grid_request=__atomic_exchange_n(&g_pv_grid_request,-1,__ATOMIC_ACQ_REL);
    if (pv_on() && grid_request>=0 && grid_request==__atomic_load_n(&g_pv_focus,__ATOMIC_ACQUIRE) &&
        g_orig_setgridadj && g_ep147_build==EPB_V200) {
        pv_cancel_inputs();
        __atomic_store_n(&g_pv_sheet_deck,-1,__ATOMIC_RELEASE);
        setgridadj_hook((void *)v,1);
        __atomic_store_n(&g_pv_draw_ms,0,__ATOMIC_RELEASE);
        return;
    }
    if (pv_on() && (pv_info_open(v) || pv_info_pending() || pv_shared_modal(v) || __atomic_load_n(&g_grid_adjust,__ATOMIC_ACQUIRE))) {
        pv_cancel_inputs();
        __atomic_store_n(&g_pv_draw_ms, 0, __ATOMIC_RELEASE);
        if (!__atomic_load_n(&g_pv_info_floor, __ATOMIC_ACQUIRE))
            for (int d = 0; d < 4; d++) __atomic_store_n(&g_pv_tile_ms[d], 0, __ATOMIC_RELEASE);
        return;
    }
    if (pv_on()) {
        int configured=pv_count(az_get(AZF_PLAYER_VIEW_COUNT));
        unsigned selected=__atomic_load_n(&g_pv_focus_seq,__ATOMIC_ACQUIRE);
        if (g_pv_context_count>=0 && (configured!=g_pv_context_count || selected!=g_pv_context_focus)) {
            pv_cancel_inputs();
            __atomic_store_n(&g_pv_enlarged,-1,__ATOMIC_RELEASE);
        }
        g_pv_context_count=configured;g_pv_context_focus=selected;
        pv_prepare();
        int mode = *(volatile int *) (v + PL_MODE);
        if (mode < 0 || mode > 2) {
            pv_cancel_inputs();
            return;
        }
        if (wref_get(&g_pv_mode_view) != v) {
            wref_set(&g_pv_mode_view, v);
            g_pv_saved_mode = mode;
        }
        if (mode != 2) {
            *(volatile int *) (v + PL_MODE) = 2;
            ((void (*)(uintptr_t)) PL_RELAYOUT)(v);
            __atomic_store_n(&g_wv_mode, 2, __ATOMIC_RELEASE);
        }
        /* A native cached drawer does not reliably reallocate when its height
         * changes. Never resize or move it for a 1/2/4-player transition. */
        player_wave_source_tick(v, 2, 0, 0);
        /* Four-deck layouts can hide native pad tiles, so their paint getter
         * cannot be our mode-acknowledgement clock. Read the true getter without
         * modeget_hook's waveform-visibility side effects, only on this screen. */
        pv_modes_refresh();
        pv_controls_tick(v);
        pv_needle_tick(1);
        __atomic_store_n(&g_pv_input_active, 1, __ATOMIC_RELEASE);
        /* Repaint the moving player surface. The shared native FX panel and
         * header have their own invalidations; don't rebuild them for a clock
         * change below. Layout/FX/header changes still request a full frame. */
        static int last_count=-1,last_fx=-1;
        int count=pv_active_count(),fx=az_on(AZF_PLAYER_VIEW_FX);
        int full=__atomic_exchange_n(&g_pv_header_dirty,0,__ATOMIC_ACQ_REL) || last_count!=count || last_fx!=fx;
        last_count=count;last_fx=fx;
        int width=__atomic_load_n(&g_pv_draw_width,__ATOMIC_ACQUIRE);
        int rect[4] = {0,full?0:70,full?1280:width,full?800:730};
        ((void (*)(void *, const int *)) PM_REPAINT_AREA)((void *) v, rect);
        __atomic_fetch_add(&g_pv_tick_us_total,pv_now_us()-tick_at,__ATOMIC_RELAXED);
        __atomic_fetch_add(&g_pv_tick_n,1,__ATOMIC_RELAXED);
    } else {
        pv_cancel_inputs();
        player_wave_source_tick(v, *(volatile int *) (v + PL_MODE), 0, 0);
        __atomic_store_n(&g_pv_draw_ms, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_native_info, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_info_floor, 0, __ATOMIC_RELEASE);
        if (wref_get(&g_pv_mode_view) == v && *(volatile int *) (v + PL_MODE) == 2 && g_pv_saved_mode != 2) {
            *(volatile int *) (v + PL_MODE) = g_pv_saved_mode;
            ((void (*)(uintptr_t)) PL_RELAYOUT)(v);
            __atomic_store_n(&g_wv_mode, g_pv_saved_mode, __ATOMIC_RELEASE);
        }
        wref_set(&g_pv_mode_view, 0);
    }
}
static void pv_prepare(void) {
    if (!g_pv_canvas) mp_image_from_memory(&g_pv_canvas, PV_CANVAS, sizeof PV_CANVAS);
    if (!g_pv_snapshot) *(void **) &g_pv_snapshot = dlsym(RTLD_DEFAULT, "azstem_deck_snapshot");
    if (!g_pv_beat) *(void **) &g_pv_beat = dlsym(RTLD_DEFAULT, "azstem_deck_beat_phase");
    pv_library_start(g_pv_snapshot);
}
static unsigned pv_track_key(const struct azstem_deck_snap *sn) {
    if(sn->loaded!=1)return 0;
    unsigned key=2166136261u^sn->content_id;
    const char *parts[2]={sn->title,sn->artist};
    for(int k=0;k<2;k++) {
        for(const unsigned char *c=(const unsigned char *)parts[k];*c;c++)key=(key^*c)*16777619u;
        key=(key^0xffu)*16777619u;
    }
    return key?key:1;
}
static void pv_collect_mask(struct pv_deck ds[4], int waves, int only_deck,unsigned needed) {
    pv_analysis_clear();
    memset(ds, 0, 4 * sizeof *ds);
    int focus = __atomic_load_n(&g_pv_focus, __ATOMIC_ACQUIRE), master = __atomic_load_n(&g_master_deck, __ATOMIC_RELAXED);
    for (int d = 0; d < 4; d++) {
        if ((only_deck >= 0 && d != only_deck) || !(needed&(1u<<d))) continue;
        ds[d].loaded = -1;
        ds[d].tempo_range = ds[d].master_tempo = ds[d].quantize = ds[d].quantize_value = -1;
        ds[d].auto_cue = ds[d].auto_hotcue = ds[d].track_number = ds[d].beat_jump = -1;
        int mode = pad_mode_fresh(d % 2, d);
        ds[d].pad_mode = mode > 0 ? mode - 1 : -1;
        if (az_on(AZF_BEAT_JUMP_2_FIRST) && (ds[d].pad_mode == 5 || ds[d].pad_mode == 6))
            ds[d].pad_mode = 11 - ds[d].pad_mode;
        ds[d].pad_stems = __atomic_load_n(&g_erp_active[d], __ATOMIC_RELAXED) && stems_page(d % 2, d);
        ds[d].pad_drums=ds[d].pad_mode==7 && __atomic_load_n(&g_drum_arm,__ATOMIC_ACQUIRE);
        ds[d].panel_hidden=(__atomic_load_n(&g_pv_panel_hidden,__ATOMIC_ACQUIRE)>>d)&1u;
        ds[d].zoom=__atomic_load_n(&g_pv_zoom[d],__ATOMIC_ACQUIRE);
        ds[d].cursor_center=__atomic_load_n(&g_pv_center[d],__ATOMIC_ACQUIRE)>0;
        ds[d].cursor_native=__atomic_load_n(&g_pv_center[d],__ATOMIC_ACQUIRE)<0;
        ds[d].sheet=__atomic_load_n(&g_pv_sheet_deck,__ATOMIC_ACQUIRE)==d?
            __atomic_load_n(&g_pv_sheet_kind,__ATOMIC_ACQUIRE):0;
        ds[d].preview_q=__atomic_load_n(&g_pv_preview[d],__ATOMIC_ACQUIRE);
        ds[d].preview_active=ds[d].preview_q>=0;
        int candidate=__atomic_load_n(&g_pv_mark_candidate[d],__ATOMIC_ACQUIRE);
        ds[d].mark_q=candidate>=0?candidate:__atomic_load_n(&g_pv_mark[d],__ATOMIC_ACQUIRE);
        ds[d].mark_set=ds[d].mark_q>=0;
        ds[d].enlarged=__atomic_load_n(&g_pv_enlarged,__ATOMIC_ACQUIRE)==d;
        ds[d].seek_available=(__atomic_load_n(&g_pv_seek_mask,__ATOMIC_ACQUIRE)>>d)&1u;
        ds[d].controls = __atomic_load_n(&g_pv_controls_caps, __ATOMIC_ACQUIRE);
        if (g_orig_setgridadj && g_ep147_build==EPB_V200 && d==__atomic_load_n(&g_pv_focus,__ATOMIC_ACQUIRE))
            ds[d].controls|=PV_CTL_GRID;
        ds[d].pads_down = __atomic_load_n(&g_pv_pad_seen[d], __ATOMIC_ACQUIRE);
        ds[d].elapsed = ds[d].duration = ds[d].phase = -1;
        ds[d].tempo = NAN;
        ds[d].selected = d == focus;
        ds[d].master = d == master;
        struct azstem_deck_snap sn;
        memset(&sn, 0, sizeof sn);
        sn.size = sizeof sn;
        if (g_pv_snapshot && g_pv_snapshot(d, &sn) == 0) {
            unsigned identity=pv_track_key(&sn);
            if (g_pv_track_identity[d]!=identity) {
                __atomic_store_n(&g_pv_preview[d],-1,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_mark[d],-1,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_mark_candidate[d],-1,__ATOMIC_RELEASE);
                ds[d].mark_set=ds[d].mark_armed=0;
                __atomic_fetch_add(&g_pv_gesture_epoch,1,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_pad_down[d],0,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_pad_req[d],0,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_mode_req[d],0,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_panel_req[d],0,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_button_req[d],0,__ATOMIC_RELEASE);
                if(g_pv_sheet_deck==d) {g_pv_sheet_deck=-1;ds[d].sheet=0;}
                if (g_pv_needle_deck==d) {g_pv_needle_down=0;g_pv_needle_pending=0;g_pv_needle_cancelled=1;}
                g_pv_track_identity[d]=identity;
                ds[d].preview_active=0;
            }
            ds[d].loaded = sn.loaded;
            if(sn.loaded!=1)ds[d].controls&=~PV_CTL_GRID;
            ds[d].elapsed = sn.pos_s;
            snprintf(ds[d].title, sizeof ds[d].title, "%s", sn.title);
            snprintf(ds[d].artist, sizeof ds[d].artist, "%s", sn.artist);
            struct pv_library_data lib;
            if (pv_library_copy(d, &sn, &lib)) {
                ds[d].source_usb=1;
                g_pv_analysis_hold[d][0]=lib.wave;
                g_pv_analysis_hold[d][1]=lib.wave3;
                g_pv_analysis_hold[d][2]=lib.wave_blue;
                g_pv_grid_hold[d]=lib.beats;
                if(lib.beats) {ds[d].beat_times=lib.beats->times;ds[d].beat_n=lib.beats->count;ds[d].beat_first=lib.beats->first;}
                memcpy(ds[d].memory,lib.memory,sizeof ds[d].memory);ds[d].memory_n=lib.memory_n;
                if (lib.wave) {
                    ds[d].analysis=lib.wave->samples; ds[d].analysis_n=lib.wave->count; ds[d].analysis_kind=2;
                }
                snprintf(ds[d].key_name, sizeof ds[d].key_name, "%s", lib.key);
                memcpy(ds[d].hotcues, lib.cues, sizeof ds[d].hotcues);
                ds[d].countdown = lib.countdown;
                /* Preserve the native stem-recoloured/attenuated overview
                 * when a stem is muted; all-stems-on uses the detailed export. */
                unsigned stem_mask=__atomic_load_n(&g_pv_stems[d], __ATOMIC_RELAXED);
                if (lib.overview_n > 0 && (!(stem_mask & 0x80u) || (stem_mask & 15u) == 15u)) {
                    memcpy(g_pv_overview[d], lib.overview, (size_t)lib.overview_n*12);
                    ds[d].overview=g_pv_overview[d];
                    ds[d].overview_n=lib.overview_n;
                }
            }
        }
        pv_status_collect(&ds[d], d);
        long display_ms;
        if (ds[d].loaded != 0 && pm_wpos(d, &display_ms) && display_ms >= 0)
            ds[d].elapsed = (double) display_ms / 1000.0;
        double live = 0, track = 0;
        if (bpmstate_read(d, &live, &track) && isfinite(live) && live >= 20 && live <= 400) {
            ds[d].bpm = live;
            if (isfinite(track) && track >= 20 && track <= 400) {
                ds[d].track_bpm = track;
                ds[d].tempo = 100.0 * (live / track - 1.0);
            }
        }
        unsigned long bar = __atomic_load_n(&g_pm_bar[d], __ATOMIC_ACQUIRE);
        unsigned bpm100 = (unsigned) (bar >> 32), pos = (unsigned) bar;
        if (bpm100 >= 2000 && bpm100 <= 40000) ds[d].phase = fmod((double) pos / (6000000.0 / bpm100), 4.0);
        double ph = -1;
        if (g_pv_beat && g_pv_beat(d, &ph) == 0 && ph >= 0 && ph < 1 && ds[d].phase >= 0)
            ds[d].phase = floor(ds[d].phase) + ph;
        ds[d].stems = __atomic_load_n(&g_pv_stems[d], __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&g_wavemute,__ATOMIC_RELAXED) && __atomic_load_n(&g_att_have[d],__ATOMIC_ACQUIRE)) {
            ds[d].attenuation=g_att[d];
            ds[d].attenuation_n=ATT_N;
            if (__atomic_load_n(&g_wavecolor,__ATOMIC_RELAXED)) ds[d].tint=g_col[d];
        }
        int nb = waves ? __atomic_load_n(&g_wwbbase_n, __ATOMIC_RELAXED) : 0;
        for (int b = 0; b < nb && b < 8; b++) {
            if (g_wwbbase[b].deck != d) continue;
            for (int m = 0; m < 3; m++) {
                uintptr_t w = wwb_widget_live(b, m);
                int rr[4];
                if (!w || !ht_screen_rect(w + WIDGET_COMPONENT_OFF, rr)) continue;
                struct pv_image wave, grid;
                if (!pv_image(w + 0x108, &wave, 2048, 256) || !pv_image(w + 0x110, &grid, 2048, 64)) continue;
                if (wave.w != 1220 || grid.w != wave.w) continue;
                ds[d].wave = wave;
                ds[d].columns = (const unsigned char *)(w + 0x118);
                ds[d].column_height = *(const int *)(w + 0x8c);
                if (ds[d].column_height < 2 || ds[d].column_height > 256) ds[d].columns = NULL;
                ds[d].layered = m == 2;
                ds[d].wave_style=m+1;
                struct pv_analysis *analysis=g_pv_analysis_hold[d][m==0?2:m==2?1:0];
                if (analysis) {
                    ds[d].analysis=analysis->samples; ds[d].analysis_n=analysis->count; ds[d].analysis_kind=analysis->kind;
                    ds[d].summary=analysis->summary; ds[d].summary_n=analysis->summary_n;
                } else {
                    ds[d].analysis=NULL; ds[d].analysis_n=0;
                }
                /* BLUE uses the native blue amplitude/colour layers. PWV5
                 * is RGB detail and must not be advertised as native BLUE. */
                /* Genuine 150 Hz PWV3 data replaces the tiny Blue400 preview.
                 * Never use an RGB export as if it were BLUE. */
                if (m==2) { ds[d].summary=NULL; ds[d].summary_n=0; }
                /* An RGB export is not a 3-band overview. If that export
                 * lacks 2EX, keep the native 3-band overview as the fallback. */
                if (m==2 || m==0) { ds[d].overview=NULL; ds[d].overview_n=0; }
                if (g_pv_wave_time[b].owner == g_wwbbase[b].base &&
                    g_pv_wave_time[b].stamp > 0 && isfinite(g_pv_wave_time[b].zoom) &&
                    g_pv_wave_time[b].zoom > .01f && g_pv_wave_time[b].zoom < 100.f) {
                    ds[d].seconds_per_column = 1.0 / (150.0 * g_pv_wave_time[b].zoom);
                    ds[d].wave_position = g_pv_wave_time[b].position / 1000.0;
                }
                ds[d].grid = grid;
                ds[d].crop_x = PM_X0;
                ds[d].crop_w = PM_X1 - PM_X0;
                uintptr_t box = *(uintptr_t *) (w + WIDGET_COMPONENT_OFF + 0x18);
                uintptr_t playhead = pm_child(box, PM_PLAYHEAD_VT, 0);
                ds[d].playhead = playhead ? *(int *) (playhead + 0x20) : -1;
                int native_center=ds[d].playhead >= wave.w/2-4 && ds[d].playhead<=wave.w/2+4;
                if(ds[d].cursor_native)ds[d].cursor_center=native_center;
                if (native_center)
                    ds[d].crop_w = wave.w - 2;
                if (ds[d].zoom>0 && ds[d].zoom<=3) ds[d].crop_w>>=ds[d].zoom;
                break;
            }
            if (ds[d].wave.data) break;
        }
        int points = __atomic_load_n(&g_pv_points[d], __ATOMIC_RELAXED);
        if (ds[d].loaded != 0 && points > 0) ds[d].duration = (double) points / 150.0;
        int ov_source = ds[d].summary_n || ds[d].overview_n ? 5 : 0;
        int nt = __atomic_load_n(&g_ovtile_n, __ATOMIC_RELAXED);
        for (int i = 0; i < nt && i < 24 && !ds[d].overview_n && !ds[d].summary_n; i++) {
            /* wref stores the primary overview object. Its registered virtual
             * Component offset is used by destruction tracking, not subtracted
             * on reads (see tilector_hook and ov_refresh_if_needed). */
            uintptr_t o = wref_get(&g_ovtile_w[i]), vt = 0;
            if (g_ovtile[i].deck != d || !o || !own_q(o, &vt) || vt != OV_WIDGET_VT) continue;
            int cached = pv_cached_overview(o, &ds[d].overview_image);
            if (cached) {
                ov_source = cached;
                break;
            }
            uintptr_t raw_comp = 0;
            int raw_rect[4];
            if (!img_q(OV_WIDGET_VT - 0x18, &raw_comp) || raw_comp != 0x170 ||
                !ht_screen_rect(o + raw_comp, raw_rect)) continue;
            uintptr_t obj = *(uintptr_t *) (o + 0x80);
            if (!ht_ptr_ok(obj)) continue;
            uintptr_t content = *(uintptr_t *) (obj + 0x28);
            if (!ht_ptr_ok(content)) continue;
            const unsigned char *a = *(const unsigned char **) (content + 0x28);
            int n = *(int *) (content + 0x30);
            if (a && n > 0 && n <= OV_MAX) {
                memcpy(g_pv_overview[d], a, (size_t) n * OV_STRIDE);
                ds[d].overview = g_pv_overview[d];
                ds[d].overview_n = n;
                ov_source = 1;
                break;
            }
        }
        __atomic_store_n(&g_pv_overview_sources[d], ds[d].loaded == 0 ? 0 : ov_source, __ATOMIC_RELAXED);
        __atomic_store_n(&g_pv_loaded[d],ds[d].loaded,__ATOMIC_RELEASE);
    }
}
static void pv_collect(struct pv_deck ds[4],int waves,int only_deck) {
    pv_collect_mask(ds,waves,only_deck,15);
}
static unsigned pv_visible_mask(int count,int focus,int left,int right,int master) {
    unsigned mask=0;
    for(int i=0;i<count;i++) {
        int d=pv_deck_at(count,i,focus,left,right),ref=pv_reference(d,master,left,right);
        if(d>=0 && d<4)mask|=1u<<d;
        if(ref>=0 && ref<4)mask|=1u<<ref;
    }
    return mask;
}
/* These hooks run after the native per-deck children, so SOURCE and INFO keep
 * their own controls and metadata panels. No screen interception or repaint. */
static void pv_tile_paint(uintptr_t comp, void *g) {
    if (!pv_on() || !g || !on_msg_thread()) return;
    uintptr_t info_view = wref_get(&g_4wv_view);
    if ((info_view && pl_view_ok(info_view) && pv_info_open(info_view)) || pv_info_pending()) return;
    uintptr_t cv = 0, top = 0, full = 0, vt = 0;
    int rect[4];
    if (!own_q(comp, &cv) || !img_q(cv - 16, &top) || (intptr_t) top > 0 || (intptr_t) top < -0x1000 ||
        !ht_screen_rect(comp, rect) || rect[2] < 100 || rect[2] > 1280 || rect[3] < 45 || rect[3] > 256 ||
        rect[0] < 0 || rect[1] < 0 || rect[0] + rect[2] > 1280 || rect[1] + rect[3] > 800) return;
    full = comp + (intptr_t) top;
    if (!own_q(full, &vt)) return;
    int off = vt == HT_PI2D_VT || vt == HT_PI4D_VT ? HT_DECK_OFF : vt == HT_SW2_VT || vt == HT_SW4_VT ? HT_SW_DECK
                                                                                                      : -1;
    if (off < 0) return;
    if (!__atomic_load_n(&g_menu_open, __ATOMIC_RELAXED) &&
        !__atomic_load_n(&g_grid_adjust, __ATOMIC_RELAXED)) {
        uintptr_t view=wref_get(&g_4wv_view),host=0;
        /* Include the HT_SW* family actually patched by pv_install_footers.
         * The old PI-only branch never suppressed those four hidden composites.
         * A retained but detached WaveformView belongs to SOURCE: do not skip it. */
        if(view && view==wref_get(&g_pv_mode_view) && g_pv_canvas && pl_view_ok(view) &&
           own_q(view+0x18,&host) && ht_ptr_ok(host) && *(volatile int *)(view+PL_MODE)==2 &&
           !pm_trackinfo_open(view)) {
            __atomic_fetch_add(&g_pv_footer_skipped,1,__ATOMIC_RELAXED);return;
        }
    }
    int d = *(const int *) (full + off);
    if (d < 0 || d > 3) return;
    /* On SOURCE/INFO the old main-view input stamp must expire immediately.
     * A normal WaveformView paint publishes a new stamp after its footer paints. */
    __atomic_store_n(&g_pv_draw_ms, 0, __ATOMIC_RELEASE);
    pv_prepare();
    struct pv_image canvas;
    if (!pv_image((uintptr_t) &g_pv_canvas, &canvas, 1280, 800) || canvas.w != 1280 || canvas.h != 800 || canvas.pixel != 4) return;
    struct pv_deck decks[4];
    pv_collect(decks, 0, d);
    azmod_fb fb = {.px = (unsigned char *) canvas.data, .w = rect[2], .h = rect[3], .stride = canvas.stride};
    azmod_player_tile_draw(&fb, &decks[d], d, __atomic_load_n(&g_pv_elapsed_mask, __ATOMIC_RELAXED));
    ((void (*)(void *)) JUCE_G_SAVESTATE)(g);
    pm_fill(g, 0, 0, 0, 0, 0xffffff);
    ((void (*)(void *, uintptr_t *, int, int, int, int, int, int, int, int, int)) FXB_DRAWIMAGE)(
        g, &g_pv_canvas, 0, 0, rect[2], rect[3], 0, 0, rect[2], rect[3], 0);
    ((void (*)(void *)) JUCE_G_RESTORESTATE)(g);
    for (int i = 0; i < 4; i++) __atomic_store_n(&g_pv_tile_rect[d][i], rect[i], __ATOMIC_RELAXED);
    __atomic_store_n(&g_pv_tile_ms[d], now_ms(), __ATOMIC_RELEASE);
}
static void (*g_pv_footer_orig[2])(void *, void *);
static _Atomic int g_pv_footer_rc[2] = {-9, -9};
static void pv_footer2(void *c, void *g) {
    g_pv_footer_orig[0](c, g);
    pv_tile_paint((uintptr_t) c, g);
}
static void pv_footer4(void *c, void *g) {
    g_pv_footer_orig[1](c, g);
    pv_tile_paint((uintptr_t) c, g);
}
static void pv_install_footers(void) {
    const uintptr_t primaries[2] = {HT_SW2_VT, HT_SW4_VT};
    void (*hooks[2])(void *, void *) = {pv_footer2, pv_footer4};
    for (int k = 0; k < 2; k++) {
        uintptr_t ti = 0, delta = 0;
        if (!img_q(primaries[k] - 8, &ti) || !ti || !img_q(primaries[k] - 24, &delta) || delta != 0x280) continue;
        /* Both verified firmware families: Component offset 0x280. Locate its
         * secondary vtable by exact offset-to-top and the same RTTI identity;
         * patch only its original empty paintOverChildren slot. */
        for (uintptr_t off = 0; off < 0x500; off += 8) {
            uintptr_t t = 0, type = 0;
            if (!img_q(primaries[k] + off, &t) || t != (uintptr_t) -(intptr_t) delta ||
                !img_q(primaries[k] + off + 8, &type) || type != ti) continue;
            g_pv_footer_rc[k] = ht_patch_if(primaries[k] + off + 16 + 27 * 8, HT_EMPTY_STUB, (void *) hooks[k], (void **) &g_pv_footer_orig[k]);
            break;
        }
    }
}
static int pv_info_paint(uintptr_t view, void *g, struct pv_image canvas) {
    int bottom = pv_info_bottom(view);
    __atomic_store_n(&g_pv_info_floor, 0, __ATOMIC_RELEASE);
    if (!bottom) return 0;
    struct pv_deck ds[4];
    pv_prepare();
    pv_collect(ds, 0, -1);
    /* Native metadata, tabs and close button remain untouched. Clear every
     * pixel below their actual bounds, including the old AZ waveform strip. */
    int height = (800 - bottom) / 2;
    if (height > 110) height = 110;
    int top = 800 - height * 2;
    for (int y = bottom; y < 800; y++)
        for (int x = 0; x < 1280; x++)
            *(uint32_t *) ((unsigned char *) canvas.data + (long) y * canvas.stride + 4 * x) = 0xff000000u;
    for (int d = 0; d < 4; d++) {
        int rect[4] = {640 * (d % 2), top + height * (d / 2), 640, height};
        azmod_fb fb = {.px = (unsigned char *) canvas.data + (long) rect[1] * canvas.stride + 4 * rect[0],
                       .w = rect[2],
                       .h = rect[3],
                       .stride = canvas.stride};
        azmod_player_tile_draw(&fb, &ds[d], d, __atomic_load_n(&g_pv_elapsed_mask, __ATOMIC_RELAXED));
        for (int i = 0; i < 4; i++) __atomic_store_n(&g_pv_tile_rect[d][i], rect[i], __ATOMIC_RELAXED);
        __atomic_store_n(&g_pv_tile_ms[d], now_ms(), __ATOMIC_RELEASE);
    }
    ((void (*)(void *)) JUCE_G_SAVESTATE)(g);
    pm_fill(g, 0, 0, 0, 0, 0xffffff);
    ((void (*)(void *, uintptr_t *, int, int, int, int, int, int, int, int, int)) FXB_DRAWIMAGE)(
        g, &g_pv_canvas, 0, bottom, 1280, 800 - bottom, 0, bottom, 1280, 800 - bottom, 0);
    ((void (*)(void *)) JUCE_G_RESTORESTATE)(g);
    __atomic_store_n(&g_pv_info_floor, bottom, __ATOMIC_RELEASE);
    return 1;
}
static int pv_paint(uintptr_t view, void *g) {
    long paint_at=pv_now_us();
    __atomic_store_n(&g_pv_draw_ms, 0, __ATOMIC_RELEASE);
    if (!pv_on() || !g || !on_msg_thread() ||
        __atomic_load_n(&g_menu_open, __ATOMIC_ACQUIRE) || !pl_view_ok(view) ||
        view != wref_get(&g_pv_mode_view) || *(volatile int *) (view + PL_MODE) != 2 ||
        __atomic_load_n(&g_grid_adjust, __ATOMIC_RELAXED)) return 0;
    int vr[4];
    if (!ht_screen_rect(view, vr) || vr[2] != 1280 || vr[3] != 800) return 0;
    struct pv_image canvas;
    if (!pv_image((uintptr_t) &g_pv_canvas, &canvas, 1280, 800) || canvas.w != 1280 || canvas.h != 800 || canvas.pixel != 4) return 0;
    if (pv_info_open(view)) return pv_info_paint(view, g, canvas);
    if (pv_shared_modal(view)) return 0;
    if (pv_info_pending()) return 0;
    int left = __atomic_load_n(&g_pv_sides[0], __ATOMIC_RELAXED), right = __atomic_load_n(&g_pv_sides[1], __ATOMIC_RELAXED);
    int focus = pv_active_focus(),count = pv_active_count();
    unsigned needed=pv_visible_mask(count,focus,left,right,__atomic_load_n(&g_master_deck,__ATOMIC_RELAXED));
    long collect_at=pv_now_us();
    struct pv_deck ds[4];
    pv_collect_mask(ds, 1, -1,needed);
    long render_at=pv_now_us();
    /* A track loading its first raster keeps the CDJ frame and metadata; it
     * must not flash the underlying AZ layout for a frame. */
    for (int i = 0; i < count; i++) {
        int d = pv_deck_at(count, i, focus, left, right);
        if (ds[d].loaded == 1 && !ds[d].wave.data) {
            g_pv_missing++;
        }
    }
    int fx[4] = {0}, width = 1280;
    if (az_on(AZF_PLAYER_VIEW_FX)) {
        uintptr_t container = pm_child(view, PL_FXSIDE_VT, 1);
        uintptr_t native_fx = container ? pv_child_class(container, "N3gui8waveform12FxBodyWidgetE", 1) : 0;
        if (native_fx && ht_screen_rect(native_fx, fx) && fx[0] >= 960 && fx[0] <= 1200 && fx[1] >= 70 &&
            fx[2] > 0 && fx[0] + fx[2] <= 1280 && fx[3] > 0 && fx[1] + fx[3] <= 800) width = fx[0];
        else memset(fx, 0, sizeof fx);
    }
    if (__atomic_load_n(&g_pv_draw_width,__ATOMIC_ACQUIRE)!=width) {
        pv_cancel_inputs();
        for(int d=0;d<4;d++)ds[d].sheet=0;
    }
    for (int i = 0; i < 4; i++) __atomic_store_n(&g_pv_fx_rect[i], fx[i], __ATOMIC_RELAXED);
    __atomic_store_n(&g_pv_draw_width, width, __ATOMIC_RELAXED);
    azmod_fb fb = {.px = (unsigned char *) canvas.data, .w = width, .h = 800, .stride = canvas.stride};
    azmod_player_draw(&fb, ds, count, focus, left, right, __atomic_load_n(&g_pv_phase_mask, __ATOMIC_RELAXED),
                      __atomic_load_n(&g_pv_elapsed_mask, __ATOMIC_RELAXED));
    unsigned can_seek=0;
    for (int d=0;d<4;d++) {
        if(!(needed&(1u<<d)))continue;
        int slot=-1; uintptr_t p=pv_needle_widget(d,&slot);
        if (p && *(const unsigned char *)(p+0x9d) && !(*(const unsigned char *)(p+0x161)&0x10)) can_seek|=1u<<d;
    }
    __atomic_store_n(&g_pv_seek_mask,can_seek,__ATOMIC_RELEASE);
    /* Keep EP147's shared status, SINGLE, SMART CUE and TRACK INFO controls.
     * Only replace its layout tabs and the player content below the header. */
    ((void (*)(void *)) JUCE_G_SAVESTATE)(g);
    pm_fill(g, 0, 0, 0, 0, 0xffffff); /* full opacity, without changing a pixel */
    ((void (*)(void *, uintptr_t *, int, int, int, int, int, int, int, int, int)) FXB_DRAWIMAGE)(
        g, &g_pv_canvas, 0, 70, width, 730, 0, 70, width, 730, 0);
    if (width < 1280) {
        /* Leave only the real FX component uncovered. Blank old deck fragments
         * above/below it, without moving its controls or synthesising clicks. */
        pm_fill(g, width, 70, 1280 - width, fx[1] - 70, 0x000000);
        pm_fill(g, width, fx[1] + fx[3], 1280 - width, 800 - fx[1] - fx[3], 0x000000);
        pm_fill(g, fx[0] + fx[2], fx[1], 1280 - fx[0] - fx[2], fx[3], 0x000000);
    }
    ((void (*)(void *, uintptr_t *, int, int, int, int, int, int, int, int, int)) FXB_DRAWIMAGE)(
        g, &g_pv_canvas, 0, 0, 380, 70, 0, 0, 380, 70, 0);
    struct pv_rect button = pv_fx_button();
    ((void (*)(void *, uintptr_t *, int, int, int, int, int, int, int, int, int)) FXB_DRAWIMAGE)(
        g, &g_pv_canvas, button.x, button.y, button.w, button.h, button.x, button.y, button.w, button.h, 0);
    ((void (*)(void *)) JUCE_G_RESTORESTATE)(g);
    __atomic_fetch_add(&g_pv_paint_us_total,pv_now_us()-paint_at,__ATOMIC_RELAXED);
    __atomic_fetch_add(&g_pv_collect_us_total,render_at-collect_at,__ATOMIC_RELAXED);
    __atomic_fetch_add(&g_pv_render_us_total,pv_now_us()-render_at,__ATOMIC_RELAXED);
    g_pv_frames++;
    for (int d = 0; d < 4; d++)
        if (ds[d].loaded == 1 && !ds[d].wave.data) g_pv_missing++;
    __atomic_store_n(&g_pv_draw_ms, now_ms(), __ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_input_seen_ms, now_ms(), __ATOMIC_RELEASE);
    for (int d = 0; d < 4; d++) __atomic_store_n(&g_pv_tile_ms[d], 0, __ATOMIC_RELEASE);
    return 1;
}
#include "player_view_touch.c"
/* A Player View gesture belongs to this view until release. No hidden native
 * controls may receive its down or up. Physical deck and browser buttons pass. */
static int pv_xevent(XEvent *ev) {
    if (!ev) return 0;
    unsigned contact=__atomic_load_n(&g_pv_contact_epoch,__ATOMIC_ACQUIRE);
    if(g_pv_touch_owned && contact!=g_pv_owned_contact_epoch) {
        if(g_pv_mark_touch>=0)__atomic_store_n(&g_pv_mark_candidate[g_pv_mark_touch],-1,__ATOMIC_RELEASE);
        g_pv_touch_owned=0;g_pv_touch_deck=-1;g_pv_needle_touch=-1;g_pv_preview_touch=-1;g_pv_mark_touch=-1;
        if(ev->type!=ButtonPress) {ev->type=NoExpose;return 1;}
    }
    if(ev->type==ButtonPress && !g_pv_touch_owned)g_pv_owned_contact_epoch=contact;
    if (ev->type == FocusOut && g_pv_touch_owned) {
        pv_cancel_inputs();
        g_pv_touch_deck = -1;
        g_pv_needle_touch = -1;
        g_pv_preview_touch=-1;
        g_pv_mark_touch=-1;
        g_pv_touch_owned = 0;
        return 0;
    }
    if (g_pv_touch_owned && (ev->type == ButtonRelease || ev->type == MotionNotify)) {
        if (g_pv_mark_touch>=0)pv_mark_publish(ev->type==MotionNotify?ev->xmotion.x_root:ev->xbutton.x_root,ev->type==ButtonRelease);
        if (g_pv_preview_touch>=0)
            pv_preview_publish(ev->type==MotionNotify?ev->xmotion.x_root:ev->xbutton.x_root);
        if (g_pv_needle_touch >= 0) {
            int gx=ev->type == MotionNotify ? ev->xmotion.x_root : ev->xbutton.x_root;
            pv_needle_publish(gx,ev->type != ButtonRelease && pv_on());
        }
        if (ev->type == ButtonRelease) {
            if (g_pv_touch_deck >= 0 && g_pv_touch_deck < 4)
                __atomic_store_n(&g_pv_pad_down[g_pv_touch_deck], 0, __ATOMIC_RELEASE);
            g_pv_touch_deck = -1;
            g_pv_touch_owned = 0;
            g_pv_needle_touch = -1;
            g_pv_preview_touch=-1;
            g_pv_mark_touch=-1;
        }
        ev->type = NoExpose;
        return 1;
    }
    if (g_pv_touch_owned && ev->type==ButtonPress) { ev->type=NoExpose;return 1; }
    if (ev->type != ButtonPress || ev->xbutton.button != Button1 || !pv_on()) return 0;
    if(__atomic_load_n(&g_pv_contact_cancel,__ATOMIC_ACQUIRE)) {ev->type=NoExpose;return 1;}
    int x = ev->xbutton.x_root, y = ev->xbutton.y_root;
    if (x < 0 || y < 0 || x >= 1280 || y >= 800) return 0;
    if (pv_info_pending() || __atomic_load_n(&g_pv_shared_modal,__ATOMIC_ACQUIRE) || __atomic_load_n(&g_grid_adjust,__ATOMIC_ACQUIRE)) return 0;
    int info = __atomic_load_n(&g_pv_native_info, __ATOMIC_ACQUIRE);
    int floor = info ? __atomic_load_n(&g_pv_info_floor, __ATOMIC_ACQUIRE) : 0;
    if (info && (!floor || y < floor)) return 0;
    for (int d = 0; d < 4; d++) {
        long stamp = __atomic_load_n(&g_pv_tile_ms[d], __ATOMIC_ACQUIRE);
        if (!stamp || (!floor && now_ms() - stamp > 150)) continue;
        struct pv_rect tile = {__atomic_load_n(&g_pv_tile_rect[d][0], __ATOMIC_RELAXED), __atomic_load_n(&g_pv_tile_rect[d][1], __ATOMIC_RELAXED),
                               __atomic_load_n(&g_pv_tile_rect[d][2], __ATOMIC_RELAXED), __atomic_load_n(&g_pv_tile_rect[d][3], __ATOMIC_RELAXED)};
        if (!pv_inside(tile, x, y)) continue;
        int time_x = 2 * (tile.w / 60 + 2) + tile.w / 12;
        struct pv_rect time = {tile.x + time_x, tile.y + tile.h / 4, tile.w * 56 / 100, tile.h / 3};
        if (pv_inside(time, x, y)) __atomic_fetch_xor(&g_pv_elapsed_mask, 1u << d, __ATOMIC_RELAXED);
        /* Custom footer geometry must not activate an invisible AZ needle-
         * search or title control at the same point. INFO above it stays native. */
        g_pv_touch_owned = 1;
        ev->type = NoExpose;
        return 1;
    }
    if (info) {
        g_pv_touch_owned = 1;
        ev->type = NoExpose;
        return 1;
    }
    if (__atomic_load_n(&g_menu_open, __ATOMIC_RELAXED) ||
        now_ms() - __atomic_load_n(&g_pv_draw_ms, __ATOMIC_ACQUIRE) > 150) return 0;
    struct pv_rect fx_button = pv_fx_button();
    if (y < 70 && pv_inside(fx_button, x, y)) {
        __atomic_fetch_xor(&g_pv_fx_request, 1u, __ATOMIC_RELEASE);
        g_pv_touch_owned = 1;
        ev->type = NoExpose;
        return 1;
    }
    if (y < 70 && x >= 380) {
        /* Native top-right INFO button. Pass the original gesture through and
         * yield our next repaint while EP147 builds its native modal panel. */
        if (x >= 1220) __atomic_store_n(&g_pv_info_until, now_ms() + 1000, __ATOMIC_RELEASE);
        return 0;
    }
    struct pv_rect fx_rect = {__atomic_load_n(&g_pv_fx_rect[0], __ATOMIC_RELAXED), __atomic_load_n(&g_pv_fx_rect[1], __ATOMIC_RELAXED),
                              __atomic_load_n(&g_pv_fx_rect[2], __ATOMIC_RELAXED), __atomic_load_n(&g_pv_fx_rect[3], __ATOMIC_RELAXED)};
    if (az_on(AZF_PLAYER_VIEW_FX) && pv_inside(fx_rect, x, y)) return 0;
    if (y>=70 && pv_sheet_touch(x,y)) {
        g_pv_touch_owned=1;ev->type=NoExpose;return 1;
    }
    if (y < 70) {
        for (int i = 0; i < 3; i++)
            if (pv_inside((struct pv_rect) {210 + i * 56, 10, 52, 48}, x, y)) {
                pv_cancel_inputs();
                __atomic_store_n(&g_pv_enlarged,-1,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_layout_request, i + 1, __ATOMIC_RELEASE);
            }
    } else {
        int count = pv_active_count();
        int focus = pv_active_focus(), left = __atomic_load_n(&g_pv_sides[0], __ATOMIC_RELAXED),
            right = __atomic_load_n(&g_pv_sides[1], __ATOMIC_RELAXED);
        for (int i = 0; i < count; i++) {
            int width = __atomic_load_n(&g_pv_draw_width, __ATOMIC_RELAXED);
            struct pv_layout l = pv_geometry_at(width, 800, count, i, az_value(AZF_OVERLAYS) ? 96 : 70);
            int deck = pv_deck_at(count, i, focus, left, right);
            int shown_mode=pad_mode_fresh(deck%2,deck)-1;
            int pad_stems=__atomic_load_n(&g_erp_active[deck],__ATOMIC_RELAXED) && stems_page(deck%2,deck);
            unsigned stems=__atomic_load_n(&g_pv_stems[deck],__ATOMIC_ACQUIRE);
            unsigned hidden=(__atomic_load_n(&g_pv_panel_hidden,__ATOMIC_ACQUIRE)>>deck)&1u;
            l=pv_layout_state(l,shown_mode,pad_stems,(stems&0x80u)!=0,hidden);
            if (!pv_inside(l.panel,x,y)) continue;
            if (pv_inside(pv_counter_hit(l),x,y)) {
                __atomic_store_n(&g_pv_mark[deck],-1,__ATOMIC_RELEASE);
                __atomic_store_n(&g_pv_mark_candidate[deck],-1,__ATOMIC_RELEASE);
                break;
            }
            if(pv_inside(pv_mark_hit(l),x,y)) {
                if(__atomic_load_n(&g_pv_loaded[deck],__ATOMIC_ACQUIRE)==1) {
                    g_pv_mark_touch=deck;g_pv_mark_rect=l.overview;
                    g_pv_mark_epoch=__atomic_load_n(&g_pv_gesture_epoch,__ATOMIC_ACQUIRE);
                    pv_mark_publish(x,0);
                }
                break;
            }
            if (pv_inside(pv_return_button(l),x,y) && __atomic_load_n(&g_pv_preview[deck],__ATOMIC_ACQUIRE)>=0) {
                __atomic_store_n(&g_pv_preview[deck],-1,__ATOMIC_RELEASE);break;
            }
            if (pv_inside(pv_back_button(l),x,y) && __atomic_load_n(&g_pv_enlarged,__ATOMIC_ACQUIRE)==deck) {
                pv_cancel_inputs();__atomic_store_n(&g_pv_enlarged,-1,__ATOMIC_RELEASE);break;
            }
            if (pv_inside(l.overview,x,y)) {
                if ((__atomic_load_n(&g_pv_seek_mask,__ATOMIC_ACQUIRE)>>deck)&1u) {
                    g_pv_needle_touch=deck;
                    g_pv_needle_gesture=l.overview;
                    __atomic_store_n(&g_pv_preview[deck],-1,__ATOMIC_RELEASE);
                    __atomic_store_n(&g_pv_needle_cancelled,0,__ATOMIC_RELAXED);
                    __atomic_store_n(&g_pv_needle_contact_epoch,__atomic_load_n(&g_pv_contact_epoch,__ATOMIC_ACQUIRE),__ATOMIC_RELAXED);
                    __atomic_store_n(&g_pv_needle_count,count,__ATOMIC_RELAXED);
                    pv_needle_publish(x,1);
                    __atomic_store_n(&g_pv_needle_pending,1,__ATOMIC_RELEASE);
                } else {
                    g_pv_preview_touch=deck;g_pv_preview_rect=l.overview;
                    g_pv_preview_epoch=__atomic_load_n(&g_pv_gesture_epoch,__ATOMIC_ACQUIRE);
                    pv_preview_publish(x);
                }
                break;
            }
            int opened=0;
            for (int tab = 0; tab < 3; tab++)
                if (pv_inside(pv_pad_tab(l, tab), x, y)) {
                    if (l.lcd.w<900 || (deck!=left && deck!=right)) {
                        pv_open_sheet(deck,PV_SHEET_LOOP+tab);opened=1;break;
                    }
                    int mode = __atomic_load_n(&g_true_mode_deck[deck], __ATOMIC_ACQUIRE) - 1;
                    int request = tab + 1;
                    if (pv_pad_choice(mode)==tab) request=hidden?0:tab==1?2:tab==0?4:5;
                    __atomic_fetch_and(&g_pv_panel_hidden,~(1u<<deck),__ATOMIC_RELEASE);
                    if (request) __atomic_store_n(&g_pv_mode_req[deck], request, __ATOMIC_RELEASE);
                }
            if (opened) break;
            struct pv_rect title=l.title;title.w=l.tabs.x-title.x;
            if (pv_inside(title,x,y)) { pv_open_sheet(deck,PV_SHEET_INFO);break; }
            if (pv_inside(l.key,x,y)) { pv_open_sheet(deck,PV_SHEET_KEY);break; }
            if (pv_inside(pv_panel_close(l),x,y))
                __atomic_fetch_or(&g_pv_panel_hidden,1u<<deck,__ATOMIC_RELEASE);
            if (deck == left || deck == right) {
                int mode = pad_mode_fresh(deck % 2, deck) - 1;
                int display_mode=mode;
                if (az_on(AZF_BEAT_JUMP_2_FIRST) && (mode==5 || mode==6)) display_mode=11-mode;
                if (!pad_stems && (mode==3 || mode==4 || mode==5 || mode==6)) {
                    int current=display_mode==4 || display_mode==6;
                    for (int page=0;page<2;page++)
                        if (current!=page && pv_inside(pv_page_button(l,page),x,y))
                            __atomic_store_n(&g_pv_mode_req[deck],mode==3 || mode==4?4:5,__ATOMIC_RELEASE);
                }
                for (int pad = 0; pad < 8; pad++) {
                    if(__atomic_load_n(&g_pv_loaded[deck],__ATOMIC_ACQUIRE)!=1)continue;
                    if(mode==7 && __atomic_load_n(&g_drum_arm,__ATOMIC_ACQUIRE))continue;
                    if (!pv_show_pads(mode,pad_stems) || !pv_inside(pv_pad_button(l, pad), x, y)) continue;
                    g_pv_touch_deck = deck;
                    __atomic_store_n(&g_pv_pad_down[deck], __atomic_load_n(&g_pv_contact_down,__ATOMIC_ACQUIRE)!=0, __ATOMIC_RELEASE);
                    __atomic_store_n(&g_pv_pad_req[deck], (unsigned)(mode << 8) | (pad + 1u), __ATOMIC_RELEASE);
                }
                struct pv_rect jump = pv_jump_button(l);
                if (pv_inside(jump, x, y))
                    __atomic_store_n(&g_pv_panel_req[deck], x < jump.x + jump.w/2 ? 1 : 2, __ATOMIC_RELEASE);
            }
            unsigned caps = __atomic_load_n(&g_pv_controls_caps, __ATOMIC_ACQUIRE);
            if ((caps & PV_CTL_AUTO_CUE) && pv_inside(pv_auto_hit(l, 0), x, y))
                __atomic_fetch_or(&g_pv_controls_req, PV_CTL_AUTO_CUE, __ATOMIC_RELEASE);
            if ((caps & PV_CTL_AUTO_HOTCUE) && pv_inside(pv_auto_hit(l, 1), x, y))
                __atomic_fetch_or(&g_pv_controls_req, PV_CTL_AUTO_HOTCUE, __ATOMIC_RELEASE);
            if ((caps & PV_CTL_QUANT_VALUE) && pv_inside(pv_quant_button(l), x, y))
                __atomic_fetch_or(&g_pv_controls_req, PV_CTL_QUANT_VALUE, __ATOMIC_RELEASE);
            if (pv_inside(l.compare, x, y)) __atomic_fetch_xor(&g_pv_phase_mask, 1u << deck, __ATOMIC_RELAXED);
            if (pv_inside(l.time, x, y)) __atomic_fetch_xor(&g_pv_elapsed_mask, 1u << deck, __ATOMIC_RELAXED);
            if ((stems & 0x80u) && pv_inside(l.stems, x, y)) {
                int slot = (x - l.stems.x) * 4 / l.stems.w;
                if (slot >= 0 && slot < 4 && (stems & (1u << (8 + slot))))
                    __atomic_fetch_xor(&g_pv_stem_req[deck], 1u << slot, __ATOMIC_RELEASE);
            }
        }
    }
    g_pv_touch_owned = 1;
    ev->type = NoExpose;
    return 1;
}
