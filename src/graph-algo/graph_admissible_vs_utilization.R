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
# OR
# ./benchmark_sjf > output.csv
#
# This script can be run using:
# R < ./graph_path_selection_vs_utilization.R type --save
# type 0 (default) is for round robin
# type 1 is for shortest remaining job first

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
type = 0 # round-robin
if (length(args) > 2) {
   type <- as.integer(args[2])
}

data <- read.csv("output.csv", sep=",")
attach(data)

# call device driver
if (type == 0) {
   pdf(file="scalability_time_allocation_rr.pdf", width=6.6, height=3)
} else {
   pdf(file="scalability_time_allocation_sjf.pdf", width=6.6, height=3)
}

theme_set(theme_bw(base_size=12))

new_data = data[data$nodes > 128,]

summary <- summarySE(new_data, measurevar="time", groupvars=c("observed_utilization", "nodes"))

ggplot(summary, aes(x=observed_utilization, y=time,
             color=as.factor(nodes), shape=as.factor(nodes))) +
             geom_errorbar(aes(ymin=p_5, ymax=p_95), width=0.01, colour='grey') +
             geom_point() + geom_line() +
             scale_color_discrete(name="Nodes", guide = guide_legend(reverse = TRUE)) +
             scale_shape_manual(name="Nodes", guide = guide_legend(reverse = TRUE), values=c(15, 16, 17, 18)) +
             labs(x = "Network Utilization (%)",
                  y = "Latency (microseconds)") +
             coord_cartesian(xlim=c(0,1))

detach(data)
