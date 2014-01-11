# Based on the DPDK Programmer Guide November 2012 (Ch 15.3.1)

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = fast

SRCS-y := main.c \
          control.c \
          comm_core.c \
          admission_core.c \
          path_sel_core.c \
          ../protocol/fpproto.c \
          ../graph-algo/admissible_traffic.c \
          ../graph-algo/path_selection.c \
          ../graph-algo/euler_split.c

CFLAGS += -O3 
#CFLAGS += $(WERROR_FLAGS)
CFLAGS += -DFASTPASS_CONTROLLER 
#CFLAGS += -DCONFIG_IP_FASTPASS_DEBUG
CFLAGS += -DLOG_TO_STDOUT 
CFLAGS += -I${PWD} 
#CFLAGS += -g -O1
CFLAGS += -g 
#CFLAGS += -DNDEBUG

include $(RTE_SDK)/mk/rte.extapp.mk
