#include "rng.h"

static uint64_t rng_state = 0;
static uint64_t rng_inc = 0;

void rng_init(uint64_t seed, uint64_t seq) {
    rng_state = 0ULL;
    rng_inc = (seq << 1ULL) | 1ULL;
    rng_next();
    rng_state += seed;
    rng_next();
}

uint32_t rng_next(void) {
    uint64_t oldstate = rng_state;
    
    rng_state = oldstate * 6364136223846793005ULL + rng_inc;
    
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

uint32_t rng_next_range(uint32_t min, uint32_t max) {
    if (min > max) {
        uint32_t temp = min;
        min = max;
        max = temp;
    }
    
    uint64_t range = (uint64_t)max - (uint64_t)min + 1ULL;
    
    return min + (uint32_t)(rng_next() % range);
}