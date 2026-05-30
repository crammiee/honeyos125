# HoneyOS

A minimal x86 hobby OS built from scratch. Boots via a custom MBR, runs a freestanding C kernel, and exposes an interactive shell with a FAT-based linked-allocation filesystem.

## Features

### Bootloader

A hand-written 512-byte MBR (`boot/boot.asm`). On boot the BIOS loads it at `0x7C00`. It reads the kernel from disk — using INT 13h AH=42h (LBA extended read) when booted from a hard disk, or classic CHS reads (AH=02h) when booted from a floppy or an El Torito CD, chosen by the BIOS boot-drive number — then enables the A20 line via the keyboard controller, installs a flat GDT (two 4 GB ring-0 segments), sets `CR0.PE`, and far-jumps into 32-bit protected mode before handing off to the kernel at `0x1000`.

### Kernel

Entered from `kernel/kernel_entry.asm`, which sets up the stack at `0x90000`, zeroes BSS, then calls `kernel_main()`. The kernel is compiled as a freestanding 32-bit ELF and linked to a flat binary by `linker.ld` — no libc, no runtime, no ELF header in the final image.

### VGA Display

Direct memory-mapped output to the 80×25 VGA text buffer at `0xB8000` (`kernel/screen/vga.c`). Each character is a two-byte cell (ASCII + color attribute). Supports scrolling, backspace, newline, color changes, and hardware-cursor updates via the CRT controller (ports `0x3D4`/`0x3D5`). It also ships a freestanding `kprintf()` (handling `%s`, `%d`, `%u`, `%x`, `%c`, `%%` by pulling cdecl varargs off the stack) and a colored welcome banner.

### PS/2 Keyboard

Polling driver (`kernel/io/keyboard.c`) that reads scancodes from port `0x60` and translates them to ASCII using a US QWERTY scancode table. Handles key-up events and modifier keys (shift). Exposes a single `keyboard_getchar()` call that the shell reads character by character.

### FAT Filesystem

A FAT16-style linked-allocation filesystem (`kernel/fs/`). The 4 MB disk is divided into 32 blocks of 128 KB each (256 sectors per block). Layout:

| Sectors | Region | Notes |
|---------|--------|-------|
| 0 | MBR | written by `boot.asm` |
| 1–32 | Kernel binary | 32 sectors = 16 KB, loaded by the bootloader |
| 33 | Superblock | magic, geometry |
| 34 | FAT table | 1 sector → 32 `uint16_t` entries (one per 128 KB block) |
| 35–66 | Root directory | 32 sectors, fixed size |
| 67–255 | (reserved) | remainder of block 0 |
| 256–8191 | Data blocks | blocks 1–31, 128 KB each (files and sub-directories) |

Each FAT entry is a `uint16_t`: `0x0000` = free, `0xFFFF` = end of chain, otherwise the index of the next block. File and directory metadata is stored as 28-byte 8.3-format entries (`dir_entry_t`). The root directory is a fixed contiguous region (32 sectors); every sub-directory occupies exactly one 128 KB block — large enough for over 4000 entries, so no FAT chaining is needed. Sub-directories store a `..` entry in slot 0 pointing back at the parent so `cd ..` can walk home. On first boot the disk is formatted; subsequent boots detect the magic number `0x484F4E45` ("HONE") in the superblock and mount the existing filesystem. If no ATA drive responds at all (e.g. when booted from a read-only CD/ISO with no hard disk), `fs_init` formats and runs an ephemeral in-RAM filesystem instead, so the shell is still fully usable — changes just aren't saved.

### ATA PIO Disk I/O

Sector reads and writes go directly to the emulated hard disk via ATA PIO (`kernel/io/ata.c`). The driver polls the primary bus status register (`0x1F7`), programs a LBA28 address, issues the read (`0x20`) or write (`0x30`) command, then transfers 256 16-bit words through the data register (`0x1F0`). Writes are followed by a cache flush (`0xE7`). Every wait is bounded by a timeout so the kernel never hangs when no drive is attached; on timeout the call returns an error and the filesystem falls back to a RAM disk.

### Shell

A REPL loop (`kernel/shell/shell.c`) that prints a prompt, reads a line character by character, tokenises on spaces, and dispatches to a static command table. Supports `ls`, `mkdir`, `cd`, `cat`, `write`, `edit`, `rm`, `help`, and `shutdown`. `shutdown` tries QEMU, VirtualBox (old and new ACPI), and Bochs poweroff ports in sequence before falling back to `cli; hlt`.

