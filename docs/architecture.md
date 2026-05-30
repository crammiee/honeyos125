# HoneyOS — Architecture & Implemented Features

This document is the in-depth companion to the [README](../README.md). It walks
the boot path top to bottom and documents every subsystem that is implemented in
the current tree, mapping each feature to the source that provides it.

HoneyOS is a freestanding 32-bit x86 operating system that boots from its own
MBR, runs entirely in ring 0 without an underlying OS or libc, and presents an
interactive shell over a persistent FAT filesystem. It satisfies the CMSC 125
Phase 2 requirements: a bootable, self-running OS with a welcome screen, an
infinite task loop, and basic file/directory operations backed by a FAT using
linked allocation.

---

## 1. Boot sequence

The lifecycle from power-on to the shell prompt:

```
BIOS → boot.asm (0x7C00) → kernel_entry.asm (0x1000) → kernel_main() → shell_run() loop
```

### 1.1 MBR bootloader — `boot/boot.asm`

A hand-written 512-byte master boot record, the only code that runs in 16-bit
real mode. The BIOS loads it at physical `0x7C00` and the CPU begins executing.
In order, it:

1. **Sets up segments and stack** — zeroes `DS/ES/SS`, points `SP` at `0x7C00`,
   and saves the BIOS-provided boot drive number from `DL`.
2. **Loads the kernel** via `INT 13h, AH=42h` (LBA Extended Read) using a Disk
   Address Packet. This reads `KERNEL_SECTORS = 32` sectors (16 KB) starting at
   LBA 1 into linear address `0x1000`. LBA extended read is used (rather than the
   CHS `AH=02h` call) so it works against a `-hda` hard disk of any geometry.
3. **Enables the A20 line** through the 8042 keyboard controller (`out 0x64,
   0xD1` then `out 0x60, 0xDF`), unlocking memory above 1 MB.
4. **Installs a flat GDT** — a null descriptor plus two overlapping 4 GB ring-0
   segments: code selector `0x08` and data selector `0x10`, base 0, limit 4 GB.
5. **Enters protected mode** — sets `CR0.PE`, then far-jumps `0x08:protected_mode_entry`
   to flush the pipeline and load the 32-bit code segment.
6. **Hands off** — reloads the data segments with `0x10`, sets `ESP = 0x90000`,
   and jumps to the kernel at `0x1000`.

The image ends with the `0xAA55` boot signature, and the Makefile asserts the
assembled `boot.bin` is exactly 512 bytes.

### 1.2 Kernel entry — `kernel/kernel_entry.asm`

The first code linked into the kernel binary (so it sits at `0x1000`). It
establishes the C runtime environment — sets up the stack at `0x90000`, zeroes
the BSS section — then calls `kernel_main()`.

### 1.3 Kernel main — `kernel/main.c`

The top-level orchestrator. It initializes the subsystems in dependency order and
then enters the infinite task loop required by the spec:

```c
void kernel_main(void) {
    vga_init();           // clear screen, set default color
    vga_print_welcome();  // welcome banner
    keyboard_init();      // PS/2 driver
    fs_init();            // mount or format the disk
    while (1) shell_run();// infinite REPL loop
}
```

---

## 2. VGA text-mode display — `kernel/screen/vga.c`

Memory-mapped output to the 80×25 VGA text buffer at `0xB8000`. Each cell is two
bytes: an ASCII character and a color attribute (foreground/background nibble
pair, built with the `VGA_COLOR()` macro).

Implemented capabilities:

- **`vga_putchar` / `vga_puts`** — character and string output, handling `\n`
  (new line), `\r` (carriage return), and `\b` (destructive backspace that wraps
  to the previous row).
- **Scrolling** — when the cursor passes row 25, all rows shift up one and the
  bottom row is cleared.
- **`vga_clear`** — fill the screen with spaces and home the cursor.
- **Hardware cursor** — `update_hw_cursor()` writes the linear cursor position to
  the CRT controller (ports `0x3D4`/`0x3D5`) so the blinking cursor tracks output.
- **Color control** — `vga_set_color()` selects the active attribute; the shell
  prompt and welcome banner use it for color.
- **`vga_print_welcome`** — the colored "Welcome to HoneyOS / CMSC 125" banner
  shown at boot (satisfies the welcome-screen requirement).
- **`kprintf`** — a minimal `printf` for kernel/diagnostic messages. It walks the
  format string and pulls int-sized cdecl varargs directly off the stack,
  supporting `%s`, `%d`, `%u`, `%x` (8-digit `0x`-prefixed hex), `%c`, and `%%`.

---

## 3. PS/2 keyboard — `kernel/io/keyboard.c`

A polling (no-IRQ) driver. `keyboard_getchar()` busy-waits on the status port,
reads a scancode from data port `0x60`, and translates it through a US-QWERTY
scancode table. It tracks the shift modifier for capitalization/symbols and
ignores key-up (break) events, returning one ASCII character per call. Both the
shell's line editor and the file-content editor read input through this one call.

