; HoneyOS Kernel Entry Point
; The bootloader jumps here (linear address 0x1000) after entering
; protected mode. Sets up the stack, zeroes BSS, then calls kernel_main().

[BITS 32]

global _start
extern kernel_main

section .text

_start:
    mov esp, 0x00090000     ; stack grows down from 0x90000

    ; Zero-initialise all BSS (global/static variables)
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss

global bss_start
global bss_end
bss_start:
    resb 0
bss_end:
