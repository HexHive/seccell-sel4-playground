#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/util.h>
#include <stdio.h>
#include <utils/util.h>

int main(int argc, char *argv[]) {
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }

    printf("Hello World!\n");
}
