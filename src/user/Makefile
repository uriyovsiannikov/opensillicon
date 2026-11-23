# Internal file rules; included from the root build graph.
$(BUILD_DIR)/user_init.o: src/user/init/init.c src/include/common/logo_pixels.h \
		src/user/init/service.h $(LIBC_ARCHIVE) other/logo.png | $(BUILD_DIR)
	$(CC) $(LIBC_CFLAGS) -Isrc/user -c $< -o $@

$(BUILD_DIR)/service.o: src/user/init/service.c src/user/init/service.h \
		$(LIBC_ARCHIVE) src/include/abi/exec.h | $(BUILD_DIR)
	$(CC) $(LIBC_CFLAGS) -Isrc/user -c $< -o $@

$(BUILD_DIR)/doas.o: src/user/doas/doas.c | $(BUILD_DIR)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/txtview.o: src/user/coreutils/txtview.c
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/txtview.elf: $(BUILD_DIR)/coreutils/txtview.o $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(CC) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o $(BUILD_DIR)/coreutils/txtview.o $(LIBC_ARCHIVE) -lgcc -o $@

$(BUILD_DIR)/coreutils/wavplay.o: src/user/coreutils/wavplay.c src/include/abi/audio.h
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/wavplay.elf: $(BUILD_DIR)/coreutils/wavplay.o $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(CC) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o $(BUILD_DIR)/coreutils/wavplay.o $(LIBC_ARCHIVE) -lgcc -o $@

$(BUILD_DIR)/coreutils/doas.elf: $(BUILD_DIR)/doas.o $(LIBC_BUILD_DIR)/crt1.o \
		$(LIBC_ARCHIVE) src/user/c_linker.ld
	mkdir -p $(@D)
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/doas.o $(LIBC_ARCHIVE) -o $@

$(BUILD_DIR)/svctl.o: src/user/init/svctl.c src/user/init/service.h | $(BUILD_DIR)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/svctl.elf: $(BUILD_DIR)/svctl.o $(LIBC_BUILD_DIR)/crt1.o \
		$(LIBC_ARCHIVE) src/user/c_linker.ld
	mkdir -p $(@D)
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/svctl.o $(LIBC_ARCHIVE) -o $@

$(BUILD_DIR)/user_shell.o: src/user/shell/shell.c src/include/abi/exec.h $(LIBC_ARCHIVE) | $(BUILD_DIR)
	$(CC) $(LIBC_CFLAGS) -Isrc/user -c $< -o $@

# Utilities using the public POSIX runtime need libc startup and headers.
POSIX_COREUTILS := ls rm uname sleep tail cat head wc stat which pwd mkdir kill basename dirname echo clear seq touch true false host ifconfig ping ps kldload kldunload poweroff reboot
DEVICECONF_COREUTILS := cpuconf usbconf pciconf blkconf audioconf netconf kldstat
$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .o,$(DEVICECONF_COREUTILS) deviceconf)): $(BUILD_DIR)/coreutils/%.o: src/user/coreutils/%.c src/user/coreutils/deviceconf.h $(LIBC_ARCHIVE) src/user/Makefile
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .elf,$(DEVICECONF_COREUTILS))): $(BUILD_DIR)/coreutils/%.elf: $(BUILD_DIR)/coreutils/%.o $(BUILD_DIR)/coreutils/deviceconf.o $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(CC) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o $< $(BUILD_DIR)/coreutils/deviceconf.o $(LIBC_ARCHIVE) -lgcc -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .o,$(POSIX_COREUTILS))): $(BUILD_DIR)/coreutils/%.o: src/user/coreutils/%.c $(LIBC_ARCHIVE) src/user/Makefile
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .elf,$(POSIX_COREUTILS))): $(BUILD_DIR)/coreutils/%.elf: $(BUILD_DIR)/coreutils/%.o $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(CC) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o $< $(LIBC_ARCHIVE) -lgcc -o $@

$(BUILD_DIR)/coreutils/fetch.o: src/tools/pkg/repo/_src/fetch/fetch.c $(LIBC_ARCHIVE) src/user/Makefile | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/fetch.elf: $(BUILD_DIR)/coreutils/fetch.o $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(CC) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o $(BUILD_DIR)/coreutils/fetch.o $(LIBC_ARCHIVE) -lgcc -o $@

$(BUILD_DIR)/init.elf: $(LIBC_BUILD_DIR)/crt1.o \
        $(BUILD_DIR)/user_init.o $(BUILD_DIR)/service.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/user_init.o $(BUILD_DIR)/service.o $(LIBC_ARCHIVE) -o $@

$(BUILD_DIR)/shell.elf: $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) \
		$(BUILD_DIR)/user_shell.o src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/user_shell.o $(LIBC_ARCHIVE) -o $@

$(BUILD_DIR)/populate_root: src/tools/populate_root.c src/kernel/fs/vfs/vfs.c \
		src/kernel/fs/silliconfs/silliconfs.c src/kernel/proc/fd.c src/include/kernel/vfs.h \
		src/include/kernel/silliconfs.h
	$(CC) -std=c11 -Wall -Wextra -Werror -Isrc/include -Isrc/limine \
		src/tools/populate_root.c src/kernel/fs/vfs/vfs.c src/kernel/fs/silliconfs/silliconfs.c \
		src/kernel/proc/fd.c -o $@

NANO_DIR := src/tools/pkg/repo/_src/nano
NANO_NCURSES_TARBALL := $(NANO_DIR)/upstream/deps/ncurses-6.5.tar.gz
NCURSES_DIR := $(BUILD_DIR)/nano/ncurses
NCURSES_ARCHIVE := $(NCURSES_DIR)/lib/libncurses.a

