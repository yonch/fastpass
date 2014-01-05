#!/bin/bash

#DEV="eth0"
DEV="eth5"

sudo tc qdisc del dev $DEV root
sudo rmmod fastpass
echo -- lsmod empty --
sudo lsmod | grep fastpass
echo -----------------
