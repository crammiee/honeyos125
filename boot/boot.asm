; HoneyOS MBR Bootloader
; Loaded by BIOS at 0x7C00 in real mode (16-bit).
; Reads the kernel from disk into linear address 0x1000, enables the A20
; line, sets up a minimal GDT, switches to 32-bit protected mode, and
; jumps to the kernel entry point.

[BITS 16]
[ORG 0x7C00]

KERNEL_LOAD_SEG  equ 0x0100   ; segment → linear address 0x1000
KERNEL_SECTORS   equ 32       ; sectors to load (32 × 512 = 16 KB)

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl    ; BIOS passes boot drive number in DL

    mov si, msg_loading
    call print_string

    ; ------------------------------------------------------------------
    ; Load the kernel one sector at a time using CHS (INT 0x13, AH=02h).
    ; Single-sector calls avoid the track-boundary restriction: a 1.44 MB
    ; floppy has 18 sectors per track, so multi-sector reads that cross a
    ; track boundary will fail.
    ;
    ; Disk layout: sector 0 = MBR (this file), sector 1+ = kernel.
    ; CHS for a 1.44 MB floppy: 80 cylinders, 2 heads, 18 sectors/track.
    ; ------------------------------------------------------------------
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx              ; ES:BX = destination buffer (0x1000)

    mov ch, 0               ; cylinder 0
    mov dh, 0               ; head 0
    mov cl, 2               ; sector 2 (1-based; sector 1 = MBR)
    mov dl, [boot_drive]

    mov si, KERNEL_SECTORS  ; loop counter

.read_loop:
    mov ah, 0x02
    mov al, 1               ; one sector per call
    int 0x13
    jc  disk_error

    add bx, 512

    dec si
    jz  .load_done

    ; Advance CHS: sectors 1-18, then next head, then next cylinder
    inc cl
    cmp cl, 19
    jb  .read_loop
    mov cl, 1
    inc dh
    cmp dh, 2
    jb  .read_loop
    mov dh, 0
    inc ch
    jmp .read_loop

.load_done:

    ; ------------------------------------------------------------------
    ; Enable A20 line via the keyboard controller
    ; ------------------------------------------------------------------
    call enable_a20

    ; ------------------------------------------------------------------
    ; Load GDT and switch to 32-bit protected mode
    ; ------------------------------------------------------------------
    lgdt [gdt_descriptor]

    cli
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump flushes the pipeline and loads CS = code selector (0x08)
    jmp 0x08:protected_mode_entry

; ------------------------------------------------------------------
; 16-bit helpers
; ------------------------------------------------------------------

print_string:
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz   .done
    int  0x10
    jmp  .loop
.done:
    ret

disk_error:
    mov si, msg_disk_err
    call print_string
    hlt

enable_a20:
    call .kbc_wait
    mov  al, 0xD1
    out  0x64, al
    call .kbc_wait
    mov  al, 0xDF
    out  0x60, al
    call .kbc_wait
    ret
.kbc_wait:
    in  al, 0x64
    test al, 2
    jnz  .kbc_wait
    ret

; ------------------------------------------------------------------
; Data
; ------------------------------------------------------------------

boot_drive   db 0
msg_loading  db "Loading HoneyOS...", 0x0D, 0x0A, 0
msg_disk_err db "Disk read error!", 0x0D, 0x0A, 0

; ------------------------------------------------------------------
; GDT
; ------------------------------------------------------------------
gdt_start:
gdt_null:
    dq 0
gdt_code:               ; 0x08 — ring-0 32-bit code, base=0, limit=4 GB
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00
gdt_data:               ; 0x10 — ring-0 32-bit data, base=0, limit=4 GB
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ------------------------------------------------------------------
; 32-bit protected mode entry
; ------------------------------------------------------------------
[BITS 32]
protected_mode_entry:
    mov ax, 0x10        ; data selector
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x00090000
    jmp 0x1000          ; jump to kernel entry point

times 510 - ($ - $$) db 0
dw 0xAA55
