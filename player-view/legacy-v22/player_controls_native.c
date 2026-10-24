/* SPDX-License-Identifier: GPL-3.0-only */
/* Native UI commands, message thread only. Look up live components from the
 * tracked waveform's root for every operation; no borrowed object is retained.
 * Hidden shortcut children are valid owners of their settings controllers. */
static _Atomic unsigned g_pv_controls_req, g_pv_controls_caps;
static _Atomic int g_pv_controls_refresh=1;
static _Atomic unsigned long g_pv_control_scans;
static void (*g_pv_setting_click)(uintptr_t, const uintptr_t *) = (void *) 0x1dea588;
static void (*g_pv_quant_set)(uintptr_t, const int *, uintptr_t) = (void *) 0xb5c170;
static uintptr_t pv_find_component(uintptr_t parent, const char *name, int depth, int *budget) {
    uintptr_t queue[1024];
    unsigned char level[1024];
    int head = 0, tail = 1;
    queue[0] = parent; level[0] = 0;
    /* Breadth first: a long browser row subtree must not hide a sibling
     * Shortcut view behind the traversal budget. No pointer leaves this call. */
    while (head < tail && *budget > 0) {
        uintptr_t comp = queue[head];
        int at = level[head++];
        --*budget;
        if (!ht_ptr_ok(comp)) continue;
        if (pv_class_is(comp, name)) return comp;
        if (at >= depth) continue;
        uintptr_t children = 0;
        if (!own_q(comp + 0x40, &children) || !children) continue;
        int n = *(const int *)(comp + 0x50);
        if (n < 1 || n > 128) continue;
        for (int i = 0; i < n && tail < 1024; i++) {
            uintptr_t child = 0, owner = 0;
            if (!own_q(children + 8*i, &child) || !ht_ptr_ok(child) ||
                !own_q(child + 0x18, &owner) || owner != comp) continue;
            queue[tail] = child; level[tail++] = at + 1;
        }
    }
    return 0;
}
static uintptr_t pv_live_listener(uintptr_t button, const char *type, uintptr_t widget, int backref) {
    uintptr_t listeners = 0;
    if (!button || !own_q(button + 0x208, &listeners)) return 0;
    int n = *(const int *)(button + 0x218);
    if (!listeners || n < 1 || n > 16) return 0;
    for (int i = 0; i < n; i++) {
        uintptr_t listener = 0, owner = 0;
        if (own_q(listeners + 8*i, &listener) &&
            pv_class_is(listener, type) && own_q(listener + backref, &owner) && owner == widget)
            return listener;
    }
    return 0;
}
static uintptr_t pv_quant_setting(uintptr_t widget) {
    uintptr_t button = 0, flags = 0;
    if (!widget || !own_q(widget + 0xc8, &button) ||
        !own_q(button + 0xc0, &flags) || (flags & 0x1000u)) return 0;
    uintptr_t listener = pv_live_listener(button,
        "N3gui8shortcut33QuantizeBeatValueWidgetControllerE", widget, 0x38);
    uintptr_t setting = 0, control = 0;
    if (!listener || !own_q(listener + 0x28, &setting) || !own_q(listener + 0x30, &control) ||
        !control || __atomic_load_n((const int *)(control + 8), __ATOMIC_ACQUIRE) <= 0 ||
        !pv_class_is(setting, "N19application_setting24QuantizeBeatValueSettingE")) return 0;
    return setting;
}
static uintptr_t pv_header_button(uintptr_t widget, int hot, uintptr_t *component) {
    uintptr_t button = 0, flags = 0;
    if (!widget || !own_q(widget + (hot ? 0xd8 : 0xe0), &button) ||
        !button || !own_q(button + 0xc0, &flags) || (flags & 0x1000u) ||
        !own_q(button + 0x1d8, component) || !*component) return 0;
    return pv_live_listener(button,
        "N3gui11status_area8waveform37WaveformSettingButtonWidgetControllerE", widget, 8);
}
static void pv_controls_tick(uintptr_t view) {
    static long checked;
    static uintptr_t checked_view;
    if (g_ep147_build != EPB_V200 || !on_msg_thread()) return;
    unsigned request = __atomic_exchange_n(&g_pv_controls_req, 0, __ATOMIC_ACQ_REL);
    long now = now_ms();
    int refresh=__atomic_exchange_n(&g_pv_controls_refresh,0,__ATOMIC_ACQ_REL);
    /* Capabilities, not component pointers, can be retained while this view is
     * active. Resolve owners afresh for each command. Avoid two 1024-node tree
     * walks every second. A screen return or explicit command discovers newly
     * constructed owners; an unavailable hidden Shortcut is not polled. */
    if(!request && !refresh && checked_view==view && checked)return;
    checked=now;checked_view=view;__atomic_fetch_add(&g_pv_control_scans,1,__ATOMIC_RELAXED);
    uintptr_t root = view;
    for (int i = 0; i < 12; i++) {
        uintptr_t parent = 0;
        if (!own_q(root + 0x18, &parent) || !ht_ptr_ok(parent) || parent == root) break;
        root = parent;
    }
    int budget = 1024;
    uintptr_t header = pv_find_component(root,
        "N3gui11status_area8waveform27WaveformSettingButtonWidgetE", 12, &budget);
    uintptr_t component = 0;
    uintptr_t auto_listener = pv_header_button(header, 0, &component);
    unsigned caps = auto_listener ? PV_CTL_AUTO_CUE : 0;
    if (auto_listener && (request & PV_CTL_AUTO_CUE)) g_pv_setting_click(auto_listener, &component);
    uintptr_t hot_listener = pv_header_button(header, 1, &component);
    if (hot_listener) caps |= PV_CTL_AUTO_HOTCUE;
    if (hot_listener && (request & PV_CTL_AUTO_HOTCUE)) g_pv_setting_click(hot_listener, &component);
    budget = 1024;
    uintptr_t widget = pv_find_component(root, "N3gui8shortcut23QuantizeBeatValueWidgetE", 12, &budget);
    uintptr_t setting = pv_quant_setting(widget);
    if (setting) caps |= PV_CTL_QUANT_VALUE;
    if (setting && (request & PV_CTL_QUANT_VALUE)) {
        int next = -1;
        /* The native Shortcut sets one shared application setting. Read a
         * verified local deck before cycling; linked/unknown values don't wrap. */
        for (int d = 0; d < 4 && next < 0; d++) {
            uintptr_t quant = pv_status_member(perdeck_block(d), 0x10d0,
                "St23_Sp_counted_ptr_inplaceIN3gui13QuantizeStateESaIS1_ELN9__gnu_cxx12_Lock_policyE2EE");
            if (quant) {
                int value = g_pv_quant_value(quant);
                if (value >= 1 && value <= 4) next = value % 4;
            }
        }
        if (next >= 0) g_pv_quant_set(setting, &next, 0);
    }
    __atomic_store_n(&g_pv_controls_caps, caps, __ATOMIC_RELEASE);
}
