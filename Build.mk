##
# Build rules for the runtime system. Meant to be included from the master
# Makefile.
##

CPPFLAGS += -I $(RUNTIMEDIR)/include
LDLIBS += -lssm

vpath %.c $(RUNTIMEDIR)/src
vpath %.c $(RUNTIMEDIR)/test

RUNTIMESRC := ssm-queue.c ssm-sched.c ssm-types.c
TESTSRC := test-ssm-queue.c

SRCS += $(RUNTIMESRC) $(TESTSRC)
LIBS += libssm.a

libssm.a : libssm.a($(RUNTIMESRC:%.c=%.o))

test-ssm-queue:
