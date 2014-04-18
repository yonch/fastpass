
#include "admissible_traffic.h"

#include <stdio.h>

#define SPECIAL_START		(NUM_BINS-BATCH_SIZE)
#define BASE				1024

int main()
{
	int i;

	assert(bin_index_from_timeslot(BASE, BASE) == NUM_BINS);
	assert(bin_index_from_timeslot(BASE+1, BASE) == NUM_BINS+1);
	assert(bin_index_from_timeslot(BASE-1, BASE) == NUM_BINS-1);

//	for (i = 0; i < NUM_BINS + 8 * BATCH_SIZE; i++) {
//		uint16_t bin = bin_index_from_timeslot_gap(BASE - i, BASE);
//		printf("i=%d timeslot=%d bin_index=%d\n", i, BASE-i, bin);
//	}

	assert((BASE & (BATCH_SIZE - 1)) == 0);

	for (i = 0; i < BASE; i++) {
		/* start at the batch timeslot where i is in its own bin */
		uint16_t bin = NUM_BINS - BATCH_SIZE + (i % BATCH_SIZE);
		uint32_t batch_head = BATCH_SIZE + (i & ~(BATCH_SIZE - 1));

		for (; batch_head < BASE; batch_head+= BATCH_SIZE) {
			if (bin <= 2 * BATCH_SIZE)
				bin = bin / 2;
			else
				bin -= BATCH_SIZE;
		}
		uint16_t computed_bin = bin_index_from_timeslot(i, BASE);
//		printf("gap=%d timeslot=%d simulated_bin=%d computed_bin=%d\n",
//				BASE-i, i, bin, computed_bin);
		assert(computed_bin == bin);
	}

}

