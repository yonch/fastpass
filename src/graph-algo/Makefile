# Macros
#CC = gcc
CC = icc
CCFLAGS = -g
CCFLAGS += -DNDEBUG
CCFLAGS += -O3
CCFLAGS += -march=core2
#CCFLAGS += -O0
CCFLAGS += -DNO_DPDK -DALGO_N_CORES=1 
#CCFLAGS += -DPARALLEL_ALGO
CCFLAGS += -DPIPELINED_ALGO
#CCFLAGS += -debug inline-debug-info
LDFLAGS = -lm
#LDFLAGS = -debug inline-debug-info

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: test_euler_split benchmark_graph_algo benchmark_sjf test_bin_computation rdtsc microbench
clean:
	rm -f test_euler_split benchmark_graph_algo benchmark_sjf test_bin_computation rdtsc microbench *.o *~

# Dependency rules for file target
test_euler_split: test_euler_split.o euler_split.o
	$(CC) $< euler_split.o -o $@ $(LDFLAGS)

#benchmark_graph_algo: benchmark_graph_algo.o admissible_traffic.o path_selection.o euler_split.o ../grant-accept/pim_admissible_traffic.o ../grant-accept/pim.o
#	$(CC) $< admissible_traffic.o path_selection.o euler_split.o ../grant-accept/pim_admissible_traffic.o ../grant-accept/pim.o -o $@ $(LDFLAGS)

benchmark_graph_algo: benchmark_graph_algo.o admissible_traffic.o path_selection.o euler_split.o
	$(CC) $< admissible_traffic.o path_selection.o euler_split.o -o $@ $(LDFLAGS)

benchmark_sjf: benchmark_sjf.o admissible_traffic_sjf.o path_selection.o euler_split.o
	$(CC) $< admissible_traffic_sjf.o path_selection.o euler_split.o -o $@ $(LDFLAGS)

test_bin_computation: test_bin_computation.o
	$(CC) $< -o $@ $(LDFLAGS)

microbench: microbench.o
	$(CC) $< -o $@ $(LDFLAGS)

rdtsc: rdtsc.o