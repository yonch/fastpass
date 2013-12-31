#!/bin/bash

./del_tc.sh
./clear_logs.sh
./fp_tc.sh
ping -c1 -W2 4.2.2.4
tc -s qdisc



