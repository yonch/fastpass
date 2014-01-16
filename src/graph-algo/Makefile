# Macros
CC = gcc
#CC = /mnt/sdb1/bin/icc
CCFLAGS = -g
CCFLAGS += -DNDEBUG
CCFLAGS += -O3
#CCFLAGS += -O1
CCFLAGS += -DNO_DPDK 
#CCFLAGS += -debug inline-debug-info
LDFLAGS = -lm
#LDFLAGS = -debug inline-debug-info

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: test_euler_split benchmark_graph_algo benchmark_sjf test_bin_computation rdtsc
clean:
	rm -f test_euler_split benchmark_graph_algo benchmark_sjf test_bin_computation rdtsc *.o *~

# Dependency rules for file target
test_euler_split: test_euler_split.o euler_split.o
	$(CC) $< euler_split.o -o $@ $(LDFLAGS)

benchmark_graph_algo: benchmark_graph_algo.o admissible_traffic.o path_selection.o euler_split.o
	$(CC) $< admissible_traffic.o path_selection.o euler_split.o -o $@ $(LDFLAGS)

benchmark_sjf: benchmark_sjf.o admissible_traffic_sjf.o path_selection.o euler_split.o
	$(CC) $< admissible_traffic_sjf.o path_selection.o euler_split.o -o $@ $(LDFLAGS)

test_bin_computation: test_bin_computation.o
	$(CC) $< -o $@ $(LDFLAGS)

rdtsc: rdtsc.o