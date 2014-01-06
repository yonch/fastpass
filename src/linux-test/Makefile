# Macros
CC = gcc
CCFLAGS = -g
LDFLAGS = -lm -pthread -lrt

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: tcp_experiment
clean:
	rm -f tcp_experiment *.o *~

# Dependency rules for file target
tcp_experiment: tcp_experiment.o tcp_sender.o tcp_receiver.o generate_packets.o
	$(CC) $< tcp_sender.o tcp_receiver.o generate_packets.o -o $@ $(LDFLAGS)
