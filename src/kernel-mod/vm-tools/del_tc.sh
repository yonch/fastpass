#!/bin/bash

sudo tc qdisc del dev eth0 root
sudo rmmod fastpass
echo -- lsmod empty --
sudo lsmod
echo -----------------
