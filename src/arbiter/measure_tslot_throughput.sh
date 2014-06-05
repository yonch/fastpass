#!/bin/bash

# this script measures the throughput of the
# timeslot allocation algorithm for several
# different sets of parameters

# Create a directory in which to store results
# Include time in the name so that you don't
# accidentally overwrite an existing dir
TIME="$(date +%s)"
DIR=throughput_results_$TIME
mkdir $DIR

# echo the current git SHA into a file in the
# directory to make results easy to reproduce
echo `git rev-list --max-count=1 HEAD` > $DIR/info.txt

# run the experiment for different sets of parameters
algo_n_cores=( 1 2 4 8 )
batch_size=( 8 16 32 )

for n_cores in "${algo_n_cores[@]}"
do
    for batch in "${batch_size[@]}"
    do
        echo running experiment with $n_cores cores and batch size $batch
        ./run_stress_test.sh $n_cores $batch > $DIR/log_${n_cores}_cores_${batch}_batch_size.txt
    done
done

# turn off all shielding
sudo cset shield --reset