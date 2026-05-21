/*
 * HoneyOS — Kernel Main (Stage 5: full OS with file system)
 * Called from kernel_entry.asm after stack and BSS are ready.
 */

#include "types.h"
#include "screen/vga.h"
#include "io/keyboard.h"
#include "shell/shell.h"
#include "fs/fat.h"

void kernel_main(void) {
    vga_init();
    vga_print_welcome();
    keyboard_init();
    fs_init();

    while (1) {
        shell_run();
    }
}
