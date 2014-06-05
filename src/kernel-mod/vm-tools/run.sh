#!/bin/sh

. ~/fastpass_env.sh

./mount_vm.sh
sleep 0.5

sudo cp ~/fastpass/src/kernel-mod/fastpass.ko $VM_HD_MOUNTPOINT/root
sudo cp ~/fastpass/src/kernel-mod/vm-tools/* $VM_HD_MOUNTPOINT/root
sudo rm $VM_HD_MOUNTPOINT/sbin/tc
sudo rm $VM_HD_MOUNTPOINT/root/tc
sudo cp ~/src/iproute2.git/tc/tc $VM_HD_MOUNTPOINT/sbin
#sudo cat /dev/null > $VM_HD_MOUNTPOINT/var/log/syslog
#sudo cat /dev/null > $VM_HD_MOUNTPOINT/var/log/kern.log
#sudo cat /dev/null > $VM_HD_MOUNTPOINT/var/log/debug

sync;

./umount_vm.sh
sync;
sleep 0.5

sudo qemu-system-x86_64 -m 1G -hda $VM_HD_IMAGE  -kernel $KBUILD_OUTPUT/arch/x86_64/boot/bzImage -append "root=/dev/sda1 console=ttyS0,115200n8 fastpass.fastpass_debug=1" -s -nographic -netdev tap,id=mynet0,ifname=tap0 -device rtl8139,netdev=mynet0
#qemu-system-x86_64 -m 1G -hda linux.img  -kernel arch/x86_64/boot/bzImage -append "root=/dev/sda1 console=ttyS0" -s -serial stdio -vga std -chardev vc,id=a,width=1024,height=960,cols=140,rows=50
