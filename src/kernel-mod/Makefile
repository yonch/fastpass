ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := fastpass.o
fastpass-y := sch_fastpass.o fastpass_proto.o ../protocol/fpproto.o


else
# normal makefile
KDIR ?= /home/yonch/huge/linux-build/

default:
	$(MAKE) -C $(KDIR) M=$$PWD KCPPFLAGS="-DFASTPASS_ENDPOINT -DCONFIG_IP_FASTPASS_DEBUG"

clean:
	rm fastpass.o sch_fastpass.o fastpass_proto.o ../protocol/fpproto.o fastpass.ko

test:
	gcc -g -O0 -o tests/window_test tests/window_test.c
	tests/window_test
endif
