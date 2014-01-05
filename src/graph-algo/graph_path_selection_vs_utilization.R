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

data <- read.csv("output.csv", sep=",")
attach(data)

min_ratio = min(data$oversubscription_ratio)
max_ratio = max(data$oversubscription_ratio)
num_ratios = length(unique(data$oversubscription_ratio))

# set up plot
xrange <- range(observed_utilization)
yrange <- range(data$time, 0)
plot(xrange, yrange, type="n", xlab="Interrack Utilization (%)", ylab="Latency (microseconds)")

colors <- rainbow(num_ratios)
linetype <- c(1:num_ratios)
plotchar <- seq(15, 15+num_ratios, 1)

# add plot lines
ratio = min_ratio
i = 1
while (ratio <= max_ratio) {
	data_for_this_size <- subset(data, oversubscription_ratio==ratio)
	lines(data_for_this_size$observed_utilization, data_for_this_size$time, type="b", lwd=1.5, lty=linetype[i], col=colors[i], pch=plotchar[i])
	ratio <- 2 * ratio
	i <- i + 1
}

# add a title
title("Latency of Path Selection Algorithm")

# add a legend
ratios = 2^(log(min_ratio, 2):log(max_ratio, 2))
legend(xrange[1], yrange[2], ratios, cex=0.8, col=colors, pch=plotchar, lty=linetype, title="Oversubscription Ratio")

detach(data)