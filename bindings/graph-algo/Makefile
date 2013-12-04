# Macros
CC = gcc
CCFLAGS += -g
#CCFLAGS += -DNDEBUG
#CCFLAGS += -O3

# Pattern rule
%.o: ../../src/graph-algo/%.c
	$(CC) $(CCFLAGS) -c $< -fPIC

# Dependency rules for non-file targets
all: swig_graph swig_eulersplit swig_kapoorrizzi swig_structures swig_admissible
clean:
	rm -f *.o *.so *_wrap.c *~ *.py *.pyc

# Dependency rules for file targets
swig_graph:
	swig -python graph.i
	gcc $(CCFLAGS) -fPIC -c graph_wrap.c -o graph_wrap.o -I/usr/include/python2.7
	g++ -shared graph_wrap.o -o _graph.so -lpython2.7 -fPIC

swig_eulersplit: euler_split.o
	swig -python euler_split.i
	gcc $(CCFLAGS) -fPIC -c euler_split_wrap.c -o euler_split_wrap.o -I/usr/include/python2.7
	g++ -shared euler_split_wrap.o -o _eulersplit.so euler_split.o -lpython2.7 -fPIC

swig_kapoorrizzi: kapoor_rizzi.o euler_split.o
	swig -python kapoor_rizzi.i
	gcc $(CCFLAGS) -fPIC -c kapoor_rizzi_wrap.c -o kapoor_rizzi_wrap.o -I/usr/include/python2.7
	g++ -shared kapoor_rizzi_wrap.o -o _kapoorrizzi.so kapoor_rizzi.o euler_split.o -lpython2.7 -fPIC

swig_structures:
	swig -python structures.i
	gcc $(CCFLAGS) -fPIC -c structures_wrap.c -o structures_wrap.o -I/usr/include/python2.7
	g++ -shared structures_wrap.o -o _structures.so -lpython2.7 -fPIC

swig_admissible: admissible_traffic.o
	swig -python admissible.i
	gcc $(CCFLAGS) -fPIC -c admissible_wrap.c -o admissible_wrap.o -I/usr/include/python2.7
	g++ -shared admissible_wrap.o -o _admissible.so admissible_traffic.o -lpython2.7 -fPIC

