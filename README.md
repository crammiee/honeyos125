# HoneyOS

A minimal x86 hobby OS built from scratch. Boots via a custom MBR, runs a freestanding C kernel, and exposes an interactive shell with a FAT-based linked-allocation filesystem.

## Features

### Bootloader
A hand-written 512-byte MBR (`boot/boot.asm`). On boot the BIOS loads it at `0x7C00`. It reads the kernel from disk using INT 13h LBA extended read, enables the A20 line via the keyboard controller, installs a flat GDT (two 4 GB ring-0 segments), sets `CR0.PE`, and far-jumps into 32-bit protected mode before handing off to the kernel at `0x1000`.

### Kernel
Entered from `kernel/kernel_entry.asm`, which sets up the stack at `0x90000`, zeroes BSS, then calls `kernel_main()`. The kernel is compiled as a freestanding 32-bit ELF and linked to a flat binary by `linker.ld` — no libc, no runtime, no ELF header in the final image.

### VGA Display
Direct memory-mapped output to the 80×25 VGA text buffer at `0xB8000` (`kernel/screen/vga.c`). Each character is a two-byte cell (ASCII + color attribute). Supports scrolling, backspace, newline, color changes, and hardware-cursor updates via the CRT controller (ports `0x3D4`/`0x3D5`). It also ships a freestanding `kprintf()` (handling `%s`, `%d`, `%u`, `%x`, `%c`, `%%` by pulling cdecl varargs off the stack) and a colored welcome banner. Used by the shell, filesystem, and kernel messages.

### PS/2 Keyboard
Polling driver (`kernel/io/keyboard.c`) that reads scancodes from port `0x60` and translates them to ASCII using a US QWERTY scancode table. Handles key-up events and modifier keys (shift). Exposes a single `keyboard_getchar()` call that the shell reads character by character.

### FAT Filesystem
A FAT16-style linked-allocation filesystem (`kernel/fs/`). The 2880-sector (1.44 MB) disk is laid out as:

| Sectors | Region | Notes |
|---------|--------|-------|
| 0 | MBR | written by `boot.asm` |
| 1–32 | Kernel binary | 32 sectors = 16 KB, loaded by the bootloader |
| 33 | Superblock | magic, geometry |
| 34–49 | FAT table | 16 sectors → 4096 `uint16_t` entries |
| 50–81 | Root directory | 32 sectors, fixed size |
| 82+ | Data blocks | files and sub-directories |

Each FAT entry is a `uint16_t`: `0x0000` = free, `0xFFFF` = end of chain, otherwise the index of the next block. File and directory metadata is stored as 32-byte 8.3-format entries (`dir_entry_t`). The root directory is a fixed contiguous region; every sub-directory is its own FAT chain (and grows by appending a block when its current sectors fill up). Sub-directories store a `..` entry in slot 0 pointing back at the parent so `cd ..` can walk home. On first boot the disk is formatted; subsequent boots detect the magic number `0x484F4E45` ("HONE") in the superblock and mount the existing filesystem.

### ATA PIO Disk I/O
Sector reads and writes go directly to the emulated hard disk via ATA PIO (`kernel/io/ata.c`). The driver polls the primary bus status register (`0x1F7`), programs a LBA28 address, issues the read (`0x20`) or write (`0x30`) command, then transfers 256 16-bit words through the data register (`0x1F0`). Writes are followed by a cache flush (`0xE7`). This is what makes the filesystem persistent — data survives reboots because it is written into `disk.img` on the host.

### Shell
A REPL loop (`kernel/shell/shell.c`) that prints a prompt, reads a line character by character, tokenises on spaces, and dispatches to a static command table. Supports `ls`, `mkdir`, `cd`, `cat`, `write`, `edit`, `rm`, `help`, and `shutdown`. `shutdown` writes `0x2000` to port `0x604` (QEMU ACPI poweroff) and halts with `cli; hlt`.

## Getting Started

### Linux

**Arch / Manjaro:**
```bash
sudo pacman -S base-devel gcc-multilib nasm qemu-system-x86 qemu-ui-curses
```
> `gcc-multilib` replaces `gcc` — pacman will ask you to confirm the swap.  
> `qemu-ui-curses` is a separate package on Arch; without it QEMU silently drops curses support.

**Debian / Ubuntu:**
```bash
sudo apt install build-essential gcc-multilib nasm qemu-system-x86
```

1. Install the dependencies above for your distro, then:
2. Clone and build:
   ```bash
   git clone <repo-url>
   cd honeyos125
   make
   ```
3. Run:
   ```bash
   make run
   ```

### Windows (WSL2)

WSL2 is the recommended path — it gives you a full Linux environment where all the tools work natively.

1. Enable WSL2 and install a distro (if not already set up):
   ```powershell
   wsl --install
   ```
   Restart when prompted, then open your WSL2 terminal.

2. Inside WSL2, install dependencies based on your distro:

   **Arch (`pacman`):**
   ```bash
   sudo pacman -Syy
   sudo pacman -S base-devel gcc-multilib nasm qemu-system-x86 qemu-ui-curses
   ```

   **Ubuntu/Debian (`apt`):**
   ```bash
   sudo apt update
   sudo apt install build-essential gcc-multilib nasm qemu-system-x86
   ```

3. Clone and build:
   ```bash
   git clone <repo-url>
   cd honeyos125
   make
   ```
4. Run:
   ```bash
   make run
   ```

## Dependencies

| Tool | Purpose |
|------|---------|
| `gcc` (multilib / `gcc-multilib`) | Cross-compile 32-bit freestanding C |
| `nasm` | Assemble bootloader and kernel entry |
| `ld` (binutils) | Link flat 32-bit binary |
| `qemu-system-i386` | Run the disk image |
| `make` | Build orchestration |
| `dd` | Assemble the 1.44 MB disk image (standard on Linux) |

See [Getting Started](#getting-started) for distro-specific install commands.

## Build & Run

```bash
# Build disk image
make

# Run in QEMU (curses terminal UI)
make run
# or
./run.sh

# Run with debug output (serial + QEMU interrupt log)
make run-debug
# or
./run.sh --debug

# Clean build artifacts
make clean
```

> QEMU opens in curses mode inside the terminal. Press `Alt+2` to access the QEMU monitor, `Ctrl+A X` to quit.

## Shell Commands

```
help            show available commands
ls              list current directory
mkdir <name>    create a directory
cd <name>       change directory (.. to go up)
cat <file>      print file contents
write <file>    create/overwrite file (end input with '.')
edit <file>     rewrite an existing file
rm <name>       delete a file or directory
shutdown        halt the OS
```
