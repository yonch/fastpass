# 
# Created on: February 18, 2014
# Author: aousterh
#
# This script generates a line graph of the fairness over time
# for fastpass and the baseline (similar to Figure 10 in VL2).
#
# Before running this script, generate the appropriate csv file
# using tcp_sender and tcp_receiver. compute_fairness.py can
# conver those logs into a suitable csv of fairness.
#
# This script can be run using:
# R < ./graph_fairness_index_over_time.R --save

# use ggplot2
library(ggplot2)

# read in data
data_fp <- read.csv("output_fastpass.csv", sep=",")
data_baseline <- read.csv("output_baseline.csv", sep=",")

pdf(file="fairness_index_over_time.pdf", width=6.6, height=3)

theme_set(theme_bw(base_size=12))

data_fp$Experiment = 'fastpass'
data_baseline$Experiment = 'baseline'

# create new data frames of only the data we want for this graph
START_SECONDS = 1
END_SECONDS = 270
data_fp$interval_time = ((data_fp$start_time + data_fp$end_time) / 2 - data_fp$start_time[1]) / 10^9 - START_SECONDS
new_data_fp <- data_fp[data_fp$interval_time >= 0 & data_fp$interval_time < END_SECONDS,]

data_baseline$interval_time = ((data_baseline$start_time + data_baseline$end_time) / 2 - data_baseline$start_time[1]) / 10^9 - START_SECONDS
new_data_baseline <- data_baseline[data_baseline$interval_time >= 0 & data_baseline$interval_time < END_SECONDS,]

data <- rbind(new_data_fp, new_data_baseline)

graph = ggplot(data, aes(interval_time, Jain_index, color=as.factor(Experiment))) +
      geom_line() +
      facet_grid(Experiment ~ .) +
      labs(x = "Time (seconds)", y = "Jain's Fairness Index") +
      scale_color_discrete(name="") +
      coord_cartesian(ylim=c(-0.05, 1.05))

plot(graph)

