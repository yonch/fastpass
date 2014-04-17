
#define MAX_NODES 		64

#define BATCH_SIZE 8  // must be consistent with bitmaps in batch_state
#define BATCH_SHIFT 3  // 2^BATCH_SHIFT = BATCH_SIZE
/* packing of bitmasks into 64 bit words */
#if 0
#define BITMASKS_PER_64_BIT 	(64 >> BATCH_SHIFT)
#define BITMASK_WORD(node)		(node >> (6 - BATCH_SHIFT))
#define BITMASK_SHIFT(node)		((node << BATCH_SHIFT) & (64 - 1))
#else
#define BITMASKS_PER_64_BIT 	1
#define BITMASK_WORD(node)		node
#define BITMASK_SHIFT(node)		0
#endif

#define TEST_NUM_BINS				(4*1000*1000)
#define TEST_SEED					0xDEADBEEFDEADBEEFULL
#define TEST_RING_READS_PER_BIN		10
#define TEST_DEMANDS_PER_RING_READ	30

/* MMIX by Knuth, see LCG on wikipedia */
#define RAND_A					6364136223846793005
#define RAND_C					1442695040888963407

#include <stdint.h>
#include <stdio.h>

void main() {
    uint64_t src_endnodes [MAX_NODES / BITMASKS_PER_64_BIT];
    uint64_t dst_endnodes [MAX_NODES / BITMASKS_PER_64_BIT];
    uint64_t batch_total = 0;
    uint64_t allowed_mask = (1ULL << BATCH_SIZE) - 1;
    uint64_t rand_x[TEST_DEMANDS_PER_RING_READ];
    uint32_t ring[TEST_DEMANDS_PER_RING_READ];

    int i,j,k;

    /* initialize rand_x array */
    uint64_t tmp_rand = TEST_SEED;
    for (i = 0; i < TEST_DEMANDS_PER_RING_READ; i++) {
    	tmp_rand = (tmp_rand * RAND_A + RAND_C) ^ 0xBAB124BBC847521FULL;
    	rand_x[i] = tmp_rand;
    }

    for(i = 0; i < TEST_NUM_BINS; i++) {
    	/* re-initialize masks */
    	for (j = 0; j < MAX_NODES / BITMASKS_PER_64_BIT; j++) {
    		src_endnodes[j] = ~0ULL;
    		dst_endnodes[j] = ~0ULL;
    	}

    	for (j = 0; j < TEST_RING_READS_PER_BIN; j++) {
    		/* generate numbers as if read from ring */
    		for (k = 0; k < TEST_DEMANDS_PER_RING_READ; k++) {
    			ring[k] = rand_x[k] >> 32;
        		rand_x[k] = rand_x[k] * RAND_A + RAND_C;
    		}

    		/* go through numbers read from the ring */
    		for (k = 0; k < TEST_DEMANDS_PER_RING_READ; k++) {
        		uint16_t src = ring[k] & (MAX_NODES - 1);
        		uint16_t dst = (ring[k] >> 16) & (MAX_NODES - 1);

        	    uint64_t src_mask = src_endnodes[BITMASK_WORD(src)];
        	    uint64_t dst_mask = dst_endnodes[BITMASK_WORD(dst)];

        	    uint64_t timeslot_bitmap =
        				allowed_mask
        				& (src_mask >> BITMASK_SHIFT(src))
    					& (dst_mask >> BITMASK_SHIFT(dst));

    //    		printf("src %d dst %d bitmap %lX\n", src, dst, timeslot_bitmap);
        	    if (timeslot_bitmap == 0ULL)
        	    	continue;

				uint64_t batch_timeslot;
				asm("bsfq %1,%0" : "=r"(batch_timeslot) : "r"(timeslot_bitmap));
				asm("btr %1,%0" : "+r" (src_mask) : "Ir" (batch_timeslot + BITMASK_SHIFT(src)));
				asm("btr %1,%0" : "+r" (dst_mask) : "Ir" (batch_timeslot + BITMASK_SHIFT(dst)));
				src_endnodes[BITMASK_WORD(src)] = src_mask;
				dst_endnodes[BITMASK_WORD(dst)] = dst_mask;

        	    batch_total++;
        	}
    	}
    	/* go through pseudo-random demands */
    }

    printf("batch_total %lu\n", batch_total);
}
