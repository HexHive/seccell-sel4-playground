# seL4 SecCells tests

This repository contains a project that has the goal of testing the modifications made to the seL4 microkernel to
support the SecCells architecture designed to improve memory compartmentalization.

SecCells currently is designed as an extension to the RISC-V ISA.

## Building and running

### Prerequisites

As the implementation is based on the RISC-V ISA with modifications, make sure to have the
[RISC-V toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain) installed.
To successfully build for SecCells, please note that you need a
[modified version of the binutils](https://github.com/seccells/riscv-binutils-gdb/tree/secure) to compile the seL4
microkernel and the userspace components.

Additionally, current tests are executed in a virtual environment provided by QEMU, since there is no hardware for
SecCells (yet).
Please use our [modified version of QEMU](https://github.com/seccells/qemu/tree/secure-florian) for SecCells support in
the `qemu-system-riscv64` executable.

Please note that there are also other requirements for building and running the code such as CMake, Ninja, or Python 3
for building and running the code.
However, we don't list all the prerequisites because we assume that you have already read the
[seL4 host dependencies webpage](https://docs.sel4.systems/projects/buildsystem/host-dependencies.html) and set up your
build environment accordingly.

### Retrieving the code

Since libraries, build tools and the seL4 microkernel are located in git submodules, those need to be retrieved together
with this repository.  
If you haven't cloned the repository yet, you can add the `--recurse-submodules` flag to `git clone`.
If you have already cloned it without this flag, you can simply issue `git submodule update --init --recursive` and
don't need to start over and do a fresh clone.

__Note:__ It is suggested to also run `git submodule update --init --recursive` after every `git pull`.
This ensures that all the submodules (kernel, libraries, firmware, etc.) are in a state that we as the developers of the
whole system envisioned and tested.
You surely can skip this step and checkout the submodules at any commit you wish but there is obviously no guarantee
that the system will work in such a case.

### Initializing the build environment

Builds should be done in a dedicated subdirectory.
You're free to name the subdirectory whatever you want.
However, we suggest naming the directory `build` or at least using the prefix `build`, because the
[`.gitignore`](./.gitignore) file excludes such directories from tracking.

After you've created the build directory and entered it, set up the build system by invoking
`../init-build.sh`.
Some sane default options are already provided in [`settings.cmake`](./settings.cmake) and thus no arguments have to be
provided to the initialization script.
If you want to modify the build options, either modify the necessary CMake files directly or provide explicit arguments
on the command line, e.g., calling `../init-build.sh -DKernelRiscvExtD=0` to turn off double precision floating point
support in the kernel.

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
