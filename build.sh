#!/bin/bash
#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

# Uncomment and fill following lines or provide following variables before
# running this script.
#GCC_ARM_BIN_DIR= #Directory containing C compiler "arm-none-eabi-gcc".
#NRFX_DIR= #Directory containing nrfx
#CMSIS_DIR= #Directory containing CMSIS
#NRFJPROG_DIR= #Directory containing nrfjprog
GCC_ARM_BIN_DIR=/home/doki/work/apps/gcc-arm-none-eabi-10.3-2021.10/bin
CMSIS_DIR=/home/doki/work/ncs2/modules/hal/cmsis

# export BUILD_MODE=BUILD_MODE_DONGLE
# export JPROG_SNR=682325625

# export BUILD_MODE=BUILD_MODE_HOST
# export JPROG_SNR=681548549

TARGET=$BUILD_MODE

build() {
	find_CC
	find_NRFX
	find_CMSIS

	SOURCE_FILES="./src/main.c
		./SEGGER_RTT/RTT/SEGGER_RTT.c
		./SEGGER_RTT/RTT/SEGGER_RTT_printf.c
		$NRFX/mdk/gcc_startup_nrf51.S
		$NRFX/mdk/system_nrf51.c"
	INCLUDE="-I./src -I$NRFX/mdk -I$CMSIS -I./SEGGER_RTT/RTT"
	LIBS="-L./src -L$NRFX/mdk"
	CFLAGS="-Os -g3 -fdata-sections -ffunction-sections -Wl,--gc-sections
		-Wall -fno-strict-aliasing -fshort-enums
		-D__HEAP_SIZE=128 -D__STACK_SIZE=4096 -D$BUILD_MODE
		-mthumb -mabi=aapcs -Tnrf51_xxac-8kRAM.ld
		-mcpu=cortex-m0 -Wno-unused
		-DNRF51422_XXAC"

	echo "Compiling ${TARGET}..."
	$CC $CFLAGS $INCLUDE $LIBS $SOURCE_FILES -Wl,-Map=${TARGET}.map -o ${TARGET}.elf
	$OBJDUMP -d ${TARGET}.elf > ${TARGET}.lst
	$OBJCOPY -O ihex ${TARGET}.elf ${TARGET}.hex
	echo "Success"
}

clean() {
	rm -f ${TARGET}.elf
	rm -f ${TARGET}.lst
	rm -f ${TARGET}.hex
	rm -f ${TARGET}.map
}

flash() {
	find_NRFJPROG
	echo "Flashing ${TARGET}..."
	$NRFJPROG --snr $JPROG_SNR -f NRF51 --program ${TARGET}.hex --sectorerase
	$NRFJPROG --snr $JPROG_SNR -f NRF51 --reset
	echo "Success"
}

find_CC() {
	find_inner() {
		set +e
		$1$2 --version 2> /dev/null > /dev/null
		result=$?
		set -e
		if [ $result == 0 ]; then
			echo "Found compiler at: $1$2"
			echo "Version: $($1$2 --version | head -n 1)"			
			CC=$1$2
			OBJCOPY=${1}objcopy
			OBJDUMP=${1}objdump
			return 1
		else
			return 0
		fi
	}
	trap 'return 0' ERR
	find_inner $GCC_ARM_BIN_DIR/arm-none-eabi- gcc
	find_inner $GCC_ARM_BIN_DIR/bin/arm-none-eabi- gcc
	find_inner arm-none-eabi- gcc
	find_inner $ZEPHYR_SDK_INSTALL_DIR/arm-zephyr-eabi/bin/arm-zephyr-eabi- gcc
	echo "ERROR: Cannot find compiler (arm-none-eabi-gcc)!"
	echo "ERROR: Add its bin directory to your PATH or GCC_ARM_BIN_DIR variable."
	exit 1
}

find_CMSIS() {
	find_inner() {
		if [ -e $1/core_cm33.h ]; then
			echo "Found CMSIS at: $1"
			CMSIS=$1
			return 1
		elif [ -e ${1/CMSIS/cmsis}/core_cm33.h ]; then
			echo "Found CMSIS at: ${1/CMSIS/cmsis}"
			CMSIS=${1/CMSIS/cmsis}
			return 1
		else
			return 0
		fi
	}
	trap 'return 0' ERR
	find_inner $CMSIS_DIR
	find_inner $CMSIS_DIR/Include
	find_inner $CMSIS_DIR/Core/Include
	find_inner $CMSIS_DIR/CMSIS/Core/Include
	find_inner ./CMSIS
	find_inner ./CMSIS/Include
	find_inner ./CMSIS/Core/Include
	find_inner ./CMSIS/CMSIS/Core/Include
	WITH_VER=(./CMSIS/*)
	find_inner $WITH_VER/CMSIS/Core/Include
	WITH_VER=(./CMSIS_*)
	find_inner $WITH_VER/CMSIS/Core/Include
	find_inner $ZEPHYR_BASE/ext/hal/CMSIS/Core/Include
	echo "ERROR: Cannot find CMSIS directory!"
	echo "ERROR: Place it in $(realpath .)/CMSIS or provide path in CMSIS_DIR variable."
	exit 1
}

find_NRFX() {
	find_inner() {
		if [ -e $1 ]; then
			echo "Found nrfx at: $1"
			NRFX=$1
			return 1
		else
			return 0
		fi
	}
	trap 'return 0' ERR
	find_inner $NRFX_DIR _dummy__parameter_
	find_inner ./nrfx
	echo "ERROR: Cannot find nrfx directory!"
	echo "ERROR: Place it in $(realpath .)/nrfx or provide path in NRFX_DIR variable."
	exit 1
}

find_NRFJPROG() {
	find_inner() {
		set +e
		$1 --version 2> /dev/null > /dev/null
		result=$?
		set -e
		if [ $result == 0 ]; then
			echo "Found nrfjprog command at: $1"
			$1 --version
			NRFJPROG=$1
			return 1
		else
			return 0
		fi
	}
	trap 'return 0' ERR
	find_inner $NRFJPROG_DIR/nrfjprog
	find_inner $NRFJPROG_DIR/nrfjprog/nrfjprog
	find_inner nrfjprog
	find_inner /opt/nrfjprog/nrfjprog
	echo "ERROR: Cannot find nrfjprog!"
	echo "ERROR: Add it to your PATH or set NRFJPROG_DIR variable."
	exit 1
}

set -e

if [ "$1" == "flash" ]; then
	flash "$@"
elif [ "$1" == "clean" ]; then
	clean "$@"
else
	build "$@"
fi
