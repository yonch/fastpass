#!/bin/bash

# this script builds and runs the stress test with the
# specified arguments
# WARNING: this script does not currently set the cpu mask
# in ./run.sh or change the number of cpus shielded.


# check arguments
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <num_algo_cores> <batch_size>"
    exit 0
fi
ALGO_N_CORES="$1"
BATCH_SIZE="$2"
if [ "$BATCH_SIZE" -eq 8 ]; then
    BATCH_SHIFT=3
elif [ "$BATCH_SIZE" -eq 16 ]; then
    BATCH_SHIFT=4
elif [ "$BATCH_SIZE" -eq 32 ]; then
    BATCH_SHIFT=5
else
    echo "batch size $BATCH_SIZE not supported"
    exit 0
fi

# build
make clean
pls make CMD_LINE_CFLAGS+=-DALGO_N_CORES=$ALGO_N_CORES CMD_LINE_CFLAGS+=-DBATCH_SIZE=$BATCH_SIZE CMD_LINE_CFLAGS+=-DBATCH_SHIFT=$BATCH_SHIFT -j9

# run
sudo cset shield -e ./run.sh