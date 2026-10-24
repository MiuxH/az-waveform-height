/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AZMOD_PLAYER_VIEW_H
#define AZMOD_PLAYER_VIEW_H
#include <stdint.h>
typedef struct azmod_fb azmod_fb;
#include "player_cues.h"
#include "player_actions.h"
/* All image pointers are borrowed for one message-thread draw, never retained. */
struct pv_image {
    const unsigned char *data;
    int w, h, stride, pixel;
};
struct pv_deck {
    char title[160], artist[128];
    int loaded, master, selected;
    unsigned stems; /* bit7 engaged; bits0..3 on; bits8..11 present */
    double bpm, track_bpm, tempo, elapsed, duration, phase;
    struct pv_image wave, grid, overview_image;
    int crop_x, crop_w, playhead;
    const unsigned char *overview;
    int overview_n;
    char key_name[32];
    int tempo_range, master_tempo, quantize, quantize_value, auto_cue, auto_hotcue, track_number, beat_jump, pad_mode;
    int pad_stems;
    unsigned controls, pads_down;
    struct pv_cue hotcues[8];
    struct pv_countdown countdown;
    const unsigned char *columns; /* live native 1220 x 28 amplitude/colour cells */
    int column_height, layered;
    double seconds_per_column, wave_position;
    const unsigned char *analysis;
    int analysis_n, analysis_kind;
    const unsigned short *attenuation;
    const unsigned *tint;
    int attenuation_n;
    int panel_hidden, wave_style, source_usb;
    const struct pv_peak *summary;
    int summary_n;
    int zoom, cursor_center, sheet, preview_active, preview_q, enlarged;
    unsigned seek_available;
    int pad_drums;
    const double *beat_times;
    int beat_n,beat_first;
    struct pv_cue memory[PV_MEMORY_N];
    int memory_n;
    int cursor_native;
    int mark_set, mark_q, mark_armed; /* visual target; mark_armed reserved since v21 */
};
enum { PV_SHEET_TOOLS=1, PV_SHEET_LOOP, PV_SHEET_KEY, PV_SHEET_JUMP, PV_SHEET_INFO, PV_SHEET_SLIP };
static inline int pv_beat_at(const struct pv_deck *d,double seconds,double *out) {
    int lo=0,hi=d->beat_n-1;
    if(!d->beat_times || hi<1 || !(seconds>=d->beat_times[0] && seconds<=d->beat_times[hi]))return 0;
    while(lo+1<hi) {int mid=lo+(hi-lo)/2;if(d->beat_times[mid]<=seconds)lo=mid;else hi=mid;}
    double a=d->beat_times[lo],b=d->beat_times[hi];
    if(!(b>a))return 0;
    *out=d->beat_first+lo+(seconds-a)/(b-a);return 1;
}
struct pv_rect {
    int x, y, w, h;
};
struct pv_layout {
    struct pv_rect panel, lcd, title, compare, wave, overview, time, bpm, tempo, player, key, cues, axis, stems, tabs, pads;
};
static inline int pv_count(int choice) { return choice == 1 ? 2 : choice == 2 ? 4
                                                                              : 1; }
