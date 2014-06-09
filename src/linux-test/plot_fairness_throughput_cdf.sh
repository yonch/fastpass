#!/bin/bash

# plots CDFs of the distribution of
# throughputs achieved by Fastpass and the baseline
# over 1s intervals in each of the 9 phases of the
# experiment. prints out summary statistics.

BIN_SIZE=1000000000 # 1s

# compute fairness properties for fastpass
python compute_fairness_throughput.py $BIN_SIZE fairness_5_senders_start_incrementally_fastpass_port_1100.csv fairness_5_senders_start_incrementally_fastpass_port_1101.csv fairness_5_senders_start_incrementally_fastpass_port_1102.csv fairness_5_senders_start_incrementally_fastpass_port_1103.csv fairness_5_senders_start_incrementally_fastpass_port_1104.csv > fairness_throughput_fastpass.csv

# compute fairness properties for baseline
python compute_fairness_throughput.py $BIN_SIZE fairness_5_senders_start_incrementally_baseline_port_1100.csv fairness_5_senders_start_incrementally_baseline_port_1101.csv fairness_5_senders_start_incrementally_baseline_port_1102.csv fairness_5_senders_start_incrementally_baseline_port_1103.csv fairness_5_senders_start_incrementally_baseline_port_1104.csv > fairness_throughput_baseline.csv

# graph
R < ./graph_fairness_throughput_cdf.R $BIN_SIZE --save
