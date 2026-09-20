/**
 * @file kernel_rng.h
 * @author Malcolm
 * @brief
 * @version
 * @date 7/1/2026
 */
#ifndef WDM_MOCK_KERNEL_RNG_H
#define WDM_MOCK_KERNEL_RNG_H

#include <stdint.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

/* Global kernel RNG state, must be seeded before use. Must not be all-zero. */
    static uint64_t rng_state[4] = {
        0x9E3779B97F4A7C15ULL,
        0xBF58476D1CE4E5B9ULL,
        0x94D049BB133111EBULL,
        0x2545F4914F6CDD1DULL
    };

    /* splitmix64 is used only to expand a single 64-bit seed into the 256-bit xoshiro256** state.
     * This is the recommended way by the xoshiro256** authors for seeding from a single value. */
    static inline uint64_t splitmix64_next(uint64_t* state) {
        uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    static inline void rng_seed(uint64_t seed) {
        uint64_t sm_state = seed;

        if (sm_state == 0) sm_state = 0xA55A5AA5C3C3D6D6ULL;

        rng_state[0] = splitmix64_next(&sm_state);
        rng_state[1] = splitmix64_next(&sm_state);
        rng_state[2] = splitmix64_next(&sm_state);
        rng_state[3] = splitmix64_next(&sm_state);
    }

    static inline uint64_t rotl(const uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    }

    static inline uint64_t rng_next(void) {
        const uint64_t result = rotl(rng_state[1] * 5, 7) * 9;
        const uint64_t t = rng_state[1] << 17;

        rng_state[2] ^= rng_state[0];
        rng_state[3] ^= rng_state[1];
        rng_state[1] ^= rng_state[2];
        rng_state[0] ^= rng_state[3];

        rng_state[2] ^= t;

        rng_state[3] = rotl(rng_state[3], 45);

        return result;
    }

    /* Jump function, useful if for per-CPU streams that are guaranteed non-overlapping
     * (equivalent to 2^128 calls to rng_next()).
     * Call once per CPU with a different jump count, or just call it N times to set up N independent streams.
     */
    static inline void rng_jump(void) {
        static const uint64_t JUMP[] = {
            0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
            0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
        };

        uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        for (size_t i = 0; i < 4; i++) {
            for (int b = 0; b < 64; b++) {
                if (JUMP[i] & (1ULL << b)) {
                    s0 ^= rng_state[0];
                    s1 ^= rng_state[1];
                    s2 ^= rng_state[2];
                    s3 ^= rng_state[3];
                }
                rng_next();
            }
        }

        rng_state[0] = s0;
        rng_state[1] = s1;
        rng_state[2] = s2;
        rng_state[3] = s3;
    }

#ifdef __cplusplus
}
#endif

#endif //WDM_MOCK_KERNEL_RNG_H
