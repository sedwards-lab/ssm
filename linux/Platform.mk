# Linux platform make rules; include this in a program's Makefile

CFLAGS += -std=c99 -Wall -O2 -g -pedantic
CC = gcc
AR = ar
PENG = peng

CPPFLAGS += -I $(PENGLIB)/linux/include

CFLAGS += -pedantic

include $(PENGLIB)/Peng.mk


