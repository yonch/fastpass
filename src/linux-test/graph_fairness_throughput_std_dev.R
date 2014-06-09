# 
# Created on: June 8, 2014
# Author: aousterh
#
# This script generates plots of std deviations of throughput
# achieved during different portions of the fairness experiment.
#
# This script can be run using:
# R < ./graph_fairness_throughput_std_dev.R <bin_size_ns> --save

## Based on: http://www.cookbook-r.com/Manipulating_data/Summarizing_data/
## Summarizes data.
## Gives count, mean, standard deviation, standard error of the mean, and confidence interval (default 95%).
##   data: a data frame.
##   measurevar: the name of a column that contains the variable to be summariezed
##   groupvars: a vector containing names of columns that contain grouping variables
##   na.rm: a boolean that indicates whether to ignore NA's
##   conf.interval: the percent range of the confidence interval (default is 95%)
summarySE <- function(data=NULL, measurevar, groupvars=NULL, na.rm=FALSE,
                      conf.interval=.95, .drop=TRUE) {
    require(plyr)

    # New version of length which can handle NA's: if na.rm==T, don't count them
    length2 <- function (x, na.rm=FALSE) {
        if (na.rm) sum(!is.na(x))
        else       length(x)
    }

    # This does the summary. For each group's data frame, return a vector with
    # N, mean, and sd
    datac <- ddply(data, groupvars, .drop=.drop,
      .fun = function(xx, col) {
        c(N    = length2(xx[[col]], na.rm=na.rm),
          mean = mean   (xx[[col]], na.rm=na.rm),
          sd   = sd     (xx[[col]], na.rm=na.rm),
          p_95 = quantile(xx[[col]], 0.95, na.rm=na.rm),
          p_50 = quantile(xx[[col]], 0.50, na.rm=na.rm),
          p_5 = quantile(xx[[col]], 0.05, na.rm=na.rm)
        )
      },
      measurevar
    )

    # Rename the "mean" column    
    datac <- rename(datac, c("mean" = measurevar))
    
    # Rename the percentile columns
    datac <- rename(datac, c("p_5.5%" = "p_5"))
    datac <- rename(datac, c("p_50.50%" = "p_50"))
    datac <- rename(datac, c("p_95.95%" = "p_95"))

    datac$se <- datac$sd / sqrt(datac$N)  # Calculate standard error of the mean

    # Confidence interval multiplier for standard error
    # Calculate t-statistic for confidence interval: 
    # e.g., if conf.interval is .95, use .975 (above/below), and use df=N-1
    ciMult <- qt(conf.interval/2 + .5, datac$N-1)
    datac$ci <- datac$se * ciMult

    return(datac)
}

# use the ggplot2 library
library(ggplot2)

# read in arguments
args <- commandArgs()
bin_size_ns = as.integer(args[2])

bin_size_ns

data_fp <- read.csv("throughput_std_dev_fastpass.csv", sep=",")
data_fp$experiment = "fastpass"
data_fp$exp_time = (data_fp$start_time - data_fp$start_time[1]) / (1000 * 1000 * 1000) + 30
data_baseline <- read.csv("throughput_std_dev_baseline.csv", sep=",")
data_baseline$experiment = "baseline"
data_baseline$exp_time = (data_baseline$start_time - data_baseline$start_time[1]) / (1000 * 1000 * 1000) + 30

theme_set(theme_bw(base_size=12))

BITS_PER_BYTE = 8
data_fp$std_dev_gbps = data_fp$std_dev * BITS_PER_BYTE / ( bin_size_ns )
data_fp$std_dev_mbps = data_fp$std_dev_gbps * 1000
data_baseline$std_dev_gbps = data_baseline$std_dev * BITS_PER_BYTE / ( bin_size_ns )
data_baseline$std_dev_mbps = data_baseline$std_dev_gbps * 1000

df <- rbind(data_fp, data_baseline)

# plot standard deviations over time
pdf(file="fairness_std_dev_time.pdf", width=10, height=5)
ggplot(df, aes(x=exp_time, y=std_dev_gbps, color=interaction(active_flows, experiment), shape=experiment)) +
           geom_point() +
           labs(x = "Time (s)", y = "Standard deviation of throughput (Gbps)")

# bar chart of meds of std deviations
pdf(file="fairness_std_dev_median.pdf", width=10, height=5)
summary <- summarySE(df, measurevar="std_dev_gbps", groupvars=c("active_flows", "experiment"))

ggplot(summary, aes(x=active_flows, y=p_50, fill=experiment)) +
                geom_bar(stat="identity", position="dodge") +
                labs(x = "Number of connections", y = "Median standard deviation of throughput (Gbps)")

# print out ratio of fastpass median to baseline median (in mbps)
summary_fp <-summarySE(data_fp, measurevar="std_dev_mbps", groupvars="active_flows")
summary_baseline <-summarySE(data_baseline, measurevar="std_dev_mbps", groupvars="active_flows")

summary_both <- data.frame(summary_fp, summary_baseline)
summary_both$mult = summary_both$p_50.1 / summary_both$p_50

summary_both