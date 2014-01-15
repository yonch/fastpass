# Macros
CC = gcc
#CC = /mnt/sdb1/bin/icc
CCFLAGS = -g
#CCFLAGS += -DNDEBUG
#CCFLAGS += -O3
CCFLAGS += -O1
CCFLAGS += -DNO_DPDK 
CCFLAGS += -DFASTPASS_CONTROLLER
#CCFLAGS += -debug inline-debug-info
#LDFLAGS = -debug inline-debug-info

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: log_print
clean:
	rm -f log_print *.o *~

# Dependency rules for file target
log_print: log_print.o
	$(CC) $< -o $@ $(LDFLAGS)
