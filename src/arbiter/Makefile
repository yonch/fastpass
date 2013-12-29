# Based on the DPDK Programmer Guide November 2012 (Ch 15.3.1)

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = fast

SRCS-y := main.c generate.c node.c control.c traffic_gen.c cpu_benchmark.c \
	controller.c rx_grant.c\
	../allocator/fast_path_allocator.c ../allocator/network_topology.c

CFLAGS += -O3 
CFLAGS += $(WERROR_FLAGS)
#CFLAGS += -g -O1
CFLAGS += -DNDEBUG

include $(RTE_SDK)/mk/rte.extapp.mk
