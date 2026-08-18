#ifndef RNG_H
#define RNG_H

#include <stdint.h>

void rng_init(uint64_t seed, uint64_t seq);

uint32_t rng_next(void);

uint32_t rng_next_range(uint32_t min, uint32_t max);

#endif