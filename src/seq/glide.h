/* CARBON glide choices and bounded fixed-point curves.
 * Copyright 2026 Johan W. SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GLIDE_H
#define GLIDE_H
#include <stdint.h>
#define GLIDE_MAX 8
#define GLIDE_LINEAR 64
// Private CV-only Note On encoding: len=flag|curve, data0=choice<<7|note.
#define GLIDE_CV_FLAG 0x80
static inline int glide_ticks(int choice) {
    return choice > 0 && choice <= GLIDE_MAX ? 3 << (choice - 1) : 0;
}
static inline const char *glide_label(int choice) {
    static const char *const labels[] = {
        "Off", "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"
    };
    return choice >= 0 && choice <= GLIDE_MAX ? labels[choice] : "?";
}
/* u and result are Q16, inclusive endpoints. Blend linear with quadratic
 * easing. Both directions use the same progress curve, without overshoot. */
static inline uint32_t glide_curve(uint32_t u, unsigned curve) {
    uint32_t bend;
    if(u >= 65536) return 65536;
    if(curve > 127) curve = 127;
    bend = (uint64_t)u * (65536 - u) >> 16;
    return curve < 64 ? u - bend * (64 - curve) / 64
        : u + bend * (curve - 64) / 63;
}
#endif
