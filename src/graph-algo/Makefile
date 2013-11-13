# Macros
#CC = gcc
CC = /mnt/sdb1/bin/icc
CCFLAGS = -g
CCFLAGS += -DNDEBUG
CCFLAGS += -O3
LDFLAGS =

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
