all: cluster_head
CONTIKI=../../..

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifdef SERVER_REPLY
CFLAGS += -DSERVER_REPLY=$(SERVER_REPLY)
endif
ifdef PERIOD
CFLAGS += -DPERIOD=$(PERIOD)
endif

CONTIKI_WITH_IPV6 = 1
MAKE_ROUTING = MAKE_ROUTING_RPL_CLASSIC
include $(CONTIKI)/Makefile.include
