# Teensy 4.0 Platform make rules; include this in a program's makefile

# The Teensy 4.0's IMXRT1062 has an ARM Cortex M7 processor with hardware
# floating-point in the form of the VFPv5 architecture (Double-precision)
# and support for the Thumb ISA
CPUOPTIONS = -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16 -mthumb

# -ffunction-sections
# -fdata-sections
#     Puts everything in its own section in the elf file.
#     Not clear it's useful
CFLAGS += -Wall -O2 -std=gnu99 $(CPUOPTIONS) -ffunction-sections -fdata-sections

LDFLAGS += -Os -Wl,--gc-sections,--relax $(CPUOPTIONS) -T$(PENGLIB)/teensy40/imxrt1062.ld

CC = arm-none-eabi-gcc
AR = arm-none-eabi-ar
OBJCOPY = arm-none-eabi-objcopy
PENG = peng
LOADER = ./teensy_loader_cli
LOADERFLAGS = --mcu=imxrt1062

CPPFLAGS += -I $(PENGLIB)/teensy40/include

vpath %.c $(PENGLIB)/teensy40/src

LIBSRC += startup.c

libpeng.a(startup.o) : startup.o

.PHONY : program

program : $(MODULE).hex $(LOADER)
	@echo "Press the key on the Teensy"
	$(LOADER) $(LOADERFLAGS) -w $(MODULE).hex

reboot : $(LOADER)
	@echo "Press the key on the Teensy"
	$(LOADER) $(LOADERFLAGS) -w -b

$(MODULE).hex : $(MODULE).elf

teensy_loader_cli : teensy_loader_cli.c
	$(HOSTCC) -O2 -Wall -s -DUSE_LIBUSB -o teensy_loader_cli $< -lusb

TOCLEAN += teensy_loader_cli

include $(PENGLIB)/Peng.mk
