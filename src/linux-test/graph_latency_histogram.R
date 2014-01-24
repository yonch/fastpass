# 
# Created on: January 5, 2014
# Author: aousterh
#
# This script generates a histogram of packet latencies.
#
# Before running this script, generate the appropriate csv file
# using tcp_sender and tcp_receiver. The csv fill should be
# called data.csv
#
# This script can be run using:
# R < ./graph_latency_histogram.R bin_duration num_bins --save
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

data <- read.csv("data.csv", sep=",")

# reframe the data
end_index = length(data)
start_index = end_index - num_bins + 1
counts = colSums(data[start_index:end_index]) / sum(colSums(data[start_index:end_index]))
bins = seq(bin_duration / 2, length=num_bins, by=bin_duration)
df = data.frame(bins, counts)

# plot the data
graph = ggplot(df, aes(x=bins, y=counts)) +
      geom_bar(stat='identity', fill="blue") +
      labs(x = "Packet Latency (microseconds)", y = "PDF of Packets")

plot(graph)

