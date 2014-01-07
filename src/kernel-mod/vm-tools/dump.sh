#!/bin/bash

OUTDIR=./pcap
sudo date +%Y%m%d_%H%M%S_%N
FNAME_DATE=`date +%Y%m%d_%H%M%S_%N`
FNAME=$OUTDIR/fp_$FNAME_DATE.pcap

mkdir -p $OUTDIR
chmod +x $OUTDIR
sudo tcpdump -i eth5 "ip proto 222" -n -p -w $FNAME
