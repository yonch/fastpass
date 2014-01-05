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
#
# This script can be run using:
# R < ./graph_path_selection_vs_utilization.R --save

data <- read.csv("output.csv", sep=",")
attach(data)

min_size = min(data$nodes)
max_size = max(data$nodes)
num_sizes = length(unique(data$nodes))

# set up plot
xrange <- range(observed_utilization)
yrange <- range(data$time)
plot(xrange, yrange, type="n", xlab="Network Utilization (%)", ylab="Latency (microseconds)")

colors <- rainbow(num_sizes)
linetype <- c(1:num_sizes)
plotchar <- seq(15, 15+num_sizes, 1)

# add plot lines
num_nodes = min_size
i = 1
while (num_nodes <= max_size) {
	data_for_this_size <- subset(data, nodes==num_nodes)
	lines(data_for_this_size$observed_utilization, data_for_this_size$time, type="b", lwd=1.5, lty=linetype[i], col=colors[i], pch=plotchar[i])
	num_nodes <- 2 * num_nodes
	i <- i + 1
}

# add a title
title("Latency of Admissible Traffic Algorithm")

# add a legend
sizes = 2^(log(min_size, 2):log(max_size, 2))
legend(xrange[1], yrange[2], sizes, cex=0.8, col=colors, pch=plotchar, lty=linetype, title="Number of Nodes")

detach(data)