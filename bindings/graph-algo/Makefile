# Macros
CC = gcc
CCFLAGS = -g
#CCFLAGS = -DNDEBUG

# Pattern rule
%.o: ../../src/graph-algo/%.c
	$(CC) $(CCFLAGS) -c $< -fPIC

# Dependency rules for non-file targets
all: swig_graph swig_eulersplit swig_kapoorrizzi
clean:
	rm -f *.o *.so *_wrap.c *~ *.py *.pyc

# Dependency rules for file targets
swig_graph:
	swig -python graph.i
	gcc -fPIC -c graph_wrap.c -o graph_wrap.o -I/usr/include/python2.7
	g++ -shared graph_wrap.o -o _graph.so -lpython2.7 -fPIC

swig_eulersplit: euler_split.o
	swig -python euler_split.i
	gcc -fPIC -c euler_split_wrap.c -o euler_split_wrap.o -I/usr/include/python2.7
	g++ -shared euler_split_wrap.o -o _eulersplit.so euler_split.o -lpython2.7 -fPIC

swig_kapoorrizzi: kapoor_rizzi.o euler_split.o
	swig -python kapoor_rizzi.i
	gcc -fPIC -c kapoor_rizzi_wrap.c -o kapoor_rizzi_wrap.o -I/usr/include/python2.7
	g++ -shared kapoor_rizzi_wrap.o -o _kapoorrizzi.so kapoor_rizzi.o euler_split.o -lpython2.7 -fPIC
