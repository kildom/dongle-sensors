#
# USAGE: make [options] [DEBUG=1] [TARGET_TYPE=dongle|host] [GCC_ARM_BIN_DIR=path] [NRFX_DIR=path]
#             [CMSIS_DIR=path] [NRFJPROG_DIR=path] [target]
#
# DEBUG=1          - Build debug version, with debugger information and optimizations disabled.
#                    By default, builds release (optimized) version.
#
# TARGET_TYPE=     - Specify type of target.
#                        dongle - Firmware for temperature measuring dongle (default)
#                        host   - Firmware for communication host
#
# GCC_ARM_BIN_DIR= - Directory containing C compiler "arm-none-eabi-gcc".
#
# NRFX_DIR=        - Directory containing nrfx
#
# CMSIS_DIR=       - Directory containing CMSIS
#
# NRFJPROG_DIR=    - Directory containing nrfjprog
#
# target           - Specify make target:
#                        all         - build the target file (default)
#                        clean       - remove all generated files
#                        cleanobj    - remove intermediate files (e.g. object files)
#                                      and leave only final output
#                        rebuild     - do "clean" and "all"
#                        ???_targets - make "???" target for all targets types and debug versions
#

include $(shell GCC_ARM_BIN_DIR=$(GCC_ARM_BIN_DIR) NRFX_DIR=$(NRFX_DIR) CMSIS_DIR=$(CMSIS_DIR) NRFJPROG_DIR=$(NRFJPROG_DIR) ./deps.sh)

TARGET_TYPE ?= dongle

TARGET_NAME := $(TARGET_TYPE)

ifeq ($(DEBUG),1)
    BUILD_TYPE := debug
else
    BUILD_TYPE := release
endif

OBJ_DIR := obj/$(BUILD_TYPE)/$(TARGET_TYPE)

ALLFLAGS := \
	-g \
	-Wall -Wno-unused \
	-fdata-sections -ffunction-sections -Wl,--gc-sections \
	-fno-strict-aliasing -fshort-enums \
	-mcpu=cortex-m0 -mthumb -mabi=aapcs

CFLAGS := \
	-D__HEAP_SIZE=128 \
	-DNRF51422_XXAC

LDFLAGS := \
	-L../src -L$(NRFX)/mdk \

ifeq ($(BUILD_TYPE),debug)
  CFLAGS += -DDEBUG=1
  ALLFLAGS += -O0
else
  ALLFLAGS += -Os
endif

ifeq ($(TARGET_TYPE),dongle)
  CFLAGS += -D__STACK_SIZE=4096 -DTARGET_TYPE_DONGLE=1
  LDFLAGS += -Tnrf51_xxac-8kRAM.ld
else
  CFLAGS += -D__STACK_SIZE=8192 -DTARGET_TYPE_HOST=1
  LDFLAGS += -Tnrf51_xxac.ld
endif

# Sources
CFLAGS += -I../src -I$(NRFX)/mdk -I$(CMSIS) -I../src/SEGGER_RTT/RTT
OBJ := $(patsubst ../src/%.c,$(OBJ_DIR)/src/%.c.o,$(wildcard ../src/*.c))
OBJ += $(OBJ_DIR)/src/SEGGER_RTT/RTT/SEGGER_RTT.c.o
OBJ += $(OBJ_DIR)/src/SEGGER_RTT/RTT/SEGGER_RTT_printf.c.o
OBJ += $(OBJ_DIR)/nrfx/mdk/gcc_startup_nrf51.S.o
OBJ += $(OBJ_DIR)/nrfx/mdk/system_nrf51.c.o

TARGET := $(BUILD_TYPE)/$(TARGET_NAME).elf
INFO_TARGET := $(OBJ_DIR)/../$(TARGET_NAME)

# make rules

all: $(TARGET)
all_targets:
	+make TARGET_TYPE=dongle DEBUG=0 all
	+make TARGET_TYPE=dongle DEBUG=1 all
	+make TARGET_TYPE=host DEBUG=0 all
	+make TARGET_TYPE=host DEBUG=1 all
targets: all_targets

clean:
	rm -Rf $(OBJ_DIR)
	rm -f $(TARGET)
	rm -f $(patsubst %.elf,%.hex,$@)
clean_targets:
	rm -Rf release
	rm -Rf debug
	rm -Rf obj

cleanobj:
	rm -Rf $(OBJ_DIR)
cleanobj_targets:
	rm -Rf obj

rebuild: clean
	+make all
rebuild_targets:
	+make TARGET_TYPE=dongle DEBUG=0 rebuild
	+make TARGET_TYPE=dongle DEBUG=1 rebuild
	+make TARGET_TYPE=host DEBUG=0 rebuild
	+make TARGET_TYPE=host DEBUG=1 rebuild

$(TARGET): $(OBJ) Makefile
	mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(ALLFLAGS) $(OBJ) -Wl,-Map=$(INFO_TARGET).map -o $@
	$(OBJDUMP) -d $@ > $(INFO_TARGET).lst
	$(OBJCOPY) -O ihex $@ $(patsubst %.elf,%.hex,$@)

$(OBJ_DIR)/src/%.c.o : ../src/%.c Makefile
	mkdir -p $(dir $@)
	$(CC) -MD -c $(CFLAGS) $(ALLFLAGS) $(word 1,$<) -o $@

$(OBJ_DIR)/nrfx/%.S.o : $(NRFX)/%.S Makefile
	mkdir -p $(dir $@)
	$(CC) -MD -c $(CFLAGS) $(ALLFLAGS) $(word 1,$<) -o $@

$(OBJ_DIR)/nrfx/%.c.o : $(NRFX)/%.c Makefile
	mkdir -p $(dir $@)
	$(CC) -MD -c $(CFLAGS) $(ALLFLAGS) $(word 1,$<) -o $@

-include $(patsubst %.o,%.d,$(OBJ))
