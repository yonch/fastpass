#!/bin/bash

. env.sh

sudo kpartx -av $VM_HD_IMAGE
sudo mkdir -p $VM_HD_MOUNTPOINT
sudo mount /dev/mapper/loop0p1 $VM_HD_MOUNTPOINT
