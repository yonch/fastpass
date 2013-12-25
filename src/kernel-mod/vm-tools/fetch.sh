#!/bin/bash

tc qdisc del dev eth0 root
rmmod fastpass
scp -P947 yonch@192.168.122.1:fastpass/src/kernel-mod/fastpass.ko .
