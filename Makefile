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

.PHONY: all run clean

all: disk.img

disk.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
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
	        -serial stdio -d int,cpu_reset 2>&1 | tee qemu.log

clean:
	rm -rf $(BUILD) disk.img qemu.log
