# 
# Created on: January 5, 2014
# Author: aousterh
#
# This script generates a histogram of packet latencies.
#
# Before running this script, generate the appropriate csv file
# using tcp_sender and tcp_receiver.
#
# This script can be run using:
# R < ./graph_fct_histogram.R bin_duration num_bins --save
#   bin_duration - given in microseconds
#   num_bins - number of bins
# If either of the arguments are ommitted, the default parameters
# will be used instead.

# default parameters
bin_duration = 50
num_bins = 100

# read in any supplied arguments
args <- commandArgs()
if (length(args) > 2)
   bin_duration <- as.integer(args[2])
if (length(args) > 3)
   num_bins <- as.integer(args[3])

# use the ggplot2 library
library('ggplot2')

pdf(file="latency_histogram.pdf", width=6.6, height=3)

# calculate baseline stats
data_0 <- read.csv("fct_4_senders_baseline_port_1100.csv", sep=",")
data_1 <- read.csv("fct_4_senders_baseline_port_1101.csv", sep=",")
data_2 <- read.csv("fct_4_senders_baseline_port_1102.csv", sep=",")
data_3 <- read.csv("fct_4_senders_baseline_port_1103.csv", sep=",")

# bind
data <- rbind(data_0, data_1, data_2, data_3)

# reframe the data
end_index = length(data)
start_index = end_index - num_bins + 1
counts = colSums(data[start_index:end_index]) / sum(colSums(data[start_index:end_index]))
bins = seq(bin_duration / 2, length=num_bins, by=bin_duration)
df = data.frame(bins, counts)
df$cumulative = cumsum(df$counts)
df$experiment = 'baseline'

# calculate fp stats
data_0 <- read.csv("fct_4_senders_fastpass_port_1100.csv", sep=",")
data_1 <- read.csv("fct_4_senders_fastpass_port_1101.csv", sep=",")
data_2 <- read.csv("fct_4_senders_fastpass_port_1102.csv", sep=",")
data_3 <- read.csv("fct_4_senders_fastpass_port_1103.csv", sep=",")

# bind
data <- rbind(data_0, data_1, data_2, data_3)

# reframe the data
end_index = length(data)
start_index = end_index - num_bins + 1
counts = colSums(data[start_index:end_index]) / sum(colSums(data[start_index:end_index]))
bins = seq(bin_duration / 2, length=num_bins, by=bin_duration)
df_fp = data.frame(bins, counts)
df_fp$cumulative = cumsum(df_fp$counts)
df_fp$experiment = 'fastpass'

data_total <- rbind(df, df_fp)

# plot the data
graph = ggplot(data_total, aes(x=bins, y=cumulative, color=experiment, linetype=experiment)) + 
               geom_line() +
               labs(x = "Flow Completion Time (microseconds)",
                    y = "CDF of Flow Completion Times") +
               scale_color_discrete(name = 'Experiment') +
               scale_linetype_discrete(name = 'Experiment')
               #coord_cartesian(ylim=c(0.95, 1))

plot(graph)
