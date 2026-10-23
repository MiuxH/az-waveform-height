/* SPDX-License-Identifier: GPL-3.0-only */
/* Pure four-deck table model. Height, then four top edges in native row order.
 * Only the caller accesses EP147, on the message thread through a live wref. */
struct four_wave_state {
    uintptr_t view;
    int stock[5], last[5];
};

static int four_wave_valid(const int t[5]) {
    if (t[0] < 24 || t[0] > 240 || t[1] < 0 || t[4] + t[0] > 800)
        return 0;
    for (int i = 2; i < 5; i++)
        if (t[i] < t[i - 1] + t[0] + 2)
            return 0;
    return 1;
}

static int four_wave_update(struct four_wave_state *s, uintptr_t view, const int live[5],
                            int mode, int enabled, int level, int out[5]) {
    if (!view || mode < 0 || mode > 2 || !four_wave_valid(live))
        return 0;
    /* A new view or EP147 supplying a new table establishes a fresh baseline.
     * Never dereference the old view or cache component pointers. */
    if (s->view != view || memcmp(live, s->last, sizeof s->last)) {
        s->view = view;
        memcpy(s->stock, live, sizeof s->stock);
    }
    memcpy(out, s->stock, sizeof s->stock);
    if (enabled && mode == 2 && level > 0) {
        if (level > 10)
            level = 10;
        int span = s->stock[4] + s->stock[0] - s->stock[1];
        int max_h = (span - 6) / 4;
        out[0] += (max_h - out[0]) * level / 10;
        int spare = span - 4 * out[0];
        for (int i = 0; i < 4; i++)
            out[i + 1] = s->stock[1] + i * out[0] + spare * i / 3;
    }
    memcpy(s->last, out, sizeof s->last);
    return memcmp(live, out, sizeof s->last) != 0;
}

/* Native WaveformWidgetBase uses box height minus padding (+0x50) as the
 * total drawing height (+0x5c). The two rulers (+0x58 each) surround the body.
 * Keep their native height: makeGridImage's extended ticks use row h-6.
 * The colour Components start out 162 pixels high even in 4 DECK. Fit their
 * bounds to the new drawing area; EP147 owns and rebuilds the raster images. */
#define FOUR_WAVE_GRID_MIN 7
enum {
    FWI_H,
    FWI_PAD,
    FWI_GRID_H,
    FWI_Y0,
    FWI_Y1,
    FWI_Y2,
    FWI_CH0,
    FWI_CH1,
    FWI_CH2,
    FWI_N
};
struct four_wave_inner_state {
    uintptr_t owner;
    int stock[FWI_N], last[FWI_N];
};

static int four_wave_inner_update(struct four_wave_inner_state *s, uintptr_t owner,
                                  const int live[FWI_N], int mode, int enabled,
                                  int level, int field_h, int out[FWI_N]) {
    memcpy(out, live, sizeof s->last);
    if (!owner || (s->owner && s->owner != owner))
        memset(s, 0, sizeof *s);
    if (!owner || mode < 0 || mode > 2)
        return 0;
    int want = mode == 2 && enabled && level > 0;
    if (!want) {
        if (!s->owner)
            return 0;
        for (int i = 0; i < FWI_N; i++)
            if (live[i] == s->last[i] && (i != FWI_H || mode == 2))
                out[i] = s->stock[i];
        /* On a mode switch EP147 has supplied the current two-deck height.
         * Never replace it with a cached four-deck height. The caller invokes
         * the native height setter again if padding changed. */
        memset(s, 0, sizeof *s);
        return memcmp(live, out, sizeof s->last) != 0;
    }
    if (live[FWI_H] < 48 || live[FWI_H] > 200 || field_h < 48 || field_h > 160 ||
        live[FWI_PAD] < 0 || live[FWI_PAD] > 32 ||
        live[FWI_GRID_H] < FOUR_WAVE_GRID_MIN || live[FWI_GRID_H] > 32)
        return 0;
    for (int i = FWI_Y0; i <= FWI_Y2; i++)
        if (live[i] < 0 || live[i] > 32)
            return 0;
    for (int i = FWI_CH0; i <= FWI_CH2; i++)
        if (live[i] < 48 || live[i] > 200)
            return 0;
    if (!s->owner) {
        s->owner = owner;
        memcpy(s->stock, live, sizeof s->stock);
    }
    if (level > 10)
        level = 10;
    out[FWI_H] = s->stock[FWI_H] + (field_h - s->stock[FWI_H]) * level / 10;
    out[FWI_PAD] = s->stock[FWI_PAD] * (10 - level) / 10;
    out[FWI_GRID_H] = s->stock[FWI_GRID_H];
    int total_h = out[FWI_H] - out[FWI_PAD];
    if (total_h <= 2 * out[FWI_GRID_H])
        return 0;
    for (int m = 0; m < 3; m++) {
        out[FWI_Y0 + m] = s->stock[FWI_Y0 + m] * (10 - level) / 10;
        /* Native stock y=8 and padding=16 centre the 76-pixel drawing in
         * its 92-pixel box. Keep that relationship while both shrink. */
        if (out[FWI_Y0 + m] + total_h > out[FWI_H])
            return 0;
        out[FWI_CH0 + m] = total_h;
    }
    memcpy(s->last, out, sizeof s->last);
    return memcmp(live, out, sizeof s->last) != 0;
}