$(NCURSES_ARCHIVE): $(NANO_NCURSES_TARBALL) $(NANO_DIR)/build-ncurses.sh \
		$(LIBC_ARCHIVE) $(LIBC_BUILD_DIR)/crt1.o src/user/c_linker.ld
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
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/storage/esp.o: src/include/common/uuid.h

$(BUILD_DIR)/coreutils/newfs_silliconfs.o: src/user/coreutils/newfs_silliconfs.c src/user/storage/storage.h
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/newfs_silliconfs.elf: $(BUILD_DIR)/coreutils/newfs_silliconfs.o \
		$(BUILD_DIR)/storage/newfs.o $(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/coreutils/newfs_silliconfs.o $(BUILD_DIR)/storage/newfs.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o $(LIBC_ARCHIVE) -o $@

$(BUILD_DIR)/coreutils/gpart.o: src/user/coreutils/gpart.c src/user/storage/storage.h
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/gpart.elf: $(BUILD_DIR)/coreutils/gpart.o \
		$(BUILD_DIR)/storage/gpt_write.o $(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/coreutils/gpart.o $(BUILD_DIR)/storage/gpt_write.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o $(LIBC_ARCHIVE) -o $@

$(BUILD_DIR)/coreutils/installboot.o: src/user/coreutils/installboot.c src/user/storage/storage.h src/include/common/uuid.h
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/coreutils/installboot.elf: $(BUILD_DIR)/coreutils/installboot.o \
		$(BUILD_DIR)/storage/esp.o $(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/coreutils/installboot.o $(BUILD_DIR)/storage/esp.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o $(LIBC_ARCHIVE) -o $@

SYSUPGRADE_FLAGS := -Isrc/user/thirdparty/mbedtls/include \
	-Isrc/user/thirdparty/mbedtls/opensillicon \
	-DMBEDTLS_CONFIG_FILE='"opensillicon_mbedtls_config.h"'
$(BUILD_DIR)/coreutils/sysupgrade.o: src/user/coreutils/sysupgrade.c \
		src/user/storage/storage.h $(PKG_HEADERS) src/include/common/uuid.h \
		src/include/abi/disk.h
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -Isrc/user $(SYSUPGRADE_FLAGS) -c $< -o $@

SYSUPGRADE_PKG_OBJECTS := $(BUILD_DIR)/pkg/http.o $(BUILD_DIR)/pkg/tls.o \
	$(BUILD_DIR)/pkg/format.o $(BUILD_DIR)/pkg/sha256.o
$(BUILD_DIR)/coreutils/sysupgrade.elf: $(BUILD_DIR)/coreutils/sysupgrade.o \
		$(BUILD_DIR)/storage/esp_update.o $(BUILD_DIR)/storage/esp.o \
		$(BUILD_DIR)/storage/encoding.o $(BUILD_DIR)/storage/device.o \
		$(SYSUPGRADE_PKG_OBJECTS) $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) \
		$(BUILD_DIR)/mbedtls/.pkg-ready src/user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBC_BUILD_DIR)/crt1.o \
		$(BUILD_DIR)/coreutils/sysupgrade.o $(BUILD_DIR)/storage/esp_update.o \
		$(BUILD_DIR)/storage/esp.o $(BUILD_DIR)/storage/encoding.o \
		$(BUILD_DIR)/storage/device.o $(SYSUPGRADE_PKG_OBJECTS) \
		-Wl,--start-group $(PKG_TLS_LIBS) $(LIBC_ARCHIVE) \
		-Wl,--end-group -lgcc -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .o,mount umount sync)): $(BUILD_DIR)/coreutils/%.o: src/user/coreutils/%.c
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .elf,mount umount sync)): $(BUILD_DIR)/coreutils/%.elf: $(BUILD_DIR)/coreutils/%.o $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE)
	$(LD) $(USER_C_LDFLAGS) $(LIBC_BUILD_DIR)/crt1.o $< $(LIBC_ARCHIVE) -o $@

$(BUILD_DIR)/scinstall/scinstall.o: src/user/scinstall/scinstall.c src/user/scinstall/ui.h src/user/storage/storage.h src/user/storage/install_copy.h src/include/common/uuid.h
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/scinstall/ui.o: src/user/scinstall/ui.c src/user/scinstall/ui.h $(NCURSES_ARCHIVE)
	mkdir -p $(@D)
	$(CC) $(LIBC_CFLAGS) -I$(NCURSES_DIR)/include -c $< -o $@

SCINSTALL_OBJECTS := $(BUILD_DIR)/scinstall/scinstall.o $(BUILD_DIR)/scinstall/ui.o \
	$(addprefix $(BUILD_DIR)/storage/,install_copy.o encoding.o device.o gpt_write.o newfs.o)

$(BUILD_DIR)/scinstall/scinstall.elf: $(SCINSTALL_OBJECTS) \
		$(NCURSES_ARCHIVE) $(LIBC_BUILD_DIR)/crt1.o $(LIBC_ARCHIVE) src/user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBC_BUILD_DIR)/crt1.o $(SCINSTALL_OBJECTS) \
		-Wl,--start-group $(NCURSES_ARCHIVE) $(LIBC_ARCHIVE) -Wl,--end-group -lgcc -o $@

$(addprefix $(BUILD_DIR)/coreutils/,$(addsuffix .o,mount umount sync)): $(LIBC_ARCHIVE) src/mk/common.mk
