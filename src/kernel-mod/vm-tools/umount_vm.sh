#!/bin/bash

. ~/fastpass_env.sh

sudo umount $VM_HD_MOUNTPOINT
sudo kpartx -d $VM_HD_IMAGE
