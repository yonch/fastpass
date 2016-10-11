/*
 * debug.h
 *
 *  Created on: Dec 27, 2013
 *      Author: yonch
 */

#ifndef FASTPASS_DEBUG_H_
#define FASTPASS_DEBUG_H_

#ifdef __KERNEL__
/*
 * 	Warning and debugging macros, (originally taken from DCCP)
 */
#define FASTPASS_WARN(fmt, a...) net_warn_ratelimited("%s: " fmt,       \
							__func__, ##a)
#define FASTPASS_CRIT(fmt, a...) printk(KERN_CRIT fmt " at %s:%d/%s()\n", ##a, \
					 __FILE__, __LINE__, __func__)
#define FASTPASS_BUG(a...)       do { FASTPASS_CRIT("BUG: " a); dump_stack(); } while(0)
#define FASTPASS_BUG_ON(cond)    do { if (unlikely((cond) != 0))		   \
				     FASTPASS_BUG("\"%s\" holds (exception!)", \
					      __stringify(cond));          \
			     } while (0)

#else

#include <stdio.h>
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>


/* based on http://stackoverflow.com/questions/77005/how-to-generate-a-stacktrace-when-my-gcc-c-app-crashes */
static inline void fp_backtrace(void) {
  void *array[50];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 50);

  // print out all the frames to stderr
  backtrace_symbols_fd(array, size, STDERR_FILENO);
}

#ifdef NO_DPDK
#define FASTPASS_BUG_SHOULD_PANIC	1
#else
#define FASTPASS_BUG_SHOULD_PANIC	0
#endif

static inline void panic(void) {
	exit(-1);
}

/** from linux's include/asm-generic/bug.h */
#define FASTPASS_BUG() do { \
	printf("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	fp_backtrace(); \
	if (FASTPASS_BUG_SHOULD_PANIC) \
		panic(); \
} while (0)

#define FASTPASS_BUG_ON(condition) do { if (unlikely(condition)) FASTPASS_BUG(); } while(0)

#endif

#endif /* FASTPASS_DEBUG_H_ */
