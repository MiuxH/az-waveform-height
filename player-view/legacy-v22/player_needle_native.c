/* SPDX-License-Identifier: GPL-3.0-only */
/* 2.00 NeedleSearchWidget::onNeedleDrag(primary, held, localX), 1d78cc8.
 * The same native path handles playback-lock and seek/release notifications.
 * X input publishes coordinates only; native objects stay on the message thread. */
static _Atomic int g_pv_needle_deck = -1, g_pv_needle_position, g_pv_needle_down, g_pv_needle_pending;
static int g_pv_needle_active = -1;
static uintptr_t g_pv_needle_identity;
static int g_pv_needle_slot = -1;
static struct pv_rect g_pv_needle_gesture;
static int g_pv_needle_touch = -1;
static _Atomic int g_pv_needle_count, g_pv_needle_cancelled;
static _Atomic unsigned g_pv_needle_contact_epoch;
static void (*g_pv_needle_apply)(uintptr_t,int,int) = (void *)0x1d78cc8;
static uintptr_t pv_needle_widget(int deck, int *slot) {
    int n=__atomic_load_n(&g_ovtile_n,__ATOMIC_RELAXED);
    for (int i=0;i<n && i<24;i++) {
        uintptr_t rgb=wref_get(&g_ovtile_w[i]), vt=0, parent=0;
        if (g_ovtile[i].deck != deck || !rgb || !own_q(rgb,&vt) || vt != OV_WIDGET_VT ||
            !own_q(rgb+0x170+0x18,&parent) ||
            !pv_class_is(parent,"N3gui20player_info_waveform14WaveformWidgetE")) continue;
        uintptr_t comp=0;
        /* NeedleSearch is a sibling of the overview family in the native
         * player footer. Ascend only within this deck's small footer; never
         * search the whole view and accidentally pick another player's strip. */
        for (int level=0;parent && level<4;level++) {
            int rr[4];
            if (!ht_screen_rect(parent,rr) || rr[3] > 256 || rr[3] < 1) break;
            comp=pv_child_class(parent,"N3gui20player_info_waveform18NeedleSearchWidgetE",3);
            if (comp) break;
            uintptr_t next=0;
            if (!own_q(parent+0x18,&next) || !ht_ptr_ok(next) || next==parent) break;
            parent=next;
        }
        if (comp < 0x100a0 || !own_q(comp-0xa0,&vt) || vt != 0x2f15b38) continue;
        int rect[4];
        if (!ht_screen_rect(comp,rect) || rect[2] < 1 || rect[2] > 1280 || rect[3] < 1) continue;
        *slot=i;
        return comp-0xa0;
    }
    return 0;
}
static void pv_needle_tick(int enabled) {
    if (!on_msg_thread() || g_ep147_build != EPB_V200) return;
    int pending=__atomic_exchange_n(&g_pv_needle_pending,0,__ATOMIC_ACQ_REL);
    int deck=__atomic_load_n(&g_pv_needle_deck,__ATOMIC_ACQUIRE);
    int down=enabled && __atomic_load_n(&g_pv_needle_down,__ATOMIC_ACQUIRE) &&
        __atomic_load_n(&g_pv_contact_down,__ATOMIC_ACQUIRE)!=0;
    unsigned epoch=__atomic_load_n(&g_pv_contact_epoch,__ATOMIC_ACQUIRE);
    unsigned began=__atomic_load_n(&g_pv_needle_contact_epoch,__ATOMIC_ACQUIRE);
    if(epoch!=began && !(pending && !g_pv_contact_down && epoch==began+1)) {
        down=pending=0;
        __atomic_store_n(&g_pv_needle_cancelled,1,__ATOMIC_RELEASE);
    }
    if (__atomic_load_n(&g_pv_contact_cancel,__ATOMIC_ACQUIRE))down=pending=0;
    int count=pv_active_count();
    if (down && (count != __atomic_load_n(&g_pv_needle_count,__ATOMIC_ACQUIRE) ||
        (count==1 && deck != pv_active_focus()) ||
        (count==2 && deck != __atomic_load_n(&g_pv_sides[0],__ATOMIC_RELAXED) &&
                     deck != __atomic_load_n(&g_pv_sides[1],__ATOMIC_RELAXED)))) {
        __atomic_store_n(&g_pv_needle_cancelled,1,__ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_needle_down,0,__ATOMIC_RELEASE);
        down=pending=0;
    }
    if (g_pv_needle_active >= 0 && (!down || deck != g_pv_needle_active)) {
        int slot=-1;
        uintptr_t p=pv_needle_widget(g_pv_needle_active,&slot);
        /* Never release a newly constructed widget on behalf of a dead one. */
        if (p && p == g_pv_needle_identity && slot == g_pv_needle_slot)
            g_pv_needle_apply(p,0,*(const int *)(p+0x98));
        g_pv_needle_active=-1; g_pv_needle_identity=0;
    }
    if (!enabled || deck < 0 || deck > 3 || (!down && !pending)) return;
    int slot=-1;
    uintptr_t p=pv_needle_widget(deck,&slot);
    if (g_pv_needle_active>=0 && (p!=g_pv_needle_identity || slot!=g_pv_needle_slot)) {
        g_pv_needle_active=-1; g_pv_needle_identity=0;
        __atomic_store_n(&g_pv_needle_cancelled,1,__ATOMIC_RELEASE);
        __atomic_store_n(&g_pv_needle_down,0,__ATOMIC_RELEASE);
        return;
    }
    if (!p || !*(const unsigned char *)(p+0x9d) ||
        (*(const unsigned char *)(p+0xa0+0xc1)&0x10)) {
        if (p && g_pv_needle_active==deck && p==g_pv_needle_identity) {
            g_pv_needle_apply(p,0,*(const int *)(p+0x98));
            g_pv_needle_active=-1; g_pv_needle_identity=0;
            __atomic_store_n(&g_pv_needle_cancelled,1,__ATOMIC_RELEASE);
        }
        return;
    }
    int width=*(const int *)(p+0x40), margin=*(const int *)(p+0x48);
    int q=__atomic_load_n(&g_pv_needle_position,__ATOMIC_ACQUIRE);
    if (width < 1 || width > 1280 || margin < 0 || margin > 1280 || q < 0 || q > 65535) return;
    int x=margin+(int)((long)q*width/65535);
    g_pv_needle_apply(p,1,x);
    if (down) { g_pv_needle_active=deck; g_pv_needle_identity=p; g_pv_needle_slot=slot; }
    else g_pv_needle_apply(p,0,x); /* quick press/release between timer ticks */
}
static void pv_needle_publish(int x, int down) {
    int q=g_pv_needle_gesture.w > 1 ? (int)((long)(x-g_pv_needle_gesture.x)*65535/(g_pv_needle_gesture.w-1)) : 0;
    if (q < 0) q=0;
    if (q > 65535) q=65535;
    __atomic_store_n(&g_pv_needle_position,q,__ATOMIC_RELAXED);
    __atomic_store_n(&g_pv_needle_deck,g_pv_needle_touch,__ATOMIC_RELAXED);
    __atomic_store_n(&g_pv_needle_down,down && !__atomic_load_n(&g_pv_needle_cancelled,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
}
