/*
 * linux-compat.h
 *
 *  Created on: Dec 26, 2013
 *      Author: yonch
 */

#ifndef LINUX_COMPAT_H_
#define LINUX_COMPAT_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define CONFIG_64BIT

typedef _Bool			bool;

static inline void panic(void) {
	exit(-1);
}

/** from linux's include/asm-generic/bug.h */
#define BUG() do { \
	printf("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	panic(); \
} while (0)

#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while(0)

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
static inline void __set_bit(int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

static inline void __clear_bit(int nr, unsigned long *addr)
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
typedef unsigned short u16;
typedef unsigned char u8;


#define unlikely
#define likely

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

/* from Jenkin's public domain lookup3.c at http://burtleburtle.net/bob/c/lookup3.c */
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}


static inline u32 jhash_3words(u32 a, u32 b, u32 c, u32 initval)
{
	a += 0xDEADBEEF;
	b += 0xDEADBEEF;
	c += initval;
	final(a,b,c);
	return c;
}

static inline u32 jhash_1word(u32 a, u32 initval)
{
	return jhash_3words(a, 0, 0, initval);
}

#endif /* LINUX_COMPAT_H_ */
