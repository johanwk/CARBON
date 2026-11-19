/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HUMANIZE_H
#define HUMANIZE_H
#include <stdint.h>

/* Session-only settings. No persistent song fields or shared random stream. */
enum humanize_param {
    HUMANIZE_ENABLE, HUMANIZE_VELOCITY_CHANCE, HUMANIZE_VELOCITY_AMOUNT,
    HUMANIZE_GATE_CHANCE, HUMANIZE_GATE_AMOUNT, HUMANIZE_TIMING_CHANCE,
    HUMANIZE_LATE_MAX, HUMANIZE_PARAMS
};
struct humanize_settings { uint8_t value[HUMANIZE_PARAMS]; };
#define HUMANIZE_MAX_DURATION 262140u

static inline int humanize_limit(int param) {
    static const uint8_t limits[HUMANIZE_PARAMS] = {1,100,32,100,100,100,24};
    return param >= 0 && param < HUMANIZE_PARAMS ? limits[param] : 0;
}
static inline void humanize_set(struct humanize_settings *s, int param, int value) {
    if(param < 0 || param >= HUMANIZE_PARAMS) return;
    if(value < 0) value = 0;
    if(value > humanize_limit(param)) value = humanize_limit(param);
    s->value[param] = value;
}
static inline uint32_t humanize_seed(uint32_t seed, int track) {
    uint32_t x = seed ^ (0xa511e9b3u + (uint32_t)track * 0x9e3779b9u);
    return x ? x : 1;
}
static inline uint32_t humanize_random(uint32_t *state) {
    uint32_t x = *state ? *state : 1;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}
/* Bounded multiply-high mapping: no retry loops in the RT path. */
static inline int humanize_draw(uint32_t *state, unsigned range) {
    return (int)(((uint64_t)humanize_random(state) * range) >> 32);
}
static inline int humanize_selected(uint32_t *state, int chance) {
    return chance > 0 && (chance >= 100 || humanize_draw(state, 100) < chance);
}
/* Difference of two uniform draws: symmetric triangular integer distribution. */
static inline int humanize_signed(uint32_t *state, int amount) {
    int a = humanize_draw(state, (unsigned)amount + 1);
    int b = humanize_draw(state, (unsigned)amount + 1);
    return a - b;
}
static inline int humanize_timing(const struct humanize_settings *s, uint32_t *state) {
    int amount = s->value[HUMANIZE_LATE_MAX];
    if(!s->value[HUMANIZE_ENABLE] || !amount ||
            !humanize_selected(state, s->value[HUMANIZE_TIMING_CHANCE])) return 0;
    return 1 + humanize_draw(state, (unsigned)amount);
}
static inline int humanize_velocity(const struct humanize_settings *s, uint32_t *state, int velocity) {
    int amount = s->value[HUMANIZE_VELOCITY_AMOUNT];
    if(!velocity || !s->value[HUMANIZE_ENABLE] || !amount ||
            !humanize_selected(state, s->value[HUMANIZE_VELOCITY_CHANCE])) return velocity;
    velocity += humanize_signed(state, amount);
    return velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;
}
static inline uint32_t humanize_gate_percent(uint32_t duration, int percent) {
    /* Nearest integer tick, ties up, after normal Track Gate truncation. */
    uint32_t result = (duration * (100 + percent) + 50) / 100;
    return result < 1 ? 1 : result > HUMANIZE_MAX_DURATION ? HUMANIZE_MAX_DURATION : result;
}
static inline uint32_t humanize_gate(const struct humanize_settings *s, uint32_t *state, uint32_t duration) {
    int amount = s->value[HUMANIZE_GATE_AMOUNT];
    if(!s->value[HUMANIZE_ENABLE] || !amount ||
            !humanize_selected(state, s->value[HUMANIZE_GATE_CHANCE])) return duration;
    return humanize_gate_percent(duration, humanize_signed(state, amount));
}
#endif
