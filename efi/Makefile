# GNU-EFI development package: gnu-efi (Arch), gnu-efi (Debian/Ubuntu).
EFI_PREFIX ?= /usr
EFI_INC ?= $(EFI_PREFIX)/include/efi
EFI_LIB ?= $(EFI_PREFIX)/lib
CC = gcc
LD = ld
OBJCOPY = objcopy
CFLAGS = -I$(EFI_INC) -I$(EFI_INC)/x86_64 -I$(EFI_INC)/protocol \
         -fpic -fshort-wchar -mno-red-zone -fno-stack-protector \
         -fno-strict-aliasing -ffreestanding -fno-builtin \
         -DEFI_FUNCTION_WRAPPER -Wall -Wextra -Werror -O2

.PHONY: all clean
all: build/bootswitchx64.efi

build:
	mkdir -p $@

build/bridge.o: bridge.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/bridge.so: build/bridge.o
	$(LD) -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds \
	  -shared -Bsymbolic $(EFI_LIB)/crt0-efi-x86_64.o $< \
	  -L$(EFI_LIB) -lefi -lgnuefi -o $@

build/bootswitchx64.efi: build/bridge.so Makefile
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .dynstr \
	  -j .rel -j .rela -j .reloc --input-target=elf64-x86-64 \
	  --output-target=pei-x86-64 --subsystem=10 $< $@

clean:
	rm -rf build
