/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AZMOD_PLAYER_CUES_H
#define AZMOD_PLAYER_CUES_H
#include <stdint.h>
/* Native AZ RGB palette: PWV5 R=13..15, G=10..12, B=7..9.
 * Each 3-bit value becomes a channel in steps of 32 (maximum 224).
 * Verified against the captured EP147 drawer's BGR cells, not a photo tint. */
static inline uint32_t pv_rgb_color(unsigned bits) {
    return (((bits >> 13) & 7u) << 21) |
           (((bits >> 10) & 7u) << 13) |
           (((bits >> 7) & 7u) << 5);
}
struct pv_cue {
    double seconds, loop_end;
    uint32_t color;
    int present;
};
struct pv_countdown {
    double times[64], target_beat, next_cue;
    int first, n;
};
struct pv_peak { unsigned short level[3]; unsigned color; };
#define PV_SUMMARY_N 2048
#define PV_MEMORY_N 64
struct pv_beats { unsigned refs,count,first; double times[]; };
/* Immutable exported detail and its display-only overview cache. */
struct pv_analysis {
    unsigned refs, count, kind, summary_n;
    struct pv_peak summary[PV_SUMMARY_N];
    unsigned char samples[];
};
struct pv_library_data {
    unsigned content_id;
    char title[128], artist[128], key[32];
    double duration;
    int auto_hotcue;
    struct pv_cue cues[8];
    struct pv_countdown countdown;
    unsigned char overview[2048 * 12];
    int overview_n;
    struct pv_analysis *wave;
    struct pv_analysis *wave3;
    struct pv_analysis *wave_blue;
    struct pv_beats *beats;
    struct pv_cue memory[PV_MEMORY_N];
    int memory_n,memory_format;
};
#endif
