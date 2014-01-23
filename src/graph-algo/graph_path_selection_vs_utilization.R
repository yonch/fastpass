# 
# Created on: January 4, 2013
# Author: aousterh
#
# This script generates a graph of network utilization vs. latency
# for the path selection algorithm for several different
# oversubscription ratios, for a specific network topology.
#
# Before running this script, generate the appropriate csv file:
# ./benchmark_graph_algo 1 > output.csv
#
# This script can be run using:
# R < ./graph_path_selection_vs_utilization.R --save

# use the ggplot2 library
library(ggplot2)

data <- read.csv("output.csv", sep=",")
attach(data)

# call device driver
pdf(file="scalability_path_selection.pdf", width=6.6, height=3)

theme_set(theme_bw(base_size=10))

ggplot(data, aes(x=observed_utilization, y=time,
             group=as.factor(oversubscription_ratio), color=as.factor(oversubscription_ratio))) +
             geom_point() + geom_line() +
             scale_color_discrete(name="Oversubscription\nRatio") +
             labs(x = "Network Utilization (%)",
                  y = "Latency (microseconds)") +
             guides(col = guide_legend(reverse = TRUE)) +
             coord_cartesian(ylim=c(0, 25))

detach(data)

dev.off()
