#!/bin/bash

CONTROLLER_IP="192.168.44.100"
#DEV="eth0"

#CONTROLLER_IP="1.1.2.100"
DEV="eth5"

TC="/home/am2/yonch/tc"

./del_tc.sh

insmod fastpass.ko fastpass_debug=1
echo -- lsmod after --
lsmod | grep fastpass
echo -----------------

TEXT=`cat /sys/module/fastpass/sections/.text`
DATA=`cat /sys/module/fastpass/sections/.data`
BSS=`cat /sys/module/fastpass/sections/.bss`
echo add-symbol-file /home/yonch/fastpass/src/kernel-mod/fastpass.ko $TEXT -s .data $DATA -s .bss $BSS

sudo $TC qdisc add dev $DEV root fastpass timeslot 1000000 req_cost 2000000 req_bucket 6000000 ctrl $CONTROLLER_IP rate 12500Kbps
