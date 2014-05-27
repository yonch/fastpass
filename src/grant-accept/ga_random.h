#ifndef GRANT_ACCEPT_RANDOM_H_
#define GRANT_ACCEPT_RANDOM_H_

/**
 * Use Numerical Recipes
 * http://en.wikipedia.org/wiki/Linear_congruential_generator
 */
#define GA_RAND_A		1664525
#define GA_RAND_C		1013904223

static inline __attribute__((always_inline))
void ga_srand(u32 *state, u32 value)
{
	*state = value;
}

/* produces a value between 0 and max_val */
static inline __attribute__((always_inline))
u32 ga_rand(u32 *state, u16 max_val)
{
	u32 res = ((*state >> 16) * max_val) >> 16;
	*state = *state * GA_RAND_A + GA_RAND_C;
        return res;
}

#endif /* GRANT_ACCEPT_RANDOM_H_ */

