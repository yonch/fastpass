#!/bin/bash

# insert IGB_UIO driver
insmod ~/src/DPDK/x86_64-default-linuxapp-gcc/kmod/igb_uio.ko


# set up hugepages
cd /tmp
	echo "Creating /mnt/huge and mounting as hugetlbfs"
	sudo mkdir -p /mnt/huge

	grep -s '/mnt/huge' /proc/mounts > /dev/null
	if [ $? -ne 0 ] ; then
		sudo mount -t hugetlbfs nodev /mnt/huge
	fi

	Pages=256
	echo "echo $Pages > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages" > .echo_tmp

	echo "Reserving hugepages"
	sudo sh .echo_tmp
	rm -f .echo_tmp

# bind DPDK port (eth11)
cd ~/fastpass/src/controller
sudo ./pci_bind.py +2

# start passive shell (eth6)
cd ~/fastpass/src/router_conf
sudo ./4.sh
sudo ./4.sh

# configure interface to the controller (eth9)
sudo ifconfig eth9 1.1.2.20 netmask 255.255.255.0

# start active shell (eth5)
cd ~/fastpass/src/router_conf
sudo ./3.sh
sudo ./3.sh
