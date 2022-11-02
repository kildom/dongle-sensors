# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

file(SHA256 ${CMAKE_CURRENT_LIST_DIR}/src/main.c CHECKSUM_VARIABLE)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_LIST_DIR}/src/hash.h
  COMMAND sha1sum -b ${CMAKE_CURRENT_LIST_DIR}/src/data.h > ${CMAKE_CURRENT_BINARY_DIR}/_tmp.hash1
  COMMAND head -c 16 ${CMAKE_CURRENT_BINARY_DIR}/_tmp.hash1 > ${CMAKE_CURRENT_BINARY_DIR}/_tmp.hash2
  COMMAND echo -n "#define DATA_HASH 0x" > ${CMAKE_CURRENT_LIST_DIR}/src/hash.h
  COMMAND cat ${CMAKE_CURRENT_BINARY_DIR}/_tmp.hash2 >> ${CMAKE_CURRENT_LIST_DIR}/src/hash.h
  COMMAND echo "uLL" >> ${CMAKE_CURRENT_LIST_DIR}/src/hash.h
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/data.h
  VERBATIM)

target_sources(app PRIVATE src/main.c src/multiproto.c src/calendar.c src/radio.c src/ble.c src/blecmd.c src/data.c src/hash.h)
