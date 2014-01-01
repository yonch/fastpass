# Macros
CC = gcc
#CC = /mnt/sdb1/bin/icc
CCFLAGS = -g
#CCFLAGS += -DNDEBUG
#CCFLAGS += -O3
CCFLAGS += -O1
CCFLAGS += -DNO_DPDK 
#CCFLAGS += -debug inline-debug-info
LDFLAGS = -lm
#LDFLAGS = -debug inline-debug-info

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: test_euler_split test_admissible
clean:
	rm -f test_euler_split test_admissible *.o *~

# Dependency rules for file target
test_euler_split: test_euler_split.o euler_split.o
	$(CC) $< euler_split.o -o $@ $(LDFLAGS)

test_admissible: test_admissible.o admissible_traffic.o
	$(CC) $< admissible_traffic.o -o $@ $(LDFLAGS)
