ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := fastpass.o
fastpass-y := sch_fastpass.o fastpass_proto.o


else
# normal makefile
KDIR ?= /home/yonch/huge/linux-build/

default:
	$(MAKE) -C $(KDIR) M=$$PWD
endif
