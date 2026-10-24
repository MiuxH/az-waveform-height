/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AZMOD_PLAYER_INPUT_H
#define AZMOD_PLAYER_INPUT_H
#include "player_actions.h"
/* Pure physical pad-mode pulse. EP147 switches on release. Key Shift is the
 * SLIP LOOP button's second page: confirm native mode 7 before a second pulse.
 * Requests are consumed only for the logical deck currently on that side. */
struct pv_mode_pulse {
    int deck, target, phase, attempts;
    long started, at;
};
static int pv_mode_matches(int target, int mode) {
    return target == 1 ? mode == 3 || mode == 4 : target == 2 ? mode == 8
                                              : target == 3   ? mode == 5 || mode == 6
                                              : target == 6   ? mode == 7
                                                              : 0;
}
static unsigned pv_mode_step(struct pv_mode_pulse *s, int enabled, int deck, int mode,
                             int request, unsigned physical, int shift, long now) {
    if (!enabled || physical || shift || (s->phase && s->deck != deck)) {
        *s = (struct pv_mode_pulse) {0};
        return 0;
    }
    if (!s->phase) {
        if (request < 1 || request > 6 || ((request <= 3 || request==6) && pv_mode_matches(request, mode))) return 0;
        *s = (struct pv_mode_pulse) {deck, request, 1, request == 2 && mode == 7 ? 2 : 1, now, now};
    }
    if (now - s->started > 1500) {
        *s = (struct pv_mode_pulse) {0};
        return 0;
    }
    unsigned bit = s->target == 1 || s->target == 4 ? 2u : s->target == 2 || s->target==6 ? 4u
                                                        : 8u;
    if (s->phase == 1) {
        if (now - s->at < 60) return bit;
        s->phase = 2;
        s->at = now;
    } else if (s->phase == 2 && now - s->at >= 80) {
        s->phase = 3;
        s->at = now;
    }
    if (s->phase == 3) {
        if ((s->target >= 4 && s->target<=5) || pv_mode_matches(s->target, mode)) *s = (struct pv_mode_pulse) {0};
        else if (s->target == 2 && mode == 7 && s->attempts < 2 && now - s->at >= 100) {
            s->phase = 1;
            s->at = now;
            s->attempts++;
            return bit;
        }
    }
    return 0;
}
/* One touch gesture is one physical pad gesture. Even a fast click is visible
 * for at least 60 ms; a held touch follows its release. Deck/mode changes,
 * physical input and stale UI cancel it. */
struct pv_pad_hold { int deck, mode, pad, active; long started; unsigned queued;long queued_at; };
static unsigned pv_pad_step(struct pv_pad_hold *s, int enabled, int deck, int mode,
                            unsigned request, int down, int physical, long now) {
    if (!enabled || physical || ((s->active || s->queued) && (deck != s->deck || mode != s->mode))) {
        *s = (struct pv_pad_hold){0}; return 0;
    }
    if (s->queued && now-s->queued_at>500)s->queued=0;
    if(s->active && request) {
        if(!s->queued) {s->queued=request;s->queued_at=now;}
    }
    if(s->active && s->queued && now-s->started>=60) {s->active=0;return 0;}
    if(!s->active && s->queued) {request=s->queued;s->queued=0;}
    if (!s->active && request) {
        int pad = (request & 255u) - 1, expected = (request >> 8) & 255u;
        if (pad < 0 || pad > 7 || expected != mode || mode < 1 || mode > 8) return 0;
        *s = (struct pv_pad_hold){.deck=deck,.mode=mode,.pad=pad,.active=1,.started=now};
    }
    if (!s->active) return 0;
    if (!down && now - s->started >= 60) {
        *s = (struct pv_pad_hold){0}; return 0;
    }
    return 1u << s->pad;
}
struct pv_panel_pulse { int deck, action, active; long started; };
static unsigned pv_panel_step(struct pv_panel_pulse *s, int enabled, int deck,
                              int request, int physical, long now) {
    if (!enabled || physical || (s->active && deck != s->deck)) {
        *s = (struct pv_panel_pulse){0}; return 0;
    }
    if (!s->active && request >= 1 && request <= 2)
        *s = (struct pv_panel_pulse){deck, request, 1, now};
    if (!s->active) return 0;
    if (now - s->started >= 60) { *s = (struct pv_panel_pulse){0}; return 0; }
    /* SHIFT + BEAT JUMP back/forward: change length, don't jump playback. */
    return 0x80u | (s->action == 1 ? 0x40u : 0x20u);
}
#endif
