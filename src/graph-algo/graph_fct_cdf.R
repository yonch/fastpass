# 
# Created on: January 21, 2014
# Author: aousterh
#
# This script generates a CDF of flow completion times.
#
# Before running this script, generate the appropriate csv file:
# python benchmark_schedule_quality.py > output.csv
#
# This script can be run using:
# R < ./graph_fct_cdf.R --save

# use the ggplot2 library
library(ggplot2)
library(scales)

data <- read.csv("output.csv", sep=",")

pdf(file="timeslot_allocation_fcts.pdf", width=6.6, height=3)

theme_set(theme_bw(base_size=12))

ggplot(data, aes(fct, linetype=as.factor(target_util), color=algo)) + stat_ecdf() +
             labs(x = "Flow Completion Time (number of timeslots)", y = "CDF of FCTs") +
             scale_x_log10(breaks=c(100, 1000, 10000)) + 
             scale_colour_manual(name="Algorithm", values=c("#8DD3C7", "#FB8072", "#BEBADA"),
                                 breaks=c("round_robin", "shortest_job_first", "random"),
                                 labels=c("round robin", "shortest first", "random")) +
             scale_linetype_discrete(name="Network\nUtilization", breaks=c(0.2, 0.5, 0.9),
                                     labels=c("20%", "50%", "90%"))
             

