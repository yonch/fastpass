# 
# Created on: March 16, 2014
# Author: aousterh
#
# This script generates a graph of num admitted vs. network
# utilization for several different numbers of nodes.
#
# Before running this script, generate the appropriate csv file:
# ./benchmark_graph_algo 2 > output.csv
#
# This script can be run using:
# R < ./graph_admitted_vs_utilization.R --save

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

data <- read.csv("output.csv", sep=",")

# call device driver
pdf(file="scalability_num_admitted.pdf", width=6.6, height=3)

theme_set(theme_bw(base_size=12))

MACHINES_PER_RACK = 32
data$nodes = data$num_racks * MACHINES_PER_RACK

summary <- summarySE(data, measurevar="num_admitted", groupvars=c("target_utilization", "nodes"))

ggplot(summary, aes(x=target_utilization, y=num_admitted,
             color=as.factor(nodes), shape=as.factor(nodes))) +
             geom_abline(intercept = 0, slope = 1024, linetype = 2) +
             geom_abline(intercept = 0, slope = 512, linetype = 2) +
             geom_abline(intercept = 0, slope = 256, linetype = 2) +
             geom_abline(intercept = 0, slope = 128, linetype = 2) +
             geom_errorbar(aes(ymin=p_5, ymax=p_95), width=0.01, colour='grey') +
             geom_line() + geom_point() +
             scale_color_manual(name="Nodes", guide = guide_legend(reverse = TRUE),
                                values=c("#A1DAB4", "#41B6C4", "#2C7FB8", "#253494")) +
             scale_shape_manual(name="Nodes", guide = guide_legend(reverse = TRUE), values=c(15, 16, 17, 18)) +
             labs(x = "Network Utilization (%)",
                  y = "Number of Timeslots Filled") +
             scale_y_continuous(breaks=c(128, 256, 512, 1024))
