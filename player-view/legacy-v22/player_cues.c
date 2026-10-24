/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded reader for exported analysis cues. Format reference:
 * Deep-Symmetry/crate-digger src/main/kaitai/rekordbox_anlz.ksy.
 * This independent reader accepts PCOB/PCPT and PCO2/PCP2, never audio data. */
#include "player_cues.h"
#include <stddef.h>
#include <string.h>
static uint32_t pv_be32(const unsigned char *p) {
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
/* PWV5: 150 actual analysis samples per second, big-endian RGB/height bits.
 * Colour ordering/palette: verified against the native AZ drawer; see pv_rgb_color.
 * Reduce into 2048 peak-preserving bins; never enlarge a 368px cached bitmap. */
static int pv_overview_parse(const unsigned char *p, size_t n, struct pv_library_data *out) {
    if (n < 12 || memcmp(p,"PMAI",4)) return 0;
    size_t pos=pv_be32(p+4), end=pv_be32(p+8);
    if (pos < 12 || pos > n || end != n) return 0;
    const unsigned char *samples=NULL;
    unsigned count=0;
    while (pos < end) {
        if (end-pos < 12) return 0;
        const unsigned char *t=p+pos;
        size_t h=pv_be32(t+4), len=pv_be32(t+8);
        if (h < 12 || len < h || len > end-pos) return 0;
        if (!memcmp(t,"PWV5",4)) {
            if (h != 24 || len < 24 || pv_be32(t+12) != 2) return 0;
            count=pv_be32(t+16);
            if (!count || count > 12960000 || count != (len-h)/2 || (len-h)%2) return 0;
            samples=t+h;
        }
        pos+=len;
    }
    if (!samples) return 0;
    int bins=count < 2048 ? (int)count : 2048;
    for (int x=0;x<bins;x++) {
        unsigned lo=(unsigned)((uint64_t)x*count/bins), hi=(unsigned)((uint64_t)(x+1)*count/bins);
        unsigned peak=0, bits=0;
        for (unsigned i=lo;i<hi;i++) {
            unsigned b=(unsigned)samples[2*i]<<8|samples[2*i+1], h=(b>>2)&31;
            if (h >= peak) { peak=h; bits=b; }
        }
        unsigned char *v=out->overview+x*12;
        memset(v,0,12);
        uint32_t rgb=pv_rgb_color(bits); /* RGB bytes, unlike native BGR cells */
        v[0]=(unsigned char)(rgb>>16);
        v[1]=(unsigned char)(rgb>>8);
        v[2]=(unsigned char)rgb;
        v[4]=(unsigned char)(peak*255/31); /* fixed full-scale, not per-track gain */
        memcpy(v+6,v,6);
        v[10]=0; /* one exported envelope, no invented second frequency layer */
    }
    out->overview_n=bins;
    return 1;
}
static int pv_cue_parse(const unsigned char *p, size_t n, struct pv_cue cues[8]) {
    if (n < 12 || memcmp(p, "PMAI", 4)) return -1;
    size_t pos = pv_be32(p + 4), end = pv_be32(p + 8);
    if (pos < 12 || end != n || pos > end) return -1;
    struct pv_cue next[8];
    memcpy(next, cues, sizeof next);
    int found = 0;
    while (pos < end) {
        if (end - pos < 12) return -1;
        const unsigned char *t = p + pos;
        size_t h = pv_be32(t + 4), len = pv_be32(t + 8);
        if (h < 12 || len < h || len > end - pos) return -1;
        int ext = !memcmp(t, "PCO2", 4), old = !memcmp(t, "PCOB", 4);
        if (ext || old) {
            size_t start = ext ? 20 : 24;
            if (h != start || len < start) return -1;
            unsigned count = (unsigned) t[ext ? 16 : 18] << 8 | t[ext ? 17 : 19];
            if (count > 1024) return -1;
            size_t q = start;
            for (unsigned i = 0; i < count; i++) {
                if (len - q < 12) return -1;
                const unsigned char *e = t + q;
                size_t eh = pv_be32(e + 4), el = pv_be32(e + 8);
                if (memcmp(e, ext ? "PCP2" : "PCPT", 4) || eh < 12 || eh > el ||
                    el < (ext ? 40u : 56u) || el > len - q) return -1;
                unsigned slot = pv_be32(e + 12);
                unsigned type = e[ext ? 16 : 28];
                uint32_t at = pv_be32(e + (ext ? 20 : 32));
                uint32_t to = pv_be32(e + (ext ? 24 : 36));
                uint32_t color = type == 2 ? 0xff9700 : 0x43ff50;
                if (ext && el > 43) {
                    size_t comment = pv_be32(e + 40);
                    if (comment > el - 44 || (comment & 1)) return -1;
                    if (el - comment >= 48) {
                        const unsigned char *c = e + 44 + comment;
                        uint32_t rgb = (uint32_t) c[1] << 16 | (uint32_t) c[2] << 8 | c[3];
                        if (rgb) color = rgb;
                    }
                }
                /* Hot-cue identity is its explicit number, never list order.
                 * An extended record replaces its older colourless form. */
                if (pv_be32(t + 12) == 1 && slot >= 1 && slot <= 8 &&
                    (type == 1 || type == 2) && at < 86400000u &&
                    (type != 2 || (to > at && to < 86400000u)) &&
                    (ext || !next[slot - 1].present)) {
                    next[slot - 1] = (struct pv_cue) {at / 1000.0, type == 2 ? to / 1000.0 : -1, color, 1};
                    found++;
                }
                q += el;
            }
            if (q != len) return -1;
        }
        pos += len;
    }
    memcpy(cues, next, sizeof next);
    return found;
}
/* Exported memory-cue countdown with the actual (possibly variable-tempo)
 * beat timestamps. Keep only a bounded window near this deck's playhead. */
static int pv_countdown_parse(const unsigned char *p, size_t n, double elapsed, struct pv_countdown *out) {
    struct pv_cue check[8] = {{0}};
    if (elapsed < 0 || elapsed >= 86400 || pv_cue_parse(p, n, check) < 0) return 0;
    struct pv_countdown next = *out;
    size_t start = pv_be32(p + 4);
    const unsigned char *grid = NULL;
    unsigned ng = 0;
    for (size_t pos = start; pos < n; pos += pv_be32(p + pos + 8)) {
        const unsigned char *t = p + pos;
        size_t len = pv_be32(t + 8), h = pv_be32(t + 4);
        int ext = !memcmp(t, "PCO2", 4), old = !memcmp(t, "PCOB", 4);
        if ((ext || old) && pv_be32(t + 12) == 0) {
            unsigned count = (unsigned) t[ext ? 16 : 18] << 8 | t[ext ? 17 : 19];
            size_t q = h;
            for (unsigned i = 0; i < count; i++) {
                const unsigned char *e = t + q;
                unsigned type = e[ext ? 16 : 28];
                double seconds = pv_be32(e + (ext ? 20 : 32)) / 1000.0;
                if (!pv_be32(e + 12) && (type == 1 || type == 2) && seconds > elapsed + .005 &&
                    seconds < 86400 && (next.next_cue <= elapsed || seconds < next.next_cue)) {
                    next.next_cue = seconds;
                    next.n = 0;
                }
                q += pv_be32(e + 8);
            }
        } else if (!memcmp(t, "PQTZ", 4)) {
            if (h != 24 || len < 24) return 0;
            ng = pv_be32(t + 20);
            if (ng < 2 || ng > (len - 24) / 8) return 0;
            grid = t + 24;
        }
    }
    if (!grid || next.next_cue <= elapsed) {
        *out = next;
        return 1;
    }
    unsigned previous = 0, previous_beat = 0;
    int at = -1, target = -1;
    for (unsigned i = 0; i < ng; i++) {
        const unsigned char *b = grid + 8 * i;
        unsigned beat = (unsigned) b[0] << 8 | b[1], ms = pv_be32(b + 4);
        if (beat < 1 || beat > 4 || ms >= 86400000u ||
            (i && (ms <= previous || beat != previous_beat % 4 + 1))) return 0;
        if (ms / 1000.0 <= elapsed) at = (int) i;
        if (ms / 1000.0 <= next.next_cue) target = (int) i;
        previous = ms;
        previous_beat = beat;
    }
    if (at < 0 || target < 0 || (unsigned) target + 1 >= ng) {
        *out = next;
        return 1;
    }
    double ta = pv_be32(grid + target * 8 + 4) / 1000.0;
    double tb = pv_be32(grid + (target + 1) * 8 + 4) / 1000.0;
    next.target_beat = target + (next.next_cue - ta) / (tb - ta);
    next.first = at > 2 ? at - 2 : 0;
    next.n = (int) ng - next.first;
    if (next.n > 64) next.n = 64;
    for (int i = 0; i < next.n; i++) next.times[i] = pv_be32(grid + 8 * (next.first + i) + 4) / 1000.0;
    *out = next;
    return 1;
}
