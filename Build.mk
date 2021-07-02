##
# Build rules for the runtime system. Meant to be included from the master
# Makefile.
##

CPPFLAGS += -I $(RUNTIMEDIR)/include
LDLIBS += -lssm

ifdef ACT_QUEUE_SIZE
CPPFLAGS += -DACT_QUEUE_SIZE=$(ACT_QUEUE_SIZE)
endif

ifdef EVENT_QUEUE_SIZE
CPPFLAGS += -DEVENT_QUEUE_SIZE=$(EVENT_QUEUE_SIZE)
endif

vpath %.c $(RUNTIMEDIR)/src
vpath %.c $(RUNTIMEDIR)/test

RUNTIMESRC := ssm-queue.c ssm-sched.c ssm-types.c
TESTSRC := test-ssm-queue.c

SRCS += $(RUNTIMESRC) $(TESTSRC)
LIBS += libssm.a

libssm.a : libssm.a($(RUNTIMESRC:%.c=%.o))

test-ssm-queue:
