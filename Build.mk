##
# Build rules for the runtime system. Meant to be included from the master
# Makefile.
##

CPPFLAGS += -I $(RUNTIMEDIR)/include
LDLIBS += -lpeng

vpath %.c $(RUNTIMEDIR)/src

RUNTIMESRC := ssm-queue.c ssm-sched.c ssm-types.c

SRCS += $(RUNTIMESRC)
LIBS += libpeng.a

libpeng.a : libpeng.a($(RUNTIMESRC:%.c=%.o))
