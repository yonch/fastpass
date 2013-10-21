# Macros
CC = gcc
CCFLAGS = -g
LDFLAGS = -lm

# Pattern rule
%.o: %.c
	$(CC) $(CCFLAGS) -c $<

# Dependency rules for non-file targets
all: test_euler_split
clean:
	rm -f test_euler_split *.o *~

# Dependency rules for file target
test_euler_split: test_euler_split.o euler_split.o
	$(CC) $< euler_split.o -o $@ $(LDFLAGS)
