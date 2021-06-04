##
# Build rules for the runtime system. Meant to be included from the master
# Makefile.
##

CPPFLAGS += -I $(RUNTIMEDIR)/include
LDFLAGS += -lpeng

vpath %.c $(RUNTIMEDIR)/src

RUNTIMESRC := peng-scheduler.c peng-int.c peng-bool.c

SRCS += $(RUNTIMESRC)

libpeng.a : libpeng.a($(RUNTIMESRC:%.c=%.o))
