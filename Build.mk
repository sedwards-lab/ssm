##
# Build rules for the runtime system. Meant to be included from the master
# Makefile.
##

CPPFLAGS += -I $(RUNTIMEDIR)/include
LDLIBS += -lssm

vpath %.c $(RUNTIMEDIR)/src

RUNTIMESRC := ssm-queue.c ssm-sched.c ssm-types.c

SRCS += $(RUNTIMESRC)
LIBS += libssm.a

libssm.a : libssm.a($(RUNTIMESRC:%.c=%.o))
