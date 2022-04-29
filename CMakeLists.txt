#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#
cmake_minimum_required(VERSION 3.7.2)
project(seL4-playground C ASM)

# Include settings (variables and environment)
include(settings.cmake)
# Include the actual build system provided by seL4_tools
include(tools/seL4_tools/cmake-tool/all.cmake)
# Include configuration options
include(config.cmake)

# Include SecureCells-specific source files and libraries
if(KernelSecCell)
    set(sc_libs
        ${sc_libs}
        seL4seccells
    )
else()
    set(sc_libs
        ${sc_libs}
    )
endif()

# Name the executable and list source files required to build it
add_executable(
    seL4-playground
    src/main.c
    src/alloc.c
    src/mmap_override.c
)

# Add the global include directory
target_include_directories(seL4-playground
    PRIVATE
        include/
)

# List the libraries to link with the application
target_link_libraries(seL4-playground
    PUBLIC
        sel4
        muslc
        utils
        sel4muslcsys
        sel4platsupport
        sel4utils
        sel4debug
        sel4_Config
        sel4_autoconf
        seL4-playground_Config
        ${sc_libs}
)

# Tell the build system that this application is the root task
include(rootserver)
DeclareRootserver(seL4-playground)

# Generate a simulation script (based on QEMU)
include(simulation)
GenerateSimulateScript()
