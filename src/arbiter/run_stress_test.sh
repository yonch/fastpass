#!/bin/bash

# this script builds and runs the stress test with the
# specified arguments. it also shields CPUs appropriately.

if [ x`which pls` == x ]; then
    echo no_pls
    MAKE=make
else
    echo pls
    MAKE="pls make"
fi

# check arguments
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <num_algo_cores> <batch_size>"
    exit 0
fi

# parse num algo cores and determine corresponding mask
# for CPU shielding
ALGO_N_CORES="$1"
let "TOTAL_NUM_CORES = $ALGO_N_CORES + 2"
if [ "$TOTAL_NUM_CORES" -eq 3 ]; then
    CPU_MASK=e
elif [ "$TOTAL_NUM_CORES" -eq 4 ]; then
    CPU_MASK=1e
elif [ "$TOTAL_NUM_CORES" -eq 6 ]; then
    CPU_MASK=7e
elif [ "$TOTAL_NUM_CORES" -eq 8 ]; then
    CPU_MASK=1fe
elif [ "$TOTAL_NUM_CORES" -eq 10 ]; then
    CPU_MASK=7fe
elif [ "$TOTAL_NUM_CORES" -eq 18 ]; then
    CPU_MASK=7fffe
else
    echo "num cores $TOTAL_NUM_CORES ($ALGO_N_CORES algo cores) not supported"
    exit 0
fi

# parse batch size and determine batch shift
BATCH_SIZE="$2"
if [ "$BATCH_SIZE" -eq 1 ]; then
    BATCH_SHIFT=0
elif [ "$BATCH_SIZE" -eq 2 ]; then
    BATCH_SHIFT=1
elif [ "$BATCH_SIZE" -eq 4 ]; then
    BATCH_SHIFT=2
elif [ "$BATCH_SIZE" -eq 8 ]; then
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
$MAKE clean
$MAKE CMD_LINE_CFLAGS+=-DALGO_N_CORES=$ALGO_N_CORES CMD_LINE_CFLAGS+=-DBATCH_SIZE=$BATCH_SIZE CMD_LINE_CFLAGS+=-DBATCH_SHIFT=$BATCH_SHIFT -j9

# shield cpus appropriately
sudo cset shield -c 1-$TOTAL_NUM_CORES

# run
sudo cset shield -e -- ./run.sh $CPU_MASK
