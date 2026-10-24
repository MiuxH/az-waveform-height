/* SPDX-License-Identifier: GPL-3.0-only */
/* Input thread: publish presentation state/commands only. No native pointers. */
static int g_pv_preview_touch=-1;
static struct pv_rect g_pv_preview_rect;
static unsigned g_pv_preview_epoch;
static void pv_preview_publish(int x) {
    if (g_pv_preview_touch<0 || g_pv_preview_touch>3 ||
        g_pv_preview_epoch!=__atomic_load_n(&g_pv_gesture_epoch,__ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&g_pv_preview[g_pv_preview_touch],pv_timeline_q(g_pv_preview_rect,x),__ATOMIC_RELEASE);
}
static int g_pv_mark_touch=-1, g_pv_mark_touch_q;
static struct pv_rect g_pv_mark_rect;
static unsigned g_pv_mark_epoch;
static void pv_mark_publish(int x,int released) {
    int deck=g_pv_mark_touch;
    if (deck<0 || deck>3 || g_pv_mark_epoch!=__atomic_load_n(&g_pv_gesture_epoch,__ATOMIC_ACQUIRE)) return;
    g_pv_mark_touch_q=pv_timeline_q(g_pv_mark_rect,x);
    __atomic_store_n(&g_pv_mark_candidate[deck],g_pv_mark_touch_q,__ATOMIC_RELEASE);
    /* Live feedback while dragging; only release commits the persistent point. */
    if (released) {
        __atomic_store_n(&g_pv_mark[deck],g_pv_mark_touch_q,__ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_mark_candidate[deck],-1,__ATOMIC_RELEASE);
    }
}
static void pv_open_sheet(int deck,int kind) {
    if (deck<0 || deck>3 || kind<PV_SHEET_LOOP || kind>PV_SHEET_SLIP) return;
    __atomic_fetch_add(&g_pv_gesture_epoch,1,__ATOMIC_RELEASE);
    for(int d=0;d<4;d++)__atomic_store_n(&g_pv_mark_candidate[d],-1,__ATOMIC_RELEASE);
    __atomic_store_n(&g_pv_sheet_kind,kind,__ATOMIC_RELAXED);
    __atomic_store_n(&g_pv_sheet_deck,deck,__ATOMIC_RELEASE);
    int left=__atomic_load_n(&g_pv_sides[0],__ATOMIC_ACQUIRE),right=__atomic_load_n(&g_pv_sides[1],__ATOMIC_ACQUIRE);
    if (deck!=left && deck!=right) return;
    int mode=pad_mode_fresh(deck%2,deck)-1;
    int tab=kind==PV_SHEET_LOOP?0:kind==PV_SHEET_KEY?1:kind==PV_SHEET_JUMP?2:-1;
    if(kind==PV_SHEET_SLIP && mode!=7)__atomic_store_n(&g_pv_mode_req[deck],6,__ATOMIC_RELEASE);
    if (tab>=0 && pv_pad_choice(mode)!=tab)
        __atomic_store_n(&g_pv_mode_req[deck],tab+1,__ATOMIC_RELEASE);
}
static int pv_sheet_touch(int x,int y) {
    int deck=__atomic_load_n(&g_pv_sheet_deck,__ATOMIC_ACQUIRE);
    if (deck<0 || deck>3) return 0;
    int kind=__atomic_load_n(&g_pv_sheet_kind,__ATOMIC_ACQUIRE);
    if (kind<PV_SHEET_LOOP || kind>PV_SHEET_SLIP) {
        __atomic_store_n(&g_pv_sheet_deck,-1,__ATOMIC_RELEASE);return 1;
    }
    int left=__atomic_load_n(&g_pv_sides[0],__ATOMIC_ACQUIRE),right=__atomic_load_n(&g_pv_sides[1],__ATOMIC_ACQUIRE);
    int enabled=deck==left || deck==right;
    struct pv_rect r=pv_sheet_rect(__atomic_load_n(&g_pv_draw_width,__ATOMIC_ACQUIRE),800,az_value(AZF_OVERLAYS)?96:70);
    if (pv_inside(pv_sheet_close(r),x,y)) { __atomic_store_n(&g_pv_sheet_deck,-1,__ATOMIC_RELEASE);return 1; }
    if (kind==PV_SHEET_INFO) return 1;
    int mode=pad_mode_fresh(deck%2,deck)-1;
    int display=mode;
    if (az_on(AZF_BEAT_JUMP_2_FIRST) && (mode==5 || mode==6))display=11-mode;
    if (kind!=PV_SHEET_TOOLS) {
        int ready=kind==PV_SHEET_LOOP?(mode==3 || mode==4):kind==PV_SHEET_KEY?mode==8:
            kind==PV_SHEET_SLIP?mode==7:(mode==5 || mode==6);
        if (enabled && __atomic_load_n(&g_pv_loaded[deck],__ATOMIC_ACQUIRE)==1 && ready && !(mode==7 && __atomic_load_n(&g_drum_arm,__ATOMIC_ACQUIRE)) &&
            !(__atomic_load_n(&g_erp_active[deck],__ATOMIC_ACQUIRE) && stems_page(deck%2,deck))) {
            for (int pad=0;pad<8;pad++) if (pv_inside(pv_sheet_cell(r,pad/4,pad%4,4),x,y)) {
                int native_pad=kind==PV_SHEET_KEY?(int[]){5,1,4,0,-1,-1,-1,-1}[pad]:pad;
                if (native_pad>=0) {
                    g_pv_touch_deck=deck;
                    __atomic_store_n(&g_pv_pad_down[deck],__atomic_load_n(&g_pv_contact_down,__ATOMIC_ACQUIRE)!=0,__ATOMIC_RELEASE);
                    __atomic_store_n(&g_pv_pad_req[deck],(unsigned)(mode<<8)|(native_pad+1u),__ATOMIC_RELEASE);
                }
            }
            if (kind==PV_SHEET_LOOP || kind==PV_SHEET_JUMP) {
                int page=display==4 || display==6;
                for (int p=0;p<2;p++) if (p!=page && pv_inside(pv_sheet_cell(r,2,p,4),x,y))
                    __atomic_store_n(&g_pv_mode_req[deck],mode==3 || mode==4?4:5,__ATOMIC_RELEASE);
                if (pv_inside(pv_sheet_cell(r,2,2,4),x,y))
                    __atomic_store_n(&g_pv_button_req[deck],PV_ACTION_RELOOP,__ATOMIC_RELEASE);
            }
        }
        if(kind==PV_SHEET_LOOP || kind==PV_SHEET_JUMP) {
            if(enabled && pv_inside(pv_sheet_cell(r,3,0,4),x,y))__atomic_store_n(&g_pv_button_req[deck],PV_ACTION_LOOP4,__ATOMIC_RELEASE);
            if(enabled && pv_inside(pv_sheet_cell(r,3,1,4),x,y))__atomic_store_n(&g_pv_button_req[deck],PV_ACTION_LOOP8,__ATOMIC_RELEASE);
            
            if(pv_inside(pv_sheet_cell(r,3,3,4),x,y))__atomic_store_n(&g_pv_sheet_deck,-1,__ATOMIC_RELEASE);
        } else {
            
            if (pv_inside(pv_sheet_cell(r,3,1,2),x,y)) __atomic_store_n(&g_pv_sheet_deck,-1,__ATOMIC_RELEASE);
        }
        return 1;
    }
    return 1;
}
