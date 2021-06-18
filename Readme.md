# seL4 SecCells tests

This repository contains a project that has the goal of testing the modifications made to the seL4 microkernel to
support the SecCells architecture designed to improve memory compartmentalization.

SecCells currently is designed as an extension to the RISC-V ISA.

## Building and running

### Prerequisites

As the implementation is based on the RISC-V ISA with modifications, make sure to have the
[RISC-V toolchain](https://github.com/riscv/riscv-gnu-toolchain) installed.
To successfully build for SecCells, please note that you need a
[modified version of the binutils](https://bitbucket.org/atrib/riscv-binutils-gdb/src/secure/) to compile the seL4
microkernel and the userspace components.

Additionally, current tests are executed in a virtual environment provided by QEMU, since there is no hardware for
SecCells (yet).
Please use our [modified version of QEMU](https://bitbucket.org/atrib/qemu/src/secure-florian/) for SecCells support in
the `qemu-system-riscv64` executable.

Please note that there are also other requirements for building and running the code such as CMake, Ninja, or Python 3
for building and running the code.
However, we don't list all the prerequisites because we assume that you have already read the 
[seL4 host dependencies webpage](https://docs.sel4.systems/projects/buildsystem/host-dependencies.html) and set up your
build environment accordingly.

### Initializing the build environment

Builds should be done in a dedicated subdirectory.
You're free to name the subdirectory whatever you want.
However, we suggest naming the directory `build` or at least using the prefix `build`, because the `.gitignore` file
excludes such directories from tracking.

After you've created the build directory and entered it, set up the build system by invoking
`../init-build.sh -DRISCV64=1 -DKernelRiscvExtD=1 -DKernelRiscvExtF=1 -DKernelSecCell=1`.

### Building the code

If CMake (invoked through the above mentioned shell script) succeeds to generate the Ninja files for building the actual
executable, simply run `ninja` to trigger the build.

### Running the compiled binaries

For running the code in QEMU, it is sufficient to use the generated simulation script in the build directory by invoking
`./simulate`.

## Modifying the code

If you need to implement new functionalities in the kernel or some libraries, simply modify the code in the
corresponding subdirectories (`kernel`, `projects`, `tools`).

For userspace modifications, modify/add/delete code in the `src` directory.
This is where the code for the rootserver is tracked.
Please don't forget to update the `CMakeLists.txt` file in the root directory of the repository accordingly if you add
or remove files or dependencies.
