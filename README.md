# HoneyOS

A minimal x86 hobby OS built from scratch. Boots via a custom MBR, runs a freestanding C kernel, and exposes an interactive shell with a FAT-based linked-allocation filesystem.

## Features

- MBR bootloader (NASM, 512 bytes)
- 32-bit protected-mode kernel in C
- VGA text-mode display
- PS/2 keyboard driver
- FAT filesystem with linked allocation (files, directories, nested `cd`)
- Interactive shell: `ls`, `mkdir`, `cd`, `cat`, `write`, `edit`, `rm`, `shutdown`

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
| `dd` | Assemble the floppy image (standard on Linux) |

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