---

## 4. Port I/O primitives — `kernel/io/ports.c`

Thin `inb`/`outb`/`inw`/`outw` inline-assembly wrappers around the x86 `in`/`out`
instructions. Every other driver (ATA, keyboard, the shutdown poweroff, the VGA
cursor) goes through these for hardware register access.

---

## 5. ATA PIO disk driver — `kernel/io/ata.c`

Single-sector, polled (no DMA, no interrupts) LBA28 PIO against the primary bus
master drive, ports `0x1F0`–`0x1F7`. This is what makes the filesystem persistent:
every sector access hits the emulated disk (`disk.img` on the host), so data
survives reboots.

- **`ata_read(lba, buf)`** — wait for `BSY` clear, program the LBA28 address and a
  sector count of 1, issue `READ SECTORS` (`0x20`), wait for `DRQ`, then read 256
  16-bit words from the data register.
- **`ata_write(lba, buf)`** — same setup, issue `WRITE SECTORS` (`0x30`), push 256
  words, then issue `FLUSH CACHE` (`0xE7`) so the write is committed to the image.

The filesystem's `sector_read`/`sector_write` are direct pass-throughs to these.

---

## 6. FAT filesystem — `kernel/fs/`

A FAT16-style filesystem using **linked allocation**, the core deliverable of the
project. Split across three files: `fat.c` (geometry, FAT chains, directory
traversal, mount/format), `dir.c` (directory commands), and `file.c` (file
commands).

### 6.1 On-disk layout

512-byte sectors on a 2880-sector (1.44 MB) disk:

| Sectors | Region | Size | Purpose |
|---------|--------|------|---------|
| 0 | MBR | 1 | bootloader (written by `boot.asm`) |
| 1–32 | Kernel | 32 | kernel binary image |
| 33 | Superblock | 1 | magic + geometry |
| 34–49 | FAT table | 16 | 4096 `uint16_t` chain entries |
| 50–81 | Root directory | 32 | fixed-size root dir region |
| 82+ | Data | rest | file data and sub-directory blocks |

### 6.2 Superblock & mount/format — `fs_init`

The superblock (`superblock_t`, padded to 512 bytes) stores the magic number
`0x484F4E45` ("HONE") plus the region offsets. `fs_init()`:

- Reads sector 33. If the magic is absent the disk is **formatted** (`fs_format`):
  write a fresh superblock, zero the FAT, and zero the root directory region.
- If the magic is present, the FAT is **loaded** from disk into the in-memory
  cache (`fat_load`) and the existing filesystem is mounted.
- Either way, the current working directory is reset to the root.

This format-on-first-boot / mount-on-subsequent-boot behavior is what gives
HoneyOS persistence across reboots.

### 6.3 FAT chain operations

The 4096-entry FAT is cached in memory (`fat_table[]`) and written back per-sector
on every change:

- **`fat_next(block)`** — follow the chain (`0xFFFF` = end of chain).
- **`fat_set(block, val)`** — update an entry and flush the affected FAT sector.
- **`fat_alloc()`** — linear scan for the first free (`0x0000`) data block, marks
  it end-of-chain, and returns it (or `FAT_EOC` when the disk is full).