## Dependencies

### Required

| Tool | Purpose |
|------|---------|
| `gcc` (`gcc-multilib`) | Cross-compile 32-bit freestanding C |
| `nasm` | Assemble bootloader and kernel entry |
| `ld` (binutils) | Link flat 32-bit binary |
| `qemu-system-i386` | Run the OS in QEMU |
| `make` | Build orchestration |
| `dd` | Assemble the disk image (standard on Linux) |

### Optional

| Tool | Unlocks |
|------|---------|
| `xorriso` | `make iso`, `make run-iso`, `make run-iso-persist`, `make artifacts` — builds a bootable ISO for VMs |
| `qemu-img` (`qemu-utils` on Debian/Ubuntu) | `make artifacts` — converts `disk.img` to `.vdi` (VirtualBox) and `.vmdk` (VMware) |

## Getting Started

HoneyOS is developed on Linux. Windows users have two options — both give a full Linux environment where the instructions below apply as normal:

- **WSL2** — `wsl --install` in PowerShell, pick any distro (Arch, Ubuntu, Debian, etc.)
- **Linux VM** — run any Linux distro in VirtualBox, VMware, or similar

**macOS (Intel):** install dependencies via Homebrew and follow the Linux instructions below — everything works natively.

```bash
brew install gcc nasm qemu xorriso
```

**macOS (Apple Silicon):** `qemu-system-i386` does not run reliably on ARM. Use a Linux VM or a Linux/Windows machine instead.

**Arch / Manjaro:**

```bash
# Required
sudo pacman -S base-devel gcc-multilib nasm qemu-system-x86 qemu-ui-curses
# Optional
sudo pacman -S xorriso qemu-img
```

> `gcc-multilib` replaces `gcc` — pacman will ask you to confirm the swap.
> `qemu-ui-curses` is required on Arch for QEMU's curses display mode.

**Debian / Ubuntu:**

```bash
# Required
sudo apt install build-essential gcc-multilib nasm qemu-system-x86
# Optional
sudo apt install xorriso qemu-utils
```

Then clone and build:

```bash
git clone <repo-url>
cd honeyos125
make
make run
```

## Build & Run

```bash
make                  # build disk.img (4 MB raw hard-disk image)
make run              # build and boot disk.img in QEMU (persistent filesystem)
make run-debug        # same, with serial output and QEMU interrupt log → qemu.log
make iso              # build honeyos.iso (bootable CD image) — requires xorriso
make run-iso          # build and boot the ISO in QEMU (ephemeral RAM filesystem)
make run-iso-persist  # boot the ISO in QEMU with disk.img attached (persistent)
make artifacts        # build disk.img + honeyos.iso + honeyos.vdi — requires xorriso and qemu-img
make clean            # remove all build artifacts
```

> QEMU opens in curses mode. Press `Alt+2` for the QEMU monitor, `Ctrl+A X` to quit.

## Running in a VM

The easiest option for VirtualBox or any VM is the ISO:

1. Run `make iso` to build `honeyos.iso`
2. Create a new VM (Type: **Other**, Version: **Other/Unknown 32-bit**)
3. Attach `honeyos.iso` as a CD/DVD drive — VirtualBox auto-creates a virtual hard disk, the ATA driver finds it, and the filesystem is **persistent** with no extra steps
4. Boot

To get `disk.img`, `honeyos.iso`, and `honeyos.vdi` all at once (requires `xorriso` and `qemu-img`):

```bash
make artifacts
```

Attach `honeyos.vdi` as a hard disk in VirtualBox, or convert to VMDK for VMware:

```bash
qemu-img convert -O vmdk disk.img honeyos.vmdk
```

## Shell Commands

```text
help            show available commands
clear           clear the screen
ls              list current directory
mkdir <name>    create a directory
cd <name>       change directory (.. to go up)
cat <file>      print file contents
write <file>    create/overwrite file (end input with '.')
edit <file>     rewrite an existing file
rm <name>       delete a file or empty directory
rm -r <name>    recursively delete a directory and all its contents
fat             display the file allocation table (used/free blocks)
shutdown        halt the OS (QEMU and VirtualBox; use VM power-off on other platforms)
```