static inline int pv_inside(struct pv_rect r, int x, int y) {
    return r.w > 0 && r.h > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static inline int pv_deck_at(int count, int index, int focus, int left, int right) {
    return count == 4 ? index : count == 2 ? (index ? right : left)
                                           : focus;
}
static inline struct pv_layout pv_geometry_at(int w, int h, int count, int index, int header) {
    struct pv_layout r = {0};
    if (w < 640 || h < 400 || (count != 1 && count != 2 && count != 4) || index < 0 || index >= count || header < 70 || header > h / 3)
        return r;
    int cols = count == 1 ? 1 : 2, rows = count == 4 ? 2 : 1;
    int x0 = w * (index % cols) / cols, x1 = w * (index % cols + 1) / cols;
    int y0 = header + (h - header) * (index / cols) / rows, y1 = header + (h - header) * (index / cols + 1) / rows;
    r.panel = (struct pv_rect) {x0 + 2, y0 + 2, x1 - x0 - 4, y1 - y0 - 4};
    /* Each player owns its full screen partition. Two players use two tall
     * panes; retaining a fixed LCD aspect ratio would letterbox half the screen. */
    int pw = r.panel.w, ph = r.panel.h;
    int x = r.panel.x, y = r.panel.y;
    r.lcd = (struct pv_rect) {x, y, pw, ph};
    /* Vertical proportions follow the partition height, independently of its
     * width. Full-height one/two players therefore share the same instrument
     * rows; two panes reflow text horizontally instead of stretching audio. */
    int unit = ph * 1000 / 720;
    int gap = 6 * unit / 1000;
    if (gap < 2) gap = 2;
    int title_h = ph * 12 / 100, compare_h = ph * 12 / 100;
    int status_h = ph * 15 / 100, tail_h = ph * 18 / 100;
    /* Compact players reflow the instrumentation instead of quarter-scaling
     * every label. Tiny windows use the original bounded fallback geometry. */
    if (pw>=470 && ph>=300 && pw<900) {
        if (title_h<54) title_h=54;
        if (compare_h<38) compare_h=38;
        if (status_h<66) status_h=66;
        if (tail_h<62) tail_h=62;
    }
    int status_y = y + ph - status_h - tail_h - gap;
    r.title = (struct pv_rect){x,y,pw,title_h};
    r.tabs = (struct pv_rect){x+pw*700/1000,y+1,pw*295/1000,title_h-2};
    r.stems = (struct pv_rect){x+pw*700/1000,y+title_h*2/3,pw*295/1000,title_h/3-1};
    /* The comparison and main wave share one horizontal coordinate system.
     * The comparison's left counter gutter is clipped when drawing. */
    r.compare = (struct pv_rect){x+2,y+title_h+gap,pw-4,compare_h};
    int wave_gap=gap/3;
    if (wave_gap<1) wave_gap=1;
    int wave_y = r.compare.y + compare_h + wave_gap;
    r.wave = (struct pv_rect){x+2,wave_y,pw-4,status_y-wave_y-gap};
    /* Closed is the natural geometry. pv_layout_state() inserts an operation
     * row only when it is actually open, without an empty reserved strip. */
    r.pads = (struct pv_rect){x+pw*100/1000,status_y-gap,pw*775/1000,0};
    r.player = (struct pv_rect){x+pw*15/1000,status_y,pw*68/1000,status_h};
    r.time = (struct pv_rect){x+pw*340/1000,status_y,pw*325/1000,status_h};
    r.tempo = (struct pv_rect){x+pw*685/1000,status_y,pw*145/1000,status_h};
    r.bpm = (struct pv_rect){x+pw*850/1000,status_y,pw*145/1000,status_h};
    int tail_y = status_y+status_h+gap, cue_h=22*unit/1000, axis_h=18*unit/1000;
    r.cues = (struct pv_rect){x+pw*100/1000,tail_y,pw*790/1000,cue_h};
    r.axis = (struct pv_rect){r.cues.x,y+ph-axis_h-2,r.cues.w,axis_h};
    r.overview = (struct pv_rect){r.cues.x,tail_y+cue_h+2,r.cues.w,r.axis.y-tail_y-cue_h-3};
    r.key = (struct pv_rect){x+pw*915/1000,r.overview.y,pw*75/1000,r.axis.y+r.axis.h-r.overview.y};
    return r;
}
/* CDJ touch pads only appear for an operation tab; HOT CUE lives on hardware. */
static inline int pv_show_pads(int mode, int stems) { return stems || (mode >= 3 && mode <= 8); }
static inline struct pv_layout pv_layout_mode(struct pv_layout l, int mode, int stems) {
    if (pv_show_pads(mode, stems) && l.lcd.w>=900) {
        int row_h=l.player.h*3/5, gap=l.player.h/12;
        if (gap<2) gap=2;
        l.pads.h=row_h;
        l.pads.y=l.player.y-row_h-gap;
        l.wave.h=l.pads.y-gap-l.wave.y;
    }
    return l;
}
static inline struct pv_layout pv_layout_state(struct pv_layout l, int mode, int stems, int strip, int hidden) {
    if (!hidden) l=pv_layout_mode(l,mode,stems);
    if (strip) l.tabs.h=l.title.h*2/3-2;
    return l;
}
static inline struct pv_rect pv_panel_close(struct pv_layout l) {
    int w=l.lcd.w*35/1000;
    return (struct pv_rect){l.lcd.x+l.lcd.w-w-3,l.pads.y,w,l.pads.h/2};
}
static inline struct pv_rect pv_page_button(struct pv_layout l, int page) {
    int w=l.lcd.w*45/1000;
    return (struct pv_rect){l.lcd.x+l.lcd.w*895/1000+page*w,l.pads.y+l.pads.h/2,w-2,l.pads.h/2};
}
static inline int pv_cursor_x(struct pv_rect r) { return r.x + r.w * 27 / 100; }
static inline int pv_cursor_for(struct pv_rect r,const struct pv_deck *d) {
    return r.x+r.w*(d->cursor_center?50:27)/100;
}
static inline struct pv_rect pv_counter_hit(struct pv_layout l) {
    return (struct pv_rect){l.compare.x,l.compare.y,l.compare.w/5,l.compare.h};
}
/* Lower overview edge and timeline set the countdown target. The upper audio
 * area retains native permitted needle search / graphical inspection. */
static inline struct pv_rect pv_mark_hit(struct pv_layout l) {
    int edge=l.overview.h/5;if(edge<8)edge=8;if(edge>14)edge=14;
    int y=l.overview.y+l.overview.h-edge;
    return (struct pv_rect){l.overview.x,y,l.overview.w,l.axis.y+l.axis.h-y};
}
/* Shared by visual inspection and marking. Use wide arithmetic before clamping. */
static inline int pv_timeline_q(struct pv_rect r,int x) {
    long long q=r.w>1?((long long)x-r.x)*65535/(r.w-1):0;
    return q<0?0:q>65535?65535:(int)q;
}
static inline struct pv_rect pv_tool_button(struct pv_layout l) {
    return (struct pv_rect){l.wave.x+l.wave.w-102,l.wave.y+l.wave.h-48,100,46};
}
static inline struct pv_rect pv_return_button(struct pv_layout l) {
    return (struct pv_rect){l.wave.x+l.wave.w-302,l.wave.y+l.wave.h-48,196,46};
}
static inline struct pv_rect pv_back_button(struct pv_layout l) {
    return (struct pv_rect){l.wave.x+6,l.wave.y+l.wave.h-48,180,46};
}
/* The shared modal stays inside the player surface, never over native Beat FX. */
static inline struct pv_rect pv_sheet_rect(int w,int h,int header) {
    return (struct pv_rect){12,header+6,w-24,h-header-18};
}
static inline struct pv_rect pv_sheet_close(struct pv_rect r) {
    return (struct pv_rect){r.x+r.w-104,r.y+6,96,48};
}
static inline struct pv_rect pv_sheet_cell(struct pv_rect r,int row,int col,int columns) {
    int gap=8, w=(r.w-32-(columns-1)*gap)/columns;
    return (struct pv_rect){r.x+16+col*(w+gap),r.y+112+row*68,w,60};
}
static inline int pv_digit_step(unsigned char ch, int cell) {
    return ch=='.' || ch==':' ? (cell+2)/3 : ch=='+' || ch=='-' ? cell*3/4 : cell;
}
static inline struct pv_layout pv_geometry(int w, int h, int count, int index) {
    return pv_geometry_at(w, h, count, index, 70);
}
static inline struct pv_rect pv_pad_tab(struct pv_layout l, int index) {
    return (struct pv_rect) {l.tabs.x + l.tabs.w * index / 3, l.tabs.y, l.tabs.w / 3 - 2, l.tabs.h};
}
static inline int pv_pad_choice(int mode) {
    return mode == 3 || mode == 4 ? 0 : mode == 8            ? 1
                                    : mode == 5 || mode == 6 ? 2
                                                             : -1;
}
enum { PV_CTL_AUTO_CUE = 1, PV_CTL_QUANT_VALUE = 2, PV_CTL_AUTO_HOTCUE = 4, PV_CTL_GRID=8 };
static inline struct pv_rect pv_pad_button(struct pv_layout l, int pad) {
    int x0 = l.pads.x + l.pads.w * pad / 8, x1 = l.pads.x + l.pads.w * (pad + 1) / 8;
    return (struct pv_rect) {x0, l.pads.y, x1 - x0 - 2, l.pads.h};
}
static inline struct pv_rect pv_auto_button(struct pv_layout l, int hot) {
    int x = l.lcd.x + l.lcd.w * 216 / 1000;
    return (struct pv_rect) {x, l.time.y + l.time.h * (hot ? 0 : 1) / 2, l.time.x - x - 6, l.time.h / 3};
}
static inline struct pv_rect pv_auto_hit(struct pv_layout l,int hot) {
    struct pv_rect r=pv_auto_button(l,hot);r.h=l.time.h/2-1;return r;
}
static inline struct pv_rect pv_mt_button(struct pv_layout l) {
    return (struct pv_rect){l.key.x,l.cues.y,l.key.w,l.overview.y-l.cues.y};
}
static inline struct pv_rect pv_quant_button(struct pv_layout l) {
    return (struct pv_rect) {l.player.x, l.cues.y, l.player.w, (l.axis.y+l.axis.h-l.cues.y)/2-2};
}
static inline struct pv_rect pv_jump_button(struct pv_layout l) {
    struct pv_rect q=pv_quant_button(l);
    int y=q.y+q.h+3;
    return (struct pv_rect) {l.player.x, y, l.player.w, l.axis.y+l.axis.h-y};
}
static inline const char *pv_quant_label(int value) {
    static const char *fraction[] = {"--", "1/8", "1/4", "1/2", "1"};
    return value >= 1 && value <= 4 ? fraction[value] : "--";
}
/* UtilitySetting's native beat-length enum, not an application-setting index. */
static inline const char *pv_jump_label(int value) {
    static const char *length[] = {"1/2", "1", "2", "4", "8", "16", "32", "64"};
    return value >= 6 && value <= 13 ? length[value - 6] : "--";
}
static inline const char *pv_pad_label(int mode, int stems, int pad) {
    static const char *loop[] = {"1/4", "1/2", "1", "2", "4", "8", "16", "32"};
    static const char *loop2[] = {"3/4", "1", "3/2", "3", "5", "6", "7", "9"};
    static const char *slip[] = {"1/16", "1/8", "1/4", "1/2", "1", "2", "1/3", "3/4"};
    static const char *jump[] = {"<1/2", "1/2>", "<1", "1>", "<2", "2>", "<4", "4>"};
    static const char *jump2[] = {"<8", "8>", "<16", "16>", "<32", "32>", "<64", "64>"};
    static const char *key[] = {"SYNC", "+1", "+7", "+12", "RESET", "-1", "-5", "-12"};
    static const char *cue[] = {"A", "B", "C", "D", "E", "F", "G", "H"};
    static const char *stem[] = {"VO", "IN", "BA", "DR", "VO SOLO", "IN SOLO", "BA SOLO", "DR SOLO"};
    if (pad < 0 || pad > 7) return "--";
    if (stems) return stem[pad];
    return mode == 3 ? loop[pad] : mode == 4 ? loop2[pad] : mode == 5 ? jump[pad]
        : mode == 6 ? jump2[pad] : mode == 7 ? slip[pad] : mode == 8 ? key[pad]
        : mode == 1 || mode == 2 ? cue[pad] : "--";
}
static inline struct pv_rect pv_fx_button(void) { return (struct pv_rect) {862, 10, 78, 48}; }
static inline int pv_reference(int deck, int master, int left, int right) {
    int ref = master >= 0 && master < 4 && master != deck ? master : (deck == left ? right : left);
    return ref >= 0 && ref < 4 && ref != deck ? ref : (deck ^ 1);
}
/* UI implementation in menu.c; framebuffer and all borrowed images are caller-owned. */
void azmod_player_draw(const azmod_fb *fb, const struct pv_deck decks[4], int count,
                       int focus, int left, int right, unsigned phase_mask, unsigned elapsed_mask);
void azmod_player_tile_draw(const azmod_fb *fb, const struct pv_deck *deck, int index, unsigned elapsed_mask);
#endif
