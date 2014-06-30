# 
# Created on: June 5, 2014
# Author: aousterh
#
# This script generates CDFs of bytes achieved during different
# portions of the fairness experiment.
#
# This script can be run using:
# R < ./graph_fairness_throughput_cdf.R --save

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
          p_5 = quantile(xx[[col]], 0.05, na.rm=na.rm)
        )
      },
      measurevar
    )

    # Rename the "mean" column    
    datac <- rename(datac, c("mean" = measurevar))
    
    # Rename the percentile columns
    datac <- rename(datac, c("p_5.5%" = "p_5"))
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

data_fp <- read.csv("fairness_throughput_fastpass.csv", sep=",")
data_fp$experiment = "fastpass"
data_baseline <- read.csv("fairness_throughput_baseline.csv", sep=",")
data_baseline$experiment = "baseline"

theme_set(theme_bw(base_size=12))

BITS_PER_BYTE = 8
data_fp$gbps = data_fp$bytes * BITS_PER_BYTE / ( bin_size_ns )
data_baseline$gbps = data_baseline$bytes * BITS_PER_BYTE / ( bin_size_ns )

df <- rbind(data_fp, data_baseline)

# call device driver
pdf(file="fairness_throughput_cdf.pdf", width=10, height=5)

# plot
ggplot(df, aes(gbps, color=active_flows, linetype=experiment)) + stat_ecdf()
