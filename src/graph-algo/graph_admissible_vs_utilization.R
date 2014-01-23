# 
# Created on: December 26, 2013
# Author: aousterh
#
# This script generates a graph of network utilization vs. latency
# for the admissible traffic algorithm for several different
# numbers of nodes.
#
# Before running this script, generate the appropriate csv file:
# ./benchmark_graph_algo 0 > output.csv
# OR
# ./benchmark_sjf > output.csv
#
# This script can be run using:
# R < ./graph_path_selection_vs_utilization.R type --save
# type 0 (default) is for round robin
# type 1 is for shortest remaining job first

# use the ggplot2 library
library(ggplot2)

# read in arguments
args <- commandArgs()
type = 0 # round-robin
if (length(args) > 2) {
   type <- as.integer(args[2])
}

data <- read.csv("output.csv", sep=",")
attach(data)

# call device driver
if (type == 0) {
   pdf(file="scalability_time_allocation_rr.pdf", width=6.6, height=3)
} else {
   pdf(file="scalability_time_allocation_sjf.pdf", width=6.6, height=3)
}

theme_set(theme_bw(base_size=10))

ggplot(data, aes(x=observed_utilization, y=time,
             group=as.factor(nodes), color=as.factor(nodes))) +
             geom_point() + geom_line() +
             scale_color_discrete(name="Nodes") +
             labs(x = "Network Utilization (%)",
                  y = "Latency (microseconds)") +
             scale_y_log10() +
             guides(col = guide_legend(reverse = TRUE))

detach(data)

dev.off()
