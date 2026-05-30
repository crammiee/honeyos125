; HoneyOS MBR Bootloader
; Loaded by BIOS at 0x7C00 in real mode (16-bit).
; Reads the kernel from disk into linear address 0x1000 (LBA extended read for
; hard disks, CHS read for floppies / El Torito CDs), enables the A20 line, sets
; up a minimal GDT, switches to 32-bit protected mode, and jumps to the kernel
; entry point.

[BITS 16]
[ORG 0x7C00]

KERNEL_LOAD_SEG  equ 0x0100   ; segment → linear address 0x1000
KERNEL_SECTORS   equ 32       ; sectors to load (32 × 512 = 16 KB)
FLOPPY_SPT       equ 18       ; CHS geometry: sectors per track (1.44 MB floppy)
FLOPPY_HEADS     equ 2        ; CHS geometry: heads (1.44 MB floppy)

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
    ; Load the kernel. The method depends on the boot device:
    ;   • Hard disk (-hda): BIOS drive >= 0x80. Use INT 13h AH=42h (LBA
    ;     Extended Read) — works for any hard-disk geometry.
    ;   • Floppy / El Torito CD floppy-emulation: BIOS drive < 0x80. Extended
    ;     read is unreliable there (the CD case silently returns wrong data),
    ;     so use classic CHS reads (AH=02h) with 1.44 MB floppy geometry.
    ; Bit 7 of the BIOS drive number cleanly distinguishes the two.
    ; ------------------------------------------------------------------
    mov ax, KERNEL_LOAD_SEG
    mov es, ax              ; destination segment (linear 0x1000)

    mov dl, [boot_drive]
    test dl, 0x80
    jz  .load_chs          ; floppy / emulated floppy → CHS

    ; ---- LBA extended read (hard disk) ----
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc  disk_error
    jmp .load_done

    ; ---- CHS read: LBA 1..KERNEL_SECTORS, one sector at a time ----
.load_chs:
    xor di, di             ; di = sector index 0..KERNEL_SECTORS-1
.chs_loop:
    cmp di, KERNEL_SECTORS
    jae .load_done

    ; Buffer offset first (this MUL clobbers DX, so it must run before the
    ; CHS values are placed in CX/DH below). ES:BX = KERNEL_LOAD_SEG:(di*512).
    mov ax, di
    mov bx, 512
    mul bx                 ; DX:AX = di*512 (DX = 0 for di < 128)
    mov bx, ax
    mov ax, KERNEL_LOAD_SEG
    mov es, ax

    ; Convert LBA (= di + 1) to CHS for 1.44 MB floppy geometry. Divisors go
    ; in CX/SI because BX now holds the buffer offset.
    mov ax, di
    inc ax                 ; AX = LBA (kernel starts at LBA 1)
    xor dx, dx
    mov cx, FLOPPY_SPT
    div cx                 ; AX = track = LBA/SPT, DX = LBA%SPT
    mov cl, dl
    inc cl                 ; CL = sector number (1-based)
    xor dx, dx
    mov si, FLOPPY_HEADS
    div si                 ; AX = cylinder, DX = head
    mov ch, al             ; CH = cylinder (low 8 bits; fits our range)
    mov dh, dl             ; DH = head

    mov ah, 0x02
    mov al, 1              ; read one sector
    mov dl, [boot_drive]
    push di
    int 0x13
    pop di
    jc  disk_error

    inc di
    jmp .chs_loop

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

; Disk Address Packet for INT 13h AH=42h (LBA Extended Read)
align 2
dap:
    db  0x10            ; DAP size = 16 bytes
    db  0               ; reserved
    dw  KERNEL_SECTORS  ; sectors to transfer
    dw  0x0000          ; buffer offset (ES:0x0000 → linear 0x1000)
    dw  0x0100          ; buffer segment
    dq  1               ; LBA of first sector (kernel starts at sector 1)

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
