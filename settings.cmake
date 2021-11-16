#
# Copyright 2018, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(DATA61_BSD)
#

include_guard(GLOBAL)


set(project_dir "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB project_modules ${project_dir}/projects/*)
list(
    APPEND
    CMAKE_MODULE_PATH
    ${project_dir}/kernel
    ${project_dir}/tools/seL4_tools/cmake-tool/helpers/
    ${project_dir}/tools/seL4_tools/elfloader-tool/
    ${project_modules}
    )

include(application_settings)

set(RISCV64 ON CACHE BOOL "" FORCE)
set(KernelArch "riscv" CACHE STRING "" FORCE)
set(KernelSel4Arch "riscv64" CACHE STRING "" FORCE)
set(KernelPlatform "spike" CACHE STRING "" FORCE)
set(KernelRiscvExtD ON CACHE BOOL "" FORCE)
set(KernelRiscvExtF ON CACHE BOOL "" FORCE)
set(KernelSecCell ON CACHE BOOL "" FORCE)
set(KernelResetChunkBits "12" CACHE STRING "" FORCE)
set(KernelOptimisation "-O0" CACHE STRING "" FORCE)
ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})

include(${project_dir}/kernel/configs/seL4Config.cmake)
set(KernelRootCNodeSizeBits 19 CACHE STRING "")

# Just let the regular abort spin without calling DebugHalt to prevent needless
# confusing output from the kernel
set(LibSel4MuslcSysDebugHalt FALSE CACHE BOOL "" FORCE)

# Only configure a single domain for the domain scheduler
set(KernelNumDomains 1 CACHE STRING "" FORCE)

# We want to build the debug kernel
ApplyCommonReleaseVerificationSettings(FALSE FALSE)

# We will attempt to generate a simulation script, so try and generate a simulation
# compatible configuration
ApplyCommonSimulationSettings(${KernelSel4Arch})
if(FORCE_IOMMU)
    set(KernelIOMMU ON CACHE BOOL "" FORCE)
endif()
