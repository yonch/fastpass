/*
 * bitasm.h
 *
 *  Created on: Jun 6, 2014
 *      Author: yonch
 */

#ifndef BITASM_H_
#define BITASM_H_

#define set_and_jmp_if_was_set(bitmap, index, jmp_label)			\
	asm volatile goto ("bts %1,%0\n\t"								\
					   "jc %l2"										\
					   : /* no outputs, will clobber memory */ 		\
					   :"m" (*(uint64_t *)(bitmap)), "r" (index)	\
					   : "cc", "memory"								\
					   : jmp_label)

static inline __attribute__((always_inline))
void arr_unset_bit(uint64_t *arr, uint64_t index) {
	asm("btr %1,%0" : "+m" (*(uint64_t *)arr) : "r" (index) : "cc", "memory");
}

#define word_unset_bit(word, index) \
	asm("btr %1,%0" : "+m" (word) : "r" (index) : "cc")


#endif /* BITASM_H_ */
