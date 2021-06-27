CC = gcc

TIME_DRIVER ?= simulation

ifeq ($(TIME_DRIVER), linux)
	CSTANDARD = gnu99
else
	CSTANDARD = c99
endif

CFLAGS = -g -Wall -pedantic -std=$(CSTANDARD) # -DDEBUG


EXE = fib fib2 fib3 counter counter2 clock onetwo onetwo-io simpleadd
obj_EXE = $(foreach e, $(EXE), $(e).o)

SSMLIB = ssm-types ssm-io ssm-queue ssm-sched $(TIME_DRIVER)-time
obj_SSMLIB = $(foreach e, $(SSMLIB), $(e).o)

TESTS = ssm-queue-test
obj_TESTS = $(foreach e, $(TESTS), $(e).o)


.PHONY: default tests all clean
default: $(EXE)
tests: $(TESTS)
all : clean default tests
clean :
	rm -rf *.o *.gch vgcore.* $(EXE) $(TESTS)

compile_commands.json: Makefile
	bear make all

$(obj_EXE) $(obj_SSMLIB) : ssm-act.h ssm-core.h ssm-queue.h ssm-runtime.h ssm-sv.h ssm-types.h ssm-time-driver.h

$(EXE): %: %.o $(obj_SSMLIB)

onetwo-io : onetwo-io.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

one-read-two : one-read-two.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^


ssm-queue-test: ssm-queue.o

ssm-queue-test.o : ssm-queue.h ssm-queue-test.h
