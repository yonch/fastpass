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
# R < ./graph_timeslot_throughput.R <input_file> --save

# use the ggplot2 library
library(ggplot2)

# read in arguments
args <- commandArgs()
f = "output.csv"
if (length(args) > 2) {
   f <- args[2]
}

data <- read.csv(f, sep=",")

# call device driver
pdf(file="timeslot_throughput.pdf", width=6.6, height=3)

theme_set(theme_bw(base_size=12))

data

ggplot(data, aes(x=cores, y=gbps, color=as.factor(batch_size), shape=as.factor(batch_size))) +
             geom_point() + geom_line() +
             labs(x = "Cores", y = "Maximum throughput (Gbps)") +
             scale_color_discrete(name="Batch size", guide = guide_legend(reverse = TRUE)) +
             scale_shape_discrete(name="Batch size", guide = guide_legend(reverse = TRUE)) +
             scale_x_continuous(breaks=c(2, 4, 6, 8)) +
             coord_cartesian(xlim=c(0, 9), ylim=c(0, 750))