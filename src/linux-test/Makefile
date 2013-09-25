# Macros
CC = gcc
CCFLAGS = -g

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: tcp_sender tcp_receiver
clean:
	rm -f tcp_sender tcp_receiver *.o *~

# Dependency rules for file target
tcp_sender: tcp_sender.o
	$(CC) $(CCFLAGS) $< -o $@

tcp_receiver: tcp_receiver.o
	$(CC) $(CCFLAGS) $< -o $@
