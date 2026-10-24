/* SPDX-License-Identifier: GPL-3.0-only */
/* 2.00 status bindings. PlayerInfoState is weak-pinned by perdeck_block();
 * each member is checked against its own make_shared control-block RTTI.
 * These are native message-thread getters, not writes to cached flags. */
static uintptr_t pv_status_member(uintptr_t block, int offset, const char *type) {
    uintptr_t object = 0, control = 0;
    if (!block || !own_q(block + offset, &object) || !own_q(block + offset + 8, &control) ||
        !object || object != control + 16 || !pv_class_is(control, type) ||
        __atomic_load_n((const int *) (control + 8), __ATOMIC_ACQUIRE) <= 0) return 0;
    return object;
}
static int (*g_pv_quant_enabled)(uintptr_t) = (void *) 0x2121550;
static int (*g_pv_quant_value)(uintptr_t) = (void *) 0x21215d0;
static uintptr_t (*g_pv_load_info)(uintptr_t) = (void *) 0x21081c8;
static int (*g_pv_auto_cue)(uintptr_t) = (void *) 0x2134828;
static int (*g_pv_auto_hotcue)(uintptr_t) = (void *) 0x21347c8;
static int (*g_pv_jump_value)(uintptr_t) = (void *) 0x21348a8;
static void pv_modes_refresh(void) {
    if (!on_msg_thread() || !g_orig_modeget) return;
    for (int d = 0; d < 4; d++) {
        uintptr_t block = perdeck_block(d), mode = 0;
        if (!block || !own_q(block + 0x10b0, &mode) || !mode) continue;
        unsigned seq = __atomic_load_n(&g_mode_seq, __ATOMIC_ACQUIRE);
        int value = g_orig_modeget((long)mode);
        if (value < 0 || value > 20) continue;
        __atomic_store_n(&g_true_mode_deck[d], value + 1, __ATOMIC_RELEASE);
        __atomic_store_n(&g_tm_seq[d], seq, __ATOMIC_RELEASE);
    }
}
static void pv_status_collect(struct pv_deck *deck, int d) {
    if (g_ep147_build != EPB_V200 || !on_msg_thread()) return;
    uintptr_t block = perdeck_block(d);
    uintptr_t utility = pv_status_member(block, 0x1010,
        "St23_Sp_counted_ptr_inplaceIN3gui14UtilitySettingESaIS1_ELN9__gnu_cxx12_Lock_policyE2EE");
    if (utility) {
        /* UtilitySetting AutoCue notification (2130038) writes +16c;
         * its getter also handles linked players instead of using that cache. */
        deck->auto_cue = !!g_pv_auto_cue(utility);
        int hot = g_pv_auto_hotcue(utility);
        if (hot == 0 || hot == 2) deck->auto_hotcue = hot == 0;
        int jump = g_pv_jump_value(utility);
        if (jump >= 6 && jump <= 13) deck->beat_jump = jump;
    }
    uintptr_t tempo = pv_status_member(block, 0x10e0,
                                       "St23_Sp_counted_ptr_inplaceIN3gui10TempoStateESaIS1_ELN9__gnu_cxx12_Lock_policyE2EE");
    if (tempo) {
        int range = *(const int *) (tempo + 0xa0);
        unsigned mt = *(const unsigned char *) (tempo + 0xa8);
        if (range >= 0 && range < 4) deck->tempo_range = range;
        if (mt < 2) deck->master_tempo = mt;
    }
    uintptr_t quant = pv_status_member(block, 0x10d0,
                                       "St23_Sp_counted_ptr_inplaceIN3gui13QuantizeStateESaIS1_ELN9__gnu_cxx12_Lock_policyE2EE");
    if (quant) {
        deck->quantize = !!g_pv_quant_enabled(quant);
        int value = g_pv_quant_value(quant);
        if (value >= 0 && value <= 20) deck->quantize_value = value;
    }
    uintptr_t load = pv_status_member(block, 0x1050,
                                      "St23_Sp_counted_ptr_inplaceIN3gui9LoadStateESaIS1_ELN9__gnu_cxx12_Lock_policyE2EE");
    if (load && deck->loaded == 1) {
        uintptr_t track = g_pv_load_info(load);
        /* TrackNoWidgetBase::update uses this same native getter and the
         * LoadedTrackInfo slot +0x30 (1f2cc70 reads +0x2c), not an album tag. */
        if (pv_class_is(track, "N3gui15LoadedTrackInfoE")) {
            int number = *(const int *) (track + 0x2c);
            if (number >= 0 && number < 1000000) deck->track_number = number;
        }
    }
}
