/*
 * HoneyOS — Kernel Main
 *
 * Entry point called by kernel_entry.asm after the stack is set up and
 * the BSS segment is zeroed. Initialises every subsystem in order, then
 * drops into the shell REPL loop which runs forever.
 */

#include "types.h"
#include "screen/vga.h"
#include "io/keyboard.h"
#include "shell/shell.h"
#include "fs/fat.h"

void kernel_main(void) {
    vga_init();          /* clear screen, set default color */
    vga_print_welcome(); /* draw the banner with team names */
    keyboard_init();     /* flush PS/2 buffer, reset shift state */
    fs_init();           /* mount existing disk or format a fresh one */

    /* Shell REPL — never returns */
    while (1) {
        shell_run();     /* print prompt, read line, dispatch command */
    }
}
