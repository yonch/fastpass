# Macros
CC = gcc
CCFLAGS = -g -lm

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: tcp_sender tcp_receiver
clean:
	rm -f tcp_sender tcp_receiver *.o *~

# Dependency rules for file target
tcp_sender: tcp_sender.o generate_packets.o
	$(CC) $(CCFLAGS) $< generate_packets.o -o $@

tcp_receiver: tcp_receiver.o generate_packets.o
	$(CC) $(CCFLAGS) $< generate_packets.o -o $@
