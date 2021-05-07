CC = gcc
CFLAGS = -g -Wall -pedantic -std=c99

EXE = fib fib2 fib3 counter counter2 clock onetwo
obj_EXE = $(foreach e, $(EXE), $(e).o)
SSMLIB = scheduler types
obj_SSMLIB = $(foreach e, $(SSMLIB), $(e).o)

all : $(EXE)

$(obj_EXE) $(obj_SSMLIB) : ssm.h

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

compile_commands.json: Makefile
	bear make all

.PHONY : clean
clean :
	rm -rf *.o *.gch $(EXE)
