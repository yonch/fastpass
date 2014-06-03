# 
# Created on: June 2, 2014
# Author: aousterh
#
# This script generates a graph of allocation throughput vs.
# number of cores for the timeslot allocation algorithm.
#
# Before running this script, generate the appropriate csv file
# by manually benchmarking using the stress test core.
#
# This script can be run using:
# R < ./graph_timeslot_throughput.R --save

# use the ggplot2 library
library(ggplot2)

data <- read.csv("output.csv", sep=",")

# call device driver
pdf(file="timeslot_throughput.pdf", width=6.6, height=3)

theme_set(theme_bw(base_size=12))

BYTES_PER_TIMESLOT = 1500
BITS_PER_BYTE = 8
EXPERIMENT_DURATION = 10

data$gbps = data$nodetslots * BYTES_PER_TIMESLOT * BITS_PER_BYTE / (EXPERIMENT_DURATION * 1000 * 1000 * 1000)

data

ggplot(data, aes(x=cores, y=gbps, color=as.factor(batch_size), shape=as.factor(batch_size))) +
             geom_abline(aes(intercept=0, slope=data$gbps[2]*0.5), linetype="dashed", color="grey") +
             geom_abline(aes(intercept=0, slope=data$gbps[7]), linetype="dashed", color="grey") +
             geom_point() + geom_line() +
             labs(x = "Cores", y = "Maximum throughput (Gbps)") +
             scale_color_discrete(name="Batch size", guide = guide_legend(reverse = TRUE)) +
             scale_shape_discrete(name="Batch size", guide = guide_legend(reverse = TRUE)) +
             scale_x_continuous(breaks=c(2, 4, 6, 8)) +
             coord_cartesian(xlim=c(0, 9), ylim=c(0, 750))