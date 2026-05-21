/*
 * HoneyOS — Kernel Main (Stage 4: keyboard + shell)
 * Called from kernel_entry.asm after stack and BSS are ready.
 */

#include "types.h"
#include "screen/vga.h"
#include "io/keyboard.h"
#include "shell/shell.h"

void kernel_main(void) {
    vga_init();
    vga_print_welcome();
    keyboard_init();

    while (1) {
        shell_run();
    }
}
