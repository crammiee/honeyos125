CC      := gcc
LD      := ld
NASM    := nasm
QEMU    := qemu-system-i386

CFLAGS  := -m32 -ffreestanding -fno-pic -fno-stack-protector \
           -nostdlib -nostdinc -Wall -Wextra -Ikernel

LDFLAGS := -m elf_i386 -T linker.ld --oformat binary

BUILD   := build
BOOT    := boot

C_SRCS  := kernel/main.c          \
            kernel/screen/vga.c    \
            kernel/io/ports.c      \
            kernel/io/ata.c        \
            kernel/io/keyboard.c   \
            kernel/shell/shell.c   \
            kernel/fs/fat.c        \
            kernel/fs/file.c       \
            kernel/fs/dir.c

ASM_SRCS := kernel/kernel_entry.asm

C_OBJS   := $(patsubst %.c,   $(BUILD)/%.o, $(C_SRCS))
ASM_OBJS := $(patsubst %.asm, $(BUILD)/%.o, $(ASM_SRCS))

ALL_OBJS := $(ASM_OBJS) $(C_OBJS)

.PHONY: all run run-debug run-iso run-iso-persist iso artifacts clean

all: disk.img

disk.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=8192 2>/dev/null
	dd if=$(BUILD)/boot.bin of=$@ conv=notrunc 2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "[OK] disk.img ready"

$(BUILD)/kernel.bin: $(ALL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)

$(BUILD)/boot.bin: $(BOOT)/boot.asm
	@mkdir -p $(BUILD)
	$(NASM) -f bin $< -o $@
	@[ $$(wc -c < $@) -eq 512 ] || (echo "ERROR: boot.bin is not 512 bytes"; exit 1)

$(BUILD)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	$(QEMU) -hda disk.img -display curses -no-reboot

run-debug: all
	$(QEMU) -hda disk.img -display curses -no-reboot \
	        -serial file:serial.log -D qemu.log -d int,cpu_reset
	@echo "[OK] serial output -> serial.log  QEMU debug log -> qemu.log"

# El Torito floppy emulation requires exactly 1.2, 1.44, or 2.88 MB images.
# disk.img is 4 MB so it cannot be used directly as the boot image. Instead,
# build a separate minimal 1.44 MB floppy image (bootloader + kernel only) for
# the ISO. The filesystem always runs in RAM when booted from CD with no disk.
$(BUILD)/iso/honeyos.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	@mkdir -p $(BUILD)/iso
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	dd if=$(BUILD)/boot.bin of=$@ conv=notrunc 2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null

iso: $(BUILD)/iso/honeyos.img
	xorriso -as mkisofs -quiet -o honeyos.iso \
	        -b honeyos.img -c boot.cat $(BUILD)/iso
	@echo "[OK] honeyos.iso ready (boots standalone; RAM filesystem if no disk)"

# Boot the ISO on its own — no hard disk, RAM-backed filesystem (ephemeral).
run-iso: iso
	$(QEMU) -cdrom honeyos.iso -boot d \
	        -display curses -no-reboot

# Boot the ISO with disk.img attached, so the filesystem persists on the disk.
run-iso-persist: iso
	$(QEMU) -cdrom honeyos.iso -hda disk.img -boot d \
	        -display curses -no-reboot

honeyos.vdi: disk.img
	qemu-img convert -O vdi disk.img honeyos.vdi
	@echo "[OK] honeyos.vdi ready"

# Build all distributable artifacts: raw disk image, bootable ISO, and VDI.
# Requires xorriso (for ISO) and qemu-img (for VDI).
artifacts: iso honeyos.vdi
	@echo "[OK] artifacts ready: disk.img  honeyos.iso  honeyos.vdi"

clean:
	rm -rf $(BUILD) disk.img honeyos.iso honeyos.vdi qemu.log serial.log
