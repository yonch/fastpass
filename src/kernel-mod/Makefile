ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := fastpass.o
fastpass-y := sch_fastpass.o fastpass_proto.o fpproto.o


else
# normal makefile
KDIR ?= /home/yonch/huge/linux-build/

default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	rm fastpass.o sch_fastpass.o fastpass_proto.o fpproto.o fastpass.ko

test:
	gcc -g -O0 -o tests/window_test tests/window_test.c
	tests/window_test
endif
