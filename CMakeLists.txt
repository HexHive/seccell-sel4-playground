#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#
cmake_minimum_required(VERSION 3.7.2)
project(seccell-test C ASM)

# Include settings (variables and environment)
include(settings.cmake)
# Include the actual build system provided by seL4_tools
include(tools/seL4_tools/cmake-tool/all.cmake)

# Name the executable and list source files required to build it
add_executable(
    seccell-test
    src/main.c
    src/alloc.c
    src/scthreads/scthreads.c
    src/scthreads/scthreads.S
)

# Add the global include directory
target_include_directories(seccell-test
    PRIVATE
        include/
)

# List the libraries to link with the application
target_link_libraries(seccell-test
    PUBLIC
        sel4
        muslc
        utils
        sel4muslcsys
        sel4platsupport
        sel4utils
        sel4debug
)

# Tell the build system that this application is the root task
include(rootserver)
DeclareRootserver(seccell-test)

# Generate a simulation script (based on QEMU)
include(simulation)
GenerateSimulateScript()
