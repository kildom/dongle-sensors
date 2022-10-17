#!/bin/bash
#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#


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
	find_inner ../CMSIS
	find_inner ../CMSIS/Include
	find_inner ../CMSIS/Core/Include
	find_inner ../CMSIS/CMSIS/Core/Include
	WITH_VER=(../CMSIS/*)
	find_inner $WITH_VER/CMSIS/Core/Include
	WITH_VER=(../CMSIS_*)
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
	find_inner ../nrfx
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


if [ "$1" == "_do_actual_job" ]; then
	set -e
	find_CC
	find_CMSIS
	find_NRFX
	find_NRFJPROG
	echo \# Auto-generated file > obj/deps.mk
	echo CC:=$CC >> obj/deps.mk
	echo OBJCOPY:=$OBJCOPY >> obj/deps.mk
	echo OBJDUMP:=$OBJDUMP >> obj/deps.mk
	echo CMSIS:=$CMSIS >> obj/deps.mk
	echo NRFX:=$NRFX >> obj/deps.mk
	echo NRFJPROG:=$NRFJPROG >> obj/deps.mk
else
	./deps.sh _do_actual_job > obj/deps-results.txt 2>&1
	if [ $? -ne 0 ]; then
		echo '$(info $(file < obj/deps-results.txt))' > obj/deps.mk
		echo '$(error Dependencies detection failed)' >> obj/deps.mk
	fi
	echo obj/deps.mk
fi
