# Macros
CC = gcc
CCFLAGS = -g
LDFLAGS = -lm -pthread

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: tcp_experiment rdtsc
clean:
	rm -f tcp_experiment rdtsc *.o *~

# Dependency rules for file target
tcp_experiment: tcp_experiment.o tcp_sender.o tcp_receiver.o generate_packets.o
	$(CC) $< tcp_sender.o tcp_receiver.o generate_packets.o -o $@ $(LDFLAGS)

rdtsc: rdtsc.o