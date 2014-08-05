ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := fastpass.o
#obj-m  := simple.o
fastpass-y := sch_timeslot.o sch_fastpass.o fastpass_proto.o ../protocol/fpproto.o compat-3_2.o
simple-y := sch_timeslot.o sch_simple.o


else
# normal makefile

default:
#	$(MAKE) -C $(KDIR) M=$$PWD KCPPFLAGS="-DFASTPASS_ENDPOINT"
	$(MAKE) -C $(KDIR) M=$$PWD KCPPFLAGS="-DFASTPASS_ENDPOINT -DCONFIG_IP_FASTPASS_DEBUG"

clean:
	rm -f fastpass.o sch_fastpass.o sch_timeslot.o fastpass_proto.o ../protocol/fpproto.o fastpass.ko compat-3_2.o

test:
	gcc -g -O0 -o tests/window_test tests/window_test.c
	tests/window_test
endif
