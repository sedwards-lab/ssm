# Master Makefile for Peng libaries/binaries
#
# Mean to be included by a Platform.mk

# Used to build local tools
HOSTCC = cc

# Running the Peng compiler

%.c : %.pen
	$(PENG) --module-name=$(MODULE) --generate-c $< > $@

%.h : %.pen
	$(PENG) --module-name=$(MODULE) --generate-h $< > $*.h

# Compiling C files

DEPFLAGS = -MT $@ -MMD -MP -MF $(*F).d

%.o : %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<


# Archive rules

ARFLAGS = -crU

(%) : %
	$(AR) $(ARFLAGS) $@ $%

# .elf file generation rules

%.elf :
	$(CC) $(LDFLAGS) -o $@ $^

# Hex file generation rules

%.hex : %.elf
	$(OBJCOPY) -O ihex $^ $@

.PHONY : clean
clean :
	rm -rf $(TOCLEAN)


# Rules for compiling the base library

vpath %.c $(PENGLIB)/src

CPPFLAGS += -I $(PENGLIB)/include

LIBSRC += peng-scheduler.c peng-int.c peng-bool.c
LIBOBJS = $(LIBSRC:%.c=%.o)

libpeng.a : libpeng.a($(LIBOBJS))

TOCLEAN += $(LIBOBJS) $(LIBSRC:%.c=%.d) libpeng.a


.PRECIOUS: %.c %.o %.a

-include $(wildcard *.d)
