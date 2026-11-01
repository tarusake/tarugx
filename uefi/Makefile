ROOT := $(abspath ..)
OUT := $(ROOT)/build/uefi
EFI_ROOT ?= /usr
EFI_INC := $(EFI_ROOT)/include/efi
EFI_LIB := $(EFI_ROOT)/lib
CC := gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -fpic -ffreestanding \
 -fshort-wchar -fno-stack-protector -fno-stack-check -mno-red-zone \
 -maccumulate-outgoing-args -DGNU_EFI_USE_MS_ABI \
 -I$(EFI_INC) -I$(EFI_INC)/x86_64 -I$(EFI_INC)/protocol

.PHONY: all test clean
all: $(OUT)/TaruGxGop.efi $(OUT)/TaruGxGop.rom $(OUT)/TaruGxTest.efi $(OUT)/TaruGxProbe.efi $(OUT)/TaruGxAutoProbe.efi $(OUT)/TaruGxGop.mem
$(OUT):
	mkdir -p $@
$(OUT)/%.o: %.c blt.h dxe_memory.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/TaruGxGop.so: $(OUT)/tarugx_gop.o $(OUT)/blt.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxGop.efi: $(OUT)/TaruGxGop.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-bsdrv-x86_64 $< $@
$(OUT)/TaruGxGop.rom: $(OUT)/TaruGxGop.efi $(ROOT)/tools/make_efi_rom.py
	python3 $(ROOT)/tools/make_efi_rom.py $< $@
$(OUT)/TaruGxTest.so: $(OUT)/boot_test.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxTest.efi: $(OUT)/TaruGxTest.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-app-x86_64 $< $@
test: | $(OUT)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -I. blt.c ../tests/uefi_blt_test.c -o $(OUT)/blt-test
	$(OUT)/blt-test
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined ../tests/uefi_driver_test.c blt.c -o $(OUT)/driver-test
	$(OUT)/driver-test
	python3 ../tests/test_efi_rom.py
	python3 -m unittest discover -s ../tests -p test_rom_mem.py
clean:
	rm -f $(OUT)/*.o $(OUT)/TaruGxGop.so $(OUT)/TaruGxGop.efi $(OUT)/TaruGxGop.rom $(OUT)/TaruGxGop.mem \
	 $(OUT)/TaruGxTest.so $(OUT)/TaruGxTest.efi $(OUT)/TaruGxProbe.so $(OUT)/TaruGxProbe.efi $(OUT)/TaruGxAutoProbe.so $(OUT)/TaruGxAutoProbe.efi $(OUT)/blt-test $(OUT)/driver-test

# Diagnostic variant: record results, then reboot through the normal BootOrder.
$(OUT)/boot_probe.o: boot_test.c dxe_memory.h | $(OUT)
	$(CC) $(CFLAGS) -DTARUGX_DIAGNOSTIC_RESET -c $< -o $@
$(OUT)/TaruGxProbe.so: $(OUT)/boot_probe.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxProbe.efi: $(OUT)/TaruGxProbe.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-app-x86_64 $< $@

$(OUT)/TaruGxGop.mem: $(OUT)/TaruGxGop.rom $(ROOT)/tools/make_rom_mem.py
	python3 $(ROOT)/tools/make_rom_mem.py $< $@ --aperture-size 65536

# Observe firmware ROM discovery; never load the GOP driver from ESP.
$(OUT)/boot_auto_probe.o: boot_test.c dxe_memory.h | $(OUT)
	$(CC) $(CFLAGS) -DTARUGX_DIAGNOSTIC_RESET -DTARUGX_AUTO_ROM_TEST -c $< -o $@
$(OUT)/TaruGxAutoProbe.so: $(OUT)/boot_auto_probe.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxAutoProbe.efi: $(OUT)/TaruGxAutoProbe.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-app-x86_64 $< $@
