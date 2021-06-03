##
# Build rules for the runtime system. Meant to be included from the master
# Makefile.
##

CPPFLAGS += -I $(RUNTIMEDIR)/include

vpath %.c $(RUNTIMEDIR)/src

LIBSRC = peng-scheduler.c peng-int.c peng-bool.c

# This becomes default goal if none are specified by the platform
libpeng.a : libpeng.a($(LIBSRC:%.c=%.o))
