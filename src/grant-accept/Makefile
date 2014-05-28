# Macros
#CC = gcc
CCFLAGS = -g
CCFLAGS += -DNDEBUG
CCFLAGS += -O3
#CCFLAGS += -O1
CCFLAGS += -DNO_DPDK
CCFLAGS += -DSINGLE_CORE
#CCFLAGS += -debug inline-debug-info
LDFLAGS = -lm
#LDFLAGS = -debug inline-debug-info

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: pim
clean:
	rm -f pim *.o *~

# Dependency rules for file targets
pim: pim_test.o pim_admissible_traffic.o pim.o
	$(CC) $< pim_admissible_traffic.o pim.o -o $@ $(LDFLAGS)
