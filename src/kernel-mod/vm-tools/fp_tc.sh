#!/bin/bash

insmod fastpass.ko fastpass_debug=1
TEXT=`cat /sys/module/fastpass/sections/.text`
DATA=`cat /sys/module/fastpass/sections/.data`
BSS=`cat /sys/module/fastpass/sections/.bss`
echo add-symbol-file /home/yonch/fastpass/src/kernel-mod/fastpass.ko $TEXT -s .data $DATA -s .bss $BSS

sudo tc qdisc del dev eth0 root
sudo tc qdisc add dev eth0 root fastpass timeslot 1000000 req_cost 2000000 req_bucket 6000000 ctrl 192.168.122.1 rate 12500Kbps
