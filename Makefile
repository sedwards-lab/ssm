CC = gcc
CFLAGS = -g -Wall -pedantic -std=c99 # -DDEBUG

EXE = fib fib2 fib3 counter counter2 clock onetwo
obj_EXE = $(foreach e, $(EXE), $(e).o)

SSMLIB = ssm-types ssm-queue ssm-sched
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

$(obj_EXE) $(obj_SSMLIB) : ssm-act.h ssm-core.h ssm-queue.h ssm-runtime.h ssm-sv.h ssm-types.h

$(EXE): %: %.o $(obj_SSMLIB)

ssm-queue-test: ssm-queue.o

ssm-queue-test.o : ssm-queue.h ssm-queue-test.h
