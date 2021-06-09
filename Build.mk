##
# Build rules for the runtime system. Meant to be included from the master
# Makefile.
##

CPPFLAGS += -I $(RUNTIMEDIR)/include
LDLIBS += -lpeng

vpath %.c $(RUNTIMEDIR)/src

RUNTIMESRC := peng-scheduler.c peng-bool.c peng-int64.c peng-int.c peng-uint8.c

SRCS += $(RUNTIMESRC)
LIBS += libpeng.a

libpeng.a : libpeng.a($(RUNTIMESRC:%.c=%.o))
