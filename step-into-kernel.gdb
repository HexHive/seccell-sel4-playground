# GDB script to step into the seL4 kernel when loaded via OpenSBI and elfloader

# Layout / UI setup (not functionally important, tune for personal preferences)
layout split
focus cmd
set radix 16

# Connect to QEMU
target remote localhost:1234

# Load symbols for OpenSBI, kernel and executable / libraries
add-symbol-file images/seccell-test-image-riscv-spike
add-symbol-file kernel/kernel.elf
add-symbol-file seccell-test

# Break at jump to kernel
# Adapt offset in elfloader binary, if necessary
b *payload_bin+0x426
# Run and clear breakpoint, don't need it anymore
c
clear *payload_bin+0x426
# Step into kernel
si

# Break at executable entry
b main
