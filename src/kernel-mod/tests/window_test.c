/*
 * window_test.c
 *
 *  Created on: Dec 24, 2013
 *      Author: yonch
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/** from linux's include/asm-generic/bug.h */
#define BUG() do { \
	printf("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	exit(-1); \
} while (0)

#ifndef BUG_USING_FUNCTION
#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while(0)
#else
void BUG_ON(int condition) {
	if (condition)
		BUG();
}
#endif

/* from kernel.h */
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/* this is a way to compute this without platform-dependent code */
#define BITS_PER_LONG 		(BITS_PER_BYTE * sizeof(long))

/* from bitops.h */
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE		8
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

/* include/asm-generic/bitops/non-atomic.h */
static inline void __set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

static inline void __clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

typedef unsigned long long u64;
typedef unsigned long long __u64;
typedef long long s64;
typedef long long __s64;
typedef unsigned int u32;
typedef int s32;
typedef _Bool			bool;

#define unlikely

/* typecheck.h */
#define typecheck(type,x) \
({	type __dummy; \
	typeof(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})

/* jiffies.h */
#define time_after64(a,b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) && \
	 ((__s64)((b) - (a)) < 0))
#define time_before64(a,b)	time_after64(b,a)

#define time_after_eq64(a,b)	\
	(typecheck(__u64, a) && \
	 typecheck(__u64, b) && \
	 ((__s64)((a) - (b)) >= 0))
#define time_before_eq64(a,b)	time_after_eq64(b,a)

/* need __fls */
#define __fls(x) (BITS_PER_LONG - 1 - __builtin_clzl(x))
#define __ffs(x) (__builtin_ffsl(x) - 1)

#include "../window.h"

/* test */
int main(void) {
	u64 tslot;
	s32 gap;
	int i;
	const int BASE = 10007;
	struct fp_window wnd;
	struct fp_window *wndp = &wnd;

	wnd_reset(wndp, BASE - 1);
	for(tslot = BASE - FASTPASS_WND_LEN; tslot < BASE; tslot++) {
		BUG_ON(wnd_at_or_before(wndp, tslot) != -1);
		BUG_ON(wnd_is_marked(wndp, tslot));
	}

	for(i = 0; i < FASTPASS_WND_LEN; i++) {
		wnd_advance(wndp, 1);
		wnd_mark(wndp, wnd.head);
	}

	for(tslot = BASE; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!wnd_is_marked(wndp, tslot));
		BUG_ON(wnd_at_or_before(wndp, tslot) != 0);
	}

	BUG_ON(wnd_earliest_marked(wndp) != BASE);
	wnd_clear(wndp, BASE);
	BUG_ON(wnd_earliest_marked(wndp) != BASE+1);
	BUG_ON(wnd_at_or_before(wndp, BASE) != -1);
	BUG_ON(wnd_at_or_before(wndp, BASE+1) != 0);
	wnd_clear(wndp, BASE+2);
	BUG_ON(wnd_earliest_marked(wndp) != BASE+1);
	BUG_ON(wnd_at_or_before(wndp, BASE+2) != 1);

	for(tslot = BASE+3; tslot < BASE + 152; tslot++) {
		wnd_clear(wndp, tslot);
		BUG_ON(wnd_is_marked(wndp, tslot));
		BUG_ON(wnd_at_or_before(wndp, tslot) != tslot - BASE - 1);
		BUG_ON(wnd_at_or_before(wndp, tslot+1) != 0);
		BUG_ON(wnd_earliest_marked(wndp) != BASE+1);
	}
	for(tslot = BASE+152; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!wnd_is_marked(wndp, tslot));
		BUG_ON(wnd_at_or_before(wndp, tslot) != 0);
	}

	wnd_clear(wndp, BASE+1);
	BUG_ON(wnd_earliest_marked(wndp) != BASE+152);

	printf("done testing wnd, cleaning up\n");

	/* clean up */
	tslot = wndp->head;
clear_next_marked:
	gap = wnd_at_or_before(wndp, tslot);
	if (gap >= 0) {
		tslot -= gap;
		wnd_clear(wndp, tslot);
		goto clear_next_marked;
	}

	return 0;
}

