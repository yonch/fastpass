#!/usr/bin/Rscript

library(ggplot2)
library(quantreg)

line_h = 6
line_w = 8

pkt_latency_p <- ggplot() + 
          geom_line(mapping=aes(x=1e6*bin, y=pkt_latency, color=factor(util))) +
          xlab("Packet latency (us)") + 
          ylab("density") +
          guides(color = guide_legend(title="network utilization")) +
          theme_bw()
          
adu_latency_p <- ggplot() + 
          geom_line(mapping=aes(x=1e6*bin, y=adu_latency, color=factor(util))) +
          xlab("ADU latency (us)") + 
          ylab("density") +
          guides(color = guide_legend(title="network utilization")) +
          theme_bw()

adu1_latency_p <- ggplot() + 
          geom_line(mapping=aes(x=1e6*bin, y=adu1_latency, color=factor(util))) +
          xlab("1-packet ADU latency (us)") + 
          ylab("density") +
          guides(color = guide_legend(title="network utilization")) +
          theme_bw()


data = read.csv("results/r1-summary.csv")

ggsave("pkt_latency.pdf", pkt_latency_p %+% data
                                + xlim(0,500)
                                , width=line_w, height=line_h)

ggsave("adu_latency.pdf", adu_latency_p %+% data
                                + xlim(0,2000)
                                , width=line_w, height=line_h)
ggsave("adu1_latency.pdf", adu1_latency_p %+% data
                                + xlim(0,2000)
                                , width=line_w, height=line_h)
                               