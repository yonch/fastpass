# Macros
CC = gcc
CCFLAGS = -g

# Pattern rule
%.o: ../../src/graph-algo/%.c
	$(CC) $(CCFLAGS) -c $< -fPIC

# Dependency rules for non-file targets
all: swig_graph
clean:
	rm -f *.o *.so *_wrap.c *~ *.py *.pyc

# Dependency rules for file targets
swig_graph:
	swig -python graph.i
	gcc -fPIC -c graph_wrap.c -o graph_wrap.o -I/usr/include/python2.7
	g++ -shared graph_wrap.o -o _graph.so -lpython2.7 -fPIC
