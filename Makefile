CC = gcc
CFLAGS = -g -Wall -pedantic # -DDEBUG

TIME_DRIVER ?= simulation

EXE = fib fib2 fib3 counter counter2 clock onetwo one-read-two
obj_EXE = $(foreach e, $(EXE), $(e).o)

SSMLIB = ssm-types ssm-queue ssm-sched $(TIME_DRIVER)-time
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

$(obj_EXE) $(obj_SSMLIB) : ssm-act.h ssm-core.h ssm-queue.h ssm-runtime.h ssm-sv.h ssm-types.h time-driver.h

fib : fib.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

fib2 : fib2.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

fib3 : fib3.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

counter : counter.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

counter2 : counter2.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

clock : clock.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

onetwo : onetwo.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^

one-read-two : one-read-two.o $(obj_SSMLIB)
	$(CC) $(CFLAGS) -o $@ $^


ssm-queue-test: ssm-queue.o

ssm-queue-test.o : ssm-queue.h ssm-queue-test.h
