.PHONY: musl musl-check
musl: $(LIBC_ARCHIVE)
musl-check: $(NCURSES_ARCHIVE) $(BUILD_DIR)/modules/e1000.ko musl $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/populate_root $(BUILD_DIR)/shell.elf $(BUILD_DIR)/initramfs.cpio
	sh src/user/thirdparty/musl/opensillicon/check.sh

MUSL_PORT := src/user/thirdparty/musl/opensillicon
MUSL_INPUTS := $(shell find src/user/thirdparty/musl -type f)
$(LIBC_BUILD_DIR)/libc.a: $(MUSL_INPUTS) $(wildcard src/include/abi/*.h)
	+CC="$(CC)" sh $(MUSL_PORT)/build.sh
	@touch $@

$(LIBC_BUILD_DIR)/crt1.o: $(LIBC_BUILD_DIR)/libc.a
	@test -f $@

# Ensure generated musl headers exist before compiling user C programs.
$(BUILD_DIR)/service.o $(BUILD_DIR)/doas.o $(BUILD_DIR)/svctl.o $(BUILD_DIR)/coreutils/host.o: $(LIBC_ARCHIVE) src/mk/common.mk src/user/Makefile
$(BUILD_DIR)/coreutils/%.o $(BUILD_DIR)/storage/%.o $(BUILD_DIR)/scinstall/%.o $(BUILD_DIR)/pkg/%.o: $(LIBC_ARCHIVE) src/mk/common.mk src/user/Makefile

$(BUILD_DIR)/user_init.o: $(LIBC_ARCHIVE) src/mk/common.mk

MUSL_USER_OBJECTS := $(foreach dir,coreutils storage scinstall pkg,\
 $(addprefix $(BUILD_DIR)/$(dir)/,$(notdir $(patsubst %.c,%.o,$(wildcard src/user/$(dir)/*.c)))))
$(MUSL_USER_OBJECTS): $(LIBC_ARCHIVE) src/mk/common.mk

src/build/musl/sysroot/include/%: $(LIBC_ARCHIVE)
	@test -f $@

.PHONY: musl-system-check
musl-system-check: live-iso
	python3 src/user/thirdparty/musl/opensillicon/system-check.py