- **`fat_free_chain(first)`** — walk and free an entire chain (file data or a
  directory's blocks).

### 6.4 Directory entries & traversal

Each `dir_entry_t` is 32 bytes: an 8.3 name (`name[8]` + `ext[3]`, space-padded,
no NUL), an attribute byte (`ATTR_FILE 0x20` / `ATTR_DIR 0x10` / `ATTR_FREE 0x00`),
the head-of-chain `first_block`, and the file `size`.

- The **root directory** is a fixed contiguous region (sectors 50–81).
- Every **sub-directory** is its own FAT chain — exactly like a file — and `mkdir`
  seeds it with a `..` entry in slot 0 pointing at the parent's first sector.
- `dir_next_sector()` abstracts over both: it advances within the fixed root region
  or follows the FAT chain for a sub-directory.
- `dir_find()` / `dir_find_free()` walk a directory one sector at a time through a
  shared buffer; `dir_find_free()` **grows a sub-directory** by appending a new FAT
  block when every existing sector is full (the fixed root simply reports full).
- `dir_is_empty()` treats a directory as empty when it holds nothing but `..`.

### 6.5 File operations — `file.c`

- **`file_read` (`cat`)** — follow the file's FAT chain, printing `size` bytes
  block by block.
- **`file_write` (`write`)** — interactive multi-line entry, terminated by a line
  containing only `.`. Overwrites any existing file of the same name (freeing its
  old chain first), allocates and links new blocks as content is typed (up to
  ~16 KB), and records the 8.3 name (splitting on `.` into name/extension), the
  head block, and the byte size in the parent directory.
- **`file_edit` (`edit`)** — prints the current contents, frees the old chain, and
  re-runs the write flow to rewrite the file.
- **`file_delete` (`rm`)** — frees the chain and clears the directory entry; works
  for files and for empty directories, but refuses a non-empty directory so child
  blocks are never orphaned.

### 6.6 Directory operations — `dir.c`

- **`dir_list` (`ls`)** — prints each entry in the current directory tagged
  `[FILE]`/`[DIR]`, with file sizes in bytes; `.`/`..` are hidden, and an empty
  directory prints `(empty directory)`.
- **`dir_create` (`mkdir`)** — rejects the reserved names `.`/`..` and duplicates,
  allocates the new directory's first block, seeds its `..` back-pointer, and
  records it in the parent.
- **`dir_change` (`cd`)** — `.` is a no-op; `..` follows the slot-0 parent pointer
  (and refuses to go above root); a named directory moves into it. The printable
  prompt path (`cwd_path`, e.g. `root/sub`) is pushed/popped to match.
- **`dir_delete`** — removes an empty directory and reclaims its blocks. (Not bound
  to its own shell command; `rm` covers directory deletion.)

---

## 7. Shell — `kernel/shell/shell.c`

The interactive REPL run in an infinite loop by `kernel_main`. Each iteration:

1. Prints a colored prompt showing the current path (`cwd_path`), e.g. `root> ` or
   `root/docs> `.
2. Reads a line character by character via the keyboard driver, echoing input and
   handling backspace.
3. Tokenizes the line on spaces into `argv`.
4. Dispatches `argv[0]` against a static command table; unknown commands print a
   hint to type `help`.

### Command reference

| Command | Handler | Description |
|---------|---------|-------------|
| `help` | `cmd_help` | list available commands |
| `ls` | `dir_list` | list the current directory |
| `mkdir <name>` | `dir_create` | create a directory |
| `cd <name>` | `dir_change` | change directory (`..` to go up) |
| `rm <name>` | `file_delete` | delete a file or empty directory |
| `cat <file>` | `file_read` | print file contents |
| `write <file>` | `file_write` | create/overwrite a file (end input with `.`) |
| `edit <file>` | `file_edit` | show then rewrite an existing file |
| `shutdown` | `cmd_shutdown` | halt the OS |

`shutdown` performs a clean poweroff by writing `0x2000` to port `0x604` (the QEMU
ACPI shutdown register), then executes `cli; hlt`.

---

## 8. Build & image assembly

The toolchain (`Makefile`) cross-compiles a freestanding 32-bit binary with no
libc, runtime, or ELF header in the final image:

- C sources compiled with `gcc -m32 -ffreestanding -nostdlib -nostdinc` (plus
  `-fno-pic`, `-fno-stack-protector`, `-Wall -Wextra`).
- `kernel_entry.asm` assembled to ELF32; `boot.asm` assembled to a flat binary and
  size-checked at 512 bytes.
- `ld -T linker.ld --oformat binary` links the kernel to a flat binary with
  `kernel_entry` first so it lands at `0x1000`.
- `dd` zeroes a 2880-sector image, writes the MBR to sector 0, and writes the
  kernel starting at sector 1, producing `disk.img`.
- `make run` boots it under `qemu-system-i386 -hda disk.img -display curses`;
  `make run-debug` adds serial output and a CPU/interrupt trace to `qemu.log`.
- `make iso` wraps the floppy-sized `disk.img` in a bootable ISO
  (`honeyos.iso`) via `xorriso` using floppy-emulation El Torito, so the custom
  MBR boots from a CD/DVD in any VM. The filesystem still lives on the ATA hard
  disk, so `make run-iso` boots the ISO (CD) together with `disk.img` (hard
  disk); files persist on the disk exactly as in the `-hda` boot.

### Boot targets at a glance

| Command | CD/DVD | Hard disk | Filesystem persists? |
|---------|--------|-----------|----------------------|
| `make run` | — | `disk.img` | yes |
| `make run-iso` | `honeyos.iso` | `disk.img` | yes |

---

## 9. Requirements coverage

| Phase 2 requirement | Where it is met |
|---------------------|-----------------|
| Runs on its own (no host OS), in a VM | `boot.asm` + `kernel_entry.asm`, run under QEMU |
| Infinite task loop until shutdown | `while (1) shell_run();` in `kernel_main` |
| Boots from `main.c` equivalent | `kernel_main()` in `kernel/main.c` |
| Welcome screen | `vga_print_welcome()` |
| Read / write / edit / delete files | `cat` / `write` / `edit` / `rm` (`file.c`) |
| Create / change / delete / list directories | `mkdir` / `cd` / `rm` / `ls` (`dir.c`) |
| Boot and shutdown operations | `boot.asm`; `shutdown` command |
| FAT with a chosen allocation method | linked allocation in `fat.c` |
| Heavily commented code | every source file carries a header + inline comments |
