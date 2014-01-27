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

ggplot(data, aes(fct, linetype=as.factor(target_util), color=algo)) + stat_ecdf() +
             labs(x = "Flow Completion Time (timeslots)", y = "CDF of FCTs") +
             scale_x_log10(breaks= trans_breaks("log10", function(x) 10^x)) + 
             scale_colour_discrete(name="Algorithm") +
             scale_linetype_discrete(name="Target Utilization")
             

