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

.DELETE_ON_ERROR:
.PHONY: all test clean
all: $(OUT)/TaruGxGop.efi $(OUT)/TaruGxGop.rom $(OUT)/TaruGxTest.efi $(OUT)/TaruGxProbe.efi $(OUT)/TaruGxAutoProbe.efi $(OUT)/TaruGxGop.mem
$(OUT):
	mkdir -p $@
$(OUT)/%.o: %.c blt.h dxe_memory.h gop_trace.h | $(OUT)
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
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -DTARUGX_EARLY_TEXT_INIT ../tests/uefi_driver_test.c blt.c -o $(OUT)/driver-early-test
	$(OUT)/driver-early-test
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined ../tests/uefi_text_init_test.c -o $(OUT)/text-init-test
	$(OUT)/text-init-test
	python3 ../tests/test_efi_rom.py
	python3 -m unittest discover -s ../tests -p test_rom_mem.py
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -I. splash.c ../tests/uefi_splash_test.c -o $(OUT)/splash-test
	$(OUT)/splash-test
	$(CC) $(CFLAGS) -Os -g -fsanitize=address,undefined -DTARUGX_EARLY_TEXT_INIT \
	 -DTARUGX_BOOT_SPLASH ../tests/uefi_driver_test.c blt.c splash.c -o $(OUT)/driver-splash-test
	$(OUT)/driver-splash-test
clean:
	rm -f $(OUT)/*.o $(OUT)/TaruGxGop.so $(OUT)/TaruGxGop.efi $(OUT)/TaruGxGop.rom $(OUT)/TaruGxGop.mem \
	 $(OUT)/TaruGxTest.so $(OUT)/TaruGxTest.efi $(OUT)/TaruGxProbe.so $(OUT)/TaruGxProbe.efi $(OUT)/TaruGxAutoProbe.so $(OUT)/TaruGxAutoProbe.efi $(OUT)/TaruGxConsoleProbe.so $(OUT)/TaruGxConsoleProbe.efi $(OUT)/blt-test $(OUT)/driver-test $(OUT)/text-init-test $(OUT)/driver-early-test $(OUT)/TaruGxEarlyText.so $(OUT)/TaruGxEarlyText.efi $(OUT)/TaruGxEarlyText.rom $(OUT)/TaruGxSplash.so $(OUT)/TaruGxSplash.efi $(OUT)/TaruGxSplash.rom $(OUT)/splash-test $(OUT)/driver-splash-test

# Diagnostic variant: record results, then reboot through the normal BootOrder.
$(OUT)/boot_probe.o: boot_test.c dxe_memory.h gop_trace.h | $(OUT)
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
$(OUT)/boot_auto_probe.o: boot_test.c dxe_memory.h gop_trace.h | $(OUT)
	$(CC) $(CFLAGS) -DTARUGX_DIAGNOSTIC_RESET -DTARUGX_AUTO_ROM_TEST -c $< -o $@
$(OUT)/TaruGxAutoProbe.so: $(OUT)/boot_auto_probe.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxAutoProbe.efi: $(OUT)/TaruGxAutoProbe.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-app-x86_64 $< $@

# Explicit opt-in: invokes firmware SimpleTextOutput; keep normal probe read-only.
$(OUT)/boot_console_probe.o: boot_test.c dxe_memory.h gop_trace.h | $(OUT)
	$(CC) $(CFLAGS) -DTARUGX_DIAGNOSTIC_RESET -DTARUGX_AUTO_ROM_TEST -DTARUGX_CONSOLE_TEST -c $< -o $@
$(OUT)/TaruGxConsoleProbe.so: $(OUT)/boot_console_probe.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxConsoleProbe.efi: $(OUT)/TaruGxConsoleProbe.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-app-x86_64 $< $@

# Optional per-output text initialization; separate artifact from normal ROM.
$(OUT)/tarugx_gop_early.o: tarugx_gop.c blt.h dxe_memory.h gop_trace.h text_init.h | $(OUT)
	$(CC) $(CFLAGS) -DTARUGX_EARLY_TEXT_INIT -c $< -o $@
$(OUT)/TaruGxEarlyText.so: $(OUT)/tarugx_gop_early.o $(OUT)/blt.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxEarlyText.efi: $(OUT)/TaruGxEarlyText.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-bsdrv-x86_64 $< $@
$(OUT)/TaruGxEarlyText.rom: $(OUT)/TaruGxEarlyText.efi $(ROOT)/tools/make_efi_rom.py
	python3 $(ROOT)/tools/make_efi_rom.py $< $@

# Adopted v3 startup logo plus the tested host-specific text initialization.
$(OUT)/tarugx_gop_splash.o: tarugx_gop.c blt.h dxe_memory.h gop_trace.h text_init.h splash.h | $(OUT)
	$(CC) $(CFLAGS) -Os -DTARUGX_EARLY_TEXT_INIT -DTARUGX_BOOT_SPLASH -c $< -o $@
$(OUT)/splash.o: splash.c splash.h blt.h boot_logo_data.h | $(OUT)
	$(CC) $(CFLAGS) -Os -c $< -o $@
$(OUT)/blt_small.o: blt.c blt.h | $(OUT)
	$(CC) $(CFLAGS) -Os -c $< -o $@
$(OUT)/TaruGxSplash.so: $(OUT)/tarugx_gop_splash.o $(OUT)/blt_small.o $(OUT)/splash.o
	ld -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
	 $(EFI_LIB)/crt0-efi-x86_64.o $^ -L$(EFI_LIB) -lefi -lgnuefi -o $@
$(OUT)/TaruGxSplash.efi: $(OUT)/TaruGxSplash.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
	 -j .rela -j .reloc --target=efi-bsdrv-x86_64 --strip-all $< $@
$(OUT)/TaruGxSplash.rom: $(OUT)/TaruGxSplash.efi $(ROOT)/tools/make_efi_rom.py
	python3 $(ROOT)/tools/make_efi_rom.py $< $@
	python3 -c 'from pathlib import Path; assert Path("$@").stat().st_size == 65536, "Splash ROM exceeds hardware aperture"'

.PHONY: logo
logo:
	python3 ../tools/make_boot_logo.py ../assets/branding/tarugx-logo-concept-v3.png boot_logo_data.h
