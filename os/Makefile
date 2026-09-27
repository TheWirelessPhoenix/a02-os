# A02-OS build. Output: a02os.bin (load at 0x120000, enter at 0x120001).
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

ARCH = -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS = $(ARCH) -O2 -g -Wall -Wextra -std=gnu99 -ffreestanding -fno-builtin-printf \
	-ffunction-sections -fdata-sections -Isrc -Ilib/fatfs -Ilib/minimp3
LDFLAGS = $(ARCH) -nostartfiles -nostdlib -Wl,-T,link.ld -Wl,--gc-sections \
	-Wl,--no-warn-rwx-segments -Wl,-Map,a02os.map

SRCS = src/start.s src/main.c src/lcd.c src/font.c src/sd.c src/diskio.c lib/fatfs/ff.c lib/fatfs/ffunicode.c \
	src/libc.c src/audio.c src/keys.c src/player.c src/ui.c

all: a02os.bin

a02os.elf: $(SRCS) src/*.h lib/fatfs/*.h link.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -lgcc -o $@

a02os.bin: a02os.elf
	$(OBJCOPY) -O binary -R .bss $< $@
	@$(SIZE) $<

# Path to a built checkout of https://github.com/ilyakurdyukov/actions_flash (with payload_arm/adfus.bin built)
ACTIONS_FLASH ?= ../../actions_flash

run: a02os.bin
	cd $(ACTIONS_FLASH) && ./actions_dump --wait 3 chip 2157 timeout $${TIMEOUT:-3600000} \
		simple_switch 0x118000 payload_arm/adfus.bin \
		simple_exec 0x120001 $(CURDIR)/a02os.bin -1

clean:
	rm -f a02os.elf a02os.bin a02os.map

.PHONY: all run clean
