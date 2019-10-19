all: client
CONTIKI = ../../..

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifdef SERVER_REPLY
CFLAGS += -DSERVER_REPLY=$(SERVER_REPLY)
endif
ifdef PERIOD
CFLAGS += -DPERIOD=$(PERIOD)
endif

UIP_CONF_ROUTER=0
UIP_CONF_IPV6_RPL=0
include $(CONTIKI)/Makefile.include
