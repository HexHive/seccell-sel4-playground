#include <sel4platsupport/platsupport.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    /* Setup serial output via seL4_Debug_PutChar */
    platsupport_serial_setup_bootinfo_failsafe();

    printf("Hello, World!\n");

    while (1);
    return 0;
}
