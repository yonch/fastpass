#!/bin/bash

# this script parses the output of a throughput experiment, generated
# by running ./measure_tslot_throughput.sh in fastpass/src/arbiter.
# it generates a csv output file and a graph of throughput vs. number
# of cores.

# check the arguments
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <results_directory>"
    exit 0
fi

# get the directory name
DIR="$1"

# create an output file
# include the timestamp in the dir in the name
TIMESTAMP=$(echo "$DIR" | cut -d '_' -f3)
OUTPUT_FILE="tslot_throughput_$TIMESTAMP.csv"
echo "cores,gbps,algo,batch_size,nodes" > $OUTPUT_FILE

# process each log file in the directory
echo "reading files from $DIR"
for f in $( ls $DIR/log_* ); do
    echo "parsing file $f"
    
    # get experiment parameters from the file
    ALGO_N_CORES=$(grep "admission core" $f | tail -n 1 | cut -d ' ' -f5)
    BATCH_SIZE=$(grep "admission core" $f | tail -n 1 | cut -d ' ' -f8)
    NODES=$(grep "admission core" $f | tail -n 1 | cut -d ' ' -f11)

    # get throughput
    THROUGHPUT=$(grep "best" $f | tail -n 1 | cut -d ' ' -f14)
    
    # echo results into file
    echo "$ALGO_N_CORES,$THROUGHPUT,seq,$BATCH_SIZE,$NODES" >> $OUTPUT_FILE
done

# plot the results
R < ./graph_timeslot_throughput.R $OUTPUT_FILE --save
