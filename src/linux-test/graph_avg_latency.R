# 
# Created on: January 5, 2014
# Author: aousterh
#
# This script plots the average packet latency.
#
# Before running this script, generate the appropriate csv file
# using tcp_sender and tcp_receiver. The csv fill should be
# called data.csv
#
# This script can be run using:
# R < ./graph_avg_latency.R num_senders --save
#   num_senders - the number of clients. defaults to 1

# parameters of input data set
base_columns = 2
columns_per_sender = 5
start_packets_offset = 1
latency_sum_offset = 3

# read in data
data <- read.csv("data.csv", sep=",")

# read in arguments
args <- commandArgs()
num_senders = 1
if (length(args) > 2)
  num_senders <- as.integer(args[2])

# set up plot
interval_time = ((data$start_time + data$end_time) / 2 - data$start_time[1]) / 10^9
xrange <- range(interval_time)
yrange <- range(0, 20*1000)  # up to 20 milliseconds
plot(xrange, yrange, type="n", xlab="Time (seconds)",
             ylab="Average Packet Latency (microseconds)")

# choose colors, styles, etc.
colors <- rainbow(num_senders)

# add plot lines
for (i in 1:num_senders) {
    avg_packet_latency = data[, base_columns + (i-1) * columns_per_sender + latency_sum_offset] /
                         (data[, base_columns + (i-1) * columns_per_sender + start_packets_offset] * 1000)
    lines(interval_time, avg_packet_latency, type="l", lwd=1, lty=1, col=colors[i])
}

# add a title
title("Average Packet Latency")

# add a legend
node_ids = c(1:num_senders)
legend(xrange[1], yrange[2], node_ids, col=colors, lty=1, title = "Clients")
