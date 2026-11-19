# Internal file rules; included from the root build graph.
$(BUILD_DIR)/user_start.o: src/user/start.S src/user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_os.o: src/user/include/os.c src/user/include/os.h src/include/abi/syscall.h src/include/abi/framebuffer.h src/include/abi/input.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_path.o: src/user/path/path.c src/user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_init.o: src/user/init/init.c src/user/init/logo_pixels.h \
		src/user/init/service.h src/user/include/os.h other/logo.png | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/service.o: src/user/init/service.c src/user/init/service.h \
		src/user/include/os.h src/include/abi/syscall.h | $(BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -Isrc/user -c $< -o $@

$(BUILD_DIR)/doas.o: src/user/doas/doas.c | $(BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/doas.elf: $(BUILD_DIR)/doas.o $(LIBOS_BUILD_DIR)/crt0.o \
		$(LIBOS_ARCHIVE) src/user/c_linker.ld
	mkdir -p $(@D)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/doas.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/svctl.o: src/user/init/svctl.c src/user/init/service.h | $(BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/svctl.elf: $(BUILD_DIR)/svctl.o $(LIBOS_BUILD_DIR)/crt0.o \
		$(LIBOS_ARCHIVE) src/user/c_linker.ld
	mkdir -p $(@D)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/svctl.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/user_shell.o: src/user/shell/shell.c src/user/include/os.h | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/%.o: src/user/coreutils/%.c src/user/include/os.h \
		src/user/coreutils/deviceconf.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/fetch.o: src/tools/pkg/repo/_src/fetch/fetch.c src/user/include/os.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/%.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/%.o src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/$*.o -o $@

$(BUILD_DIR)/coreutils/host.o: src/user/coreutils/host.c src/user/include/os.h \
		src/user/libos/include/netdb.h src/user/libos/include/netinet/in.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -Isrc/user/libos/include -c $< -o $@

$(BUILD_DIR)/coreutils/host.elf: $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/host.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/coreutils/host.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/init.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_init.o $(BUILD_DIR)/service.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_init.o $(BUILD_DIR)/service.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/shell.elf: $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_os.o \
		$(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shell.o src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(BUILD_DIR)/user_start.o \
		$(BUILD_DIR)/user_os.o $(BUILD_DIR)/user_path.o \
		$(BUILD_DIR)/user_shell.o -o $@

$(BUILD_DIR)/populate_root: src/tools/populate_root.c src/kernel/fs/vfs/vfs.c \
		src/kernel/fs/silliconfs/silliconfs.c src/kernel/proc/fd.c src/include/kernel/vfs.h \
		src/include/kernel/silliconfs.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Isrc/include -Isrc/limine \
		src/tools/populate_root.c src/kernel/fs/vfs/vfs.c src/kernel/fs/silliconfs/silliconfs.c \
		src/kernel/proc/fd.c -o $@

LIBOS_SRC_DIR := src/user/libos/src

# Sources live in subsystem subdirectories; the original order is preserved
# because archive member order matters when linking against a static library.
LIBOS_SOURCES := \
	text/iconv fs/mount crt/syscall proc/unistd proc/mman text/string \
	fs/stat stdlib/allocator fs/dirent io/stdio net/socket io/select io/uio \
	io/fcntl io/pipe io/pty io/termios time/time time/strftime io/poll \
	proc/signal io/ioctl proc/uname proc/env math/math stdlib/random \
	stdlib/stdlib proc/passwd io/termcap text/regex fs/path stdlib/getopt \
	stdlib/getopt_long
LIBOS_OBJECTS := $(addprefix $(LIBOS_BUILD_DIR)/,$(addsuffix .o,$(LIBOS_SOURCES)))
LIBOS_HEADERS := $(LIBOS_SRC_DIR)/internal.h src/include/abi/syscall.h \
	$(wildcard src/user/libos/include/* src/user/libos/include/sys/*)

# The CRT and ABI stubs keep their flat object names: link rules reference
# $(LIBOS_BUILD_DIR)/crt0.o directly.
$(LIBOS_BUILD_DIR)/setjmp.o: $(LIBOS_SRC_DIR)/crt/setjmp.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_BUILD_DIR):
	mkdir -p $@

$(LIBOS_BUILD_DIR)/%.o: $(LIBOS_SRC_DIR)/%.c $(LIBOS_HEADERS) | $(LIBOS_BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(LIBOS_BUILD_DIR)/math/math.o: $(LIBOS_SRC_DIR)/math/math.c $(LIBOS_HEADERS) | $(LIBOS_BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(filter-out -mno-sse -mno-sse2 -msoft-float,$(LIBOS_CFLAGS)) \
		-msse2 -c $< -o $@

$(LIBOS_BUILD_DIR)/io/stdio.o: $(LIBOS_SRC_DIR)/io/stdio.c $(LIBOS_HEADERS) | $(LIBOS_BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(filter-out -mno-sse -mno-sse2 -msoft-float,$(LIBOS_CFLAGS)) \
		-msse2 -c $< -o $@

$(LIBOS_BUILD_DIR)/crt0.o: $(LIBOS_SRC_DIR)/crt/crt0.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_BUILD_DIR)/syscall_abi.o: $(LIBOS_SRC_DIR)/crt/syscall_abi.S | $(LIBOS_BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -c $< -o $@

$(LIBOS_ARCHIVE): $(LIBOS_OBJECTS) $(LIBOS_BUILD_DIR)/syscall_abi.o $(LIBOS_BUILD_DIR)/setjmp.o
	ar rcs $@ $^

NANO_DIR := src/tools/pkg/repo/_src/nano
NANO_NCURSES_TARBALL := $(NANO_DIR)/upstream/deps/ncurses-6.5.tar.gz
NCURSES_DIR := $(BUILD_DIR)/nano/ncurses
NCURSES_ARCHIVE := $(NCURSES_DIR)/lib/libncurses.a

$(NCURSES_ARCHIVE): $(NANO_NCURSES_TARBALL) $(NANO_DIR)/build-ncurses.sh \
		$(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o src/user/c_linker.ld
	sh $(NANO_DIR)/build-ncurses.sh
	touch $(NCURSES_ARCHIVE)

# Optional CA for a private HTTPS package repository.
PKG_CA_CERT ?=

SERVICE_CONFIGS ?= $(wildcard src/user/init/*.conf)

ROOT_PAYLOAD = $(BUILD_DIR)/kernel-boot.elf $(BUILD_DIR)/initramfs.cpio \
		src/limine/BOOTX64.EFI $(BUILD_DIR)/init.elf $(BUILD_DIR)/shell.elf \
		$(MODULE_OBJECTS) \
		$(SERVICE_CONFIGS) $(PKG_CA_CERT) $(BUILD_DIR)/coreutils/svctl.elf $(BUILD_DIR)/scinstall/scinstall.elf \
		src/user/thirdparty/mbedtls/opensillicon/ca-certificates.crt \
		$(COREUTILS_ELFS)

# Include room for installed graphical packages and application data (Minecraft
# writes several MiB of region files even while creating its first world).
# The live root is loaded into RAM, so keep its reserve smaller than the disk's.
LIVE_ROOT_RESERVE_MIB ?= 128
ROOT_RESERVE_MIB ?= 256
$(sort $(ROOT_IMAGE) $(LIVE_ROOT_IMAGE)): $(ROOT_PAYLOAD) $(BUILD_DIR)/populate_root \
		src/user/Makefile src/tools/packers/mksilliconfs.py | $(BUILD_DIR)
	@set -eu; image_tmp=$$(mktemp '$@.tmp.XXXXXX'); \
	trap 'rm -f "$$image_tmp"' EXIT HUP INT TERM; \
	python3 src/tools/packers/mksilliconfs.py "$$image_tmp" --inodes 1024 \
		--reserve-mib $(if $(filter $(LIVE_ROOT_IMAGE),$@),$(LIVE_ROOT_RESERVE_MIB),$(ROOT_RESERVE_MIB)) \
		--size-from $(foreach file,$(ROOT_PAYLOAD),'$(file)'); \
	$(BUILD_DIR)/populate_root "$$image_tmp" $(BUILD_DIR)/init.elf \
		$(BUILD_DIR)/shell.elf \
		$(if $(PKG_CA_CERT),--pkg-ca '$(PKG_CA_CERT)') \
		--boot-files src/limine/BOOTX64.EFI $(BUILD_DIR)/kernel-boot.elf $(BUILD_DIR)/initramfs.cpio \
		$(foreach module,$(MODULE_OBJECTS),--module $(module)) \
		--svctl $(BUILD_DIR)/coreutils/svctl.elf --scinstall $(BUILD_DIR)/scinstall/scinstall.elf \
		$(foreach config,$(SERVICE_CONFIGS),--service '$(config)') --coreutils $(COREUTILS_ELFS); \
	mv -f "$$image_tmp" '$@'

$(BUILD_DIR)/storage/%.o: src/user/storage/%.c src/user/storage/storage.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/storage/esp.o: src/include/common/uuid.h

$(BUILD_DIR)/coreutils/newfs_silliconfs.o: src/user/coreutils/newfs_silliconfs.c src/user/storage/storage.h
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/newfs_silliconfs.elf: $(BUILD_DIR)/coreutils/newfs_silliconfs.o \
		$(BUILD_DIR)/storage/newfs.o $(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/coreutils/newfs_silliconfs.o $(BUILD_DIR)/storage/newfs.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/tests/newfs_guest.o: src/tests/newfs_guest.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/newfs_guest.elf: $(BUILD_DIR)/tests/newfs_guest.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o $< $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/coreutils/gpart.o: src/user/coreutils/gpart.c src/user/storage/storage.h
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/gpart.elf: $(BUILD_DIR)/coreutils/gpart.o \
		$(BUILD_DIR)/storage/gpt_write.o $(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/coreutils/gpart.o $(BUILD_DIR)/storage/gpt_write.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/tests/gpart_guest.o: src/tests/gpart_guest.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/gpart_guest.elf: $(BUILD_DIR)/tests/gpart_guest.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o $< $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/coreutils/installboot.o: src/user/coreutils/installboot.c src/user/storage/storage.h src/include/common/uuid.h
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/installboot.elf: $(BUILD_DIR)/coreutils/installboot.o \
		$(BUILD_DIR)/storage/esp.o $(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/coreutils/installboot.o $(BUILD_DIR)/storage/esp.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o $(LIBOS_ARCHIVE) -o $@

SYSUPGRADE_FLAGS := -Isrc/user/thirdparty/mbedtls/include \
	-Isrc/user/thirdparty/mbedtls/opensillicon \
	-DMBEDTLS_CONFIG_FILE='"opensillicon_mbedtls_config.h"'
$(BUILD_DIR)/coreutils/sysupgrade.o: src/user/coreutils/sysupgrade.c \
		src/user/storage/storage.h $(PKG_HEADERS) src/include/common/uuid.h \
		src/include/abi/disk.h
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -Isrc/user $(SYSUPGRADE_FLAGS) -c $< -o $@

SYSUPGRADE_PKG_OBJECTS := $(BUILD_DIR)/pkg/http.o $(BUILD_DIR)/pkg/tls.o \
	$(BUILD_DIR)/pkg/format.o $(BUILD_DIR)/pkg/sha256.o
$(BUILD_DIR)/coreutils/sysupgrade.elf: $(BUILD_DIR)/coreutils/sysupgrade.o \
		$(BUILD_DIR)/storage/esp_update.o $(BUILD_DIR)/storage/esp.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(SYSUPGRADE_PKG_OBJECTS) $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) \
		$(BUILD_DIR)/mbedtls/.pkg-ready src/user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/coreutils/sysupgrade.o $(BUILD_DIR)/storage/esp_update.o \
		$(BUILD_DIR)/storage/esp.o $(BUILD_DIR)/storage/encoding.o \
		$(BUILD_DIR)/storage/device.o $(SYSUPGRADE_PKG_OBJECTS) \
		-Wl,--start-group $(PKG_TLS_LIBS) $(LIBOS_ARCHIVE) \
		-Wl,--end-group -lgcc -o $@

$(BUILD_DIR)/tests/installboot_guest.o: src/tests/installboot_guest.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/installboot_guest.elf: $(BUILD_DIR)/tests/installboot_guest.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o $< $(LIBOS_ARCHIVE) -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .o,mount umount sync)): $(BUILD_DIR)/coreutils/%.o: src/user/coreutils/%.c
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .elf,mount umount sync)): $(BUILD_DIR)/coreutils/%.elf: $(BUILD_DIR)/coreutils/%.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o $< $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/tests/mount_guest.o: src/tests/mount_guest.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/mount_guest.elf: $(BUILD_DIR)/tests/mount_guest.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o $< $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/tests/copy_guest.o: src/tests/copy_guest.c src/user/storage/install_copy.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/copy_guest.elf: $(BUILD_DIR)/tests/copy_guest.o $(BUILD_DIR)/storage/install_copy.o $(BUILD_DIR)/storage/encoding.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE)
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o $< $(BUILD_DIR)/storage/install_copy.o $(BUILD_DIR)/storage/encoding.o $(LIBOS_ARCHIVE) -o $@

$(BUILD_DIR)/scinstall/scinstall.o: src/user/scinstall/scinstall.c src/user/scinstall/ui.h src/user/storage/storage.h src/user/storage/install_copy.h src/include/common/uuid.h
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/scinstall/ui.o: src/user/scinstall/ui.c src/user/scinstall/ui.h $(NCURSES_ARCHIVE)
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -I$(NCURSES_DIR)/include -c $< -o $@

SCINSTALL_OBJECTS := $(BUILD_DIR)/scinstall/scinstall.o $(BUILD_DIR)/scinstall/ui.o \
	$(addprefix $(BUILD_DIR)/storage/,install_copy.o encoding.o device.o gpt_write.o newfs.o)

$(BUILD_DIR)/scinstall/scinstall.elf: $(SCINSTALL_OBJECTS) \
		$(NCURSES_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o $(SCINSTALL_OBJECTS) \
		-Wl,--start-group $(NCURSES_ARCHIVE) $(LIBOS_ARCHIVE) -Wl,--end-group -lgcc -o $@
