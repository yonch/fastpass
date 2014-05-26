#!/bin/bash

DEV="eth0"
#DEV="eth5"
#TC="/home/am2/yonch/tc"
TC="/sbin/tc"

sudo $TC qdisc del dev $DEV root
sudo rmmod fastpass
echo -- lsmod empty --
sudo lsmod | grep fastpass
echo -----------------
