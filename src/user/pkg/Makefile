# Included from the root graph. The host tools and native client share the
# archive parser and SHA-256 implementation; the server has no guest runtime.
PKG_DIR := src/user/pkg
PKG_COMMON := $(PKG_DIR)/format.c $(PKG_DIR)/sha256.c $(PKG_DIR)/io.c
PKG_CLIENT := $(PKG_DIR)/pkg.c $(PKG_DIR)/http.c $(PKG_DIR)/tls.c $(PKG_COMMON)
PKG_HEADERS := $(wildcard $(PKG_DIR)/*.h)
PKG_OBJECTS := $(patsubst $(PKG_DIR)/%.c,$(BUILD_DIR)/pkg/%.o,$(PKG_CLIENT))
PKG_TLS_FLAGS := -Isrc/user/thirdparty/mbedtls/include \
	-Isrc/user/thirdparty/mbedtls/opensillicon \
	-DMBEDTLS_CONFIG_FILE='"opensillicon_mbedtls_config.h"'
PKG_TLS_LIBS := $(addprefix $(BUILD_DIR)/mbedtls/,libmbedtls.a libmbedx509.a libmbedcrypto.a)
PKG_HOST_TLS_LIBS := $(addprefix $(BUILD_DIR)/mbedtls-host/,libmbedtls.a libmbedx509.a libmbedcrypto.a)
.PHONY: $(BUILD_DIR)/mbedtls/.pkg-ready $(BUILD_DIR)/mbedtls-host/.pkg-ready

# Generic hook used by package recipes created with `make pkg-new`.
.PHONY: pkg-from-stage
pkg-from-stage: $(BUILD_DIR)/pkg/pkg-build
	@test -n "$(PKG_NAME)" -a -n "$(PKG_VERSION)" -a -n "$(PKG_STAGE)" || \
		{ echo 'pkg-from-stage requires PKG_NAME, PKG_VERSION and PKG_STAGE' >&2; exit 2; }
	$(BUILD_DIR)/pkg/pkg-build "$(PKG_NAME)" "$(PKG_VERSION)" "$(PKG_STAGE)" \
		src/tools/pkg/repo/_pkges/$(PKG_NAME)-$(PKG_VERSION).spkg "$(PKG_DESCRIPTION)"

$(BUILD_DIR)/mbedtls/.pkg-ready:
	sh src/tools/builders/build_mbedtls.sh
$(BUILD_DIR)/mbedtls-host/.pkg-ready:
	sh src/tools/builders/build_mbedtls.sh host

HOST_CC ?= cc
PKG_HOST_FLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror \
	-D_POSIX_C_SOURCE=200809L -DPKG_HOST -I$(PKG_DIR)

$(BUILD_DIR)/pkg/%.o: $(PKG_DIR)/%.c $(PKG_HEADERS) src/mk/common.mk
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) $(PKG_TLS_FLAGS) -MMD -MP -I$(PKG_DIR) -c $< -o $@

$(BUILD_DIR)/coreutils/pkg.elf: $(PKG_OBJECTS) $(LIBOS_BUILD_DIR)/crt0.o \
		$(LIBOS_ARCHIVE) $(BUILD_DIR)/mbedtls/.pkg-ready src/user/c_linker.ld
	mkdir -p $(@D)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(PKG_OBJECTS) -Wl,--start-group $(PKG_TLS_LIBS) $(LIBOS_ARCHIVE) \
		-Wl,--end-group -lgcc -o $@

$(BUILD_DIR)/pkg/pkg-host: $(PKG_CLIENT) $(PKG_HEADERS) $(BUILD_DIR)/mbedtls-host/.pkg-ready
	mkdir -p $(@D)
	$(HOST_CC) $(PKG_HOST_FLAGS) $(PKG_TLS_FLAGS) $(PKG_CLIENT) $(PKG_HOST_TLS_LIBS) -o $@

$(BUILD_DIR)/pkg/pkg-build: src/tools/pkg/build.c $(PKG_COMMON) $(PKG_HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(PKG_HOST_FLAGS) $< $(PKG_COMMON) -o $@

$(BUILD_DIR)/pkg/pkg-server: src/tools/pkg/server/server.c $(PKG_COMMON) $(PKG_DIR)/tls.c $(PKG_HEADERS) $(BUILD_DIR)/mbedtls-host/.pkg-ready
	mkdir -p $(@D)
	$(HOST_CC) $(PKG_HOST_FLAGS) $(PKG_TLS_FLAGS) $< $(PKG_COMMON) $(PKG_DIR)/tls.c \
		$(PKG_HOST_TLS_LIBS) -o $@

$(BUILD_DIR)/pkg/hello-system-stage/usr/bin/hello: src/tools/pkg/repo/_src/hello/hello.c \
		$(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	mkdir -p $(@D)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $(BUILD_DIR)/pkg/hello.o
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/pkg/hello.o $(LIBOS_ARCHIVE) -o $@

src/tools/pkg/repo/_pkges/hello-1.0.0.spkg: $(BUILD_DIR)/pkg/hello-system-stage/usr/bin/hello \
		$(BUILD_DIR)/pkg/pkg-build
	$(BUILD_DIR)/pkg/pkg-build hello 1.0.0 $(BUILD_DIR)/pkg/hello-system-stage $@ \
		'Hello program for OpenSillicon'

# X11, NetSurf and Doom builds belong to package targets, not the base root graph.
# Doom (doomgeneric) with the X11 back-end from opensillicon/.
DOOM_DIR := src/tools/pkg/repo/_src/doom
DOOM_BUILD_DIR := $(BUILD_DIR)/doom
DOOM_RESX ?= 320
DOOM_RESY ?= 200

X11_PREFIX ?= $(BUILD_DIR)/x11/prefix

DOOM_VENDOR_SOURCES := dummy am_map doomdef doomstat dstrings d_event d_items \
	d_iwad d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff \
	info i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio \
	m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random \
	p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj \
	p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick \
	p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds \
	statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum \
	w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric
DOOM_OBJECTS := $(addprefix $(DOOM_BUILD_DIR)/,$(addsuffix .o,$(DOOM_VENDOR_SOURCES))) \
	$(DOOM_BUILD_DIR)/doomgeneric_opensillicon.o

# Upstream is C89-era code: keep its warnings visible but do not let them fail
# the build.  Doom prints floats through varargs, so SSE must stay enabled like
# the other X11 clients; only the kernel and the core libos are SSE-free.
DOOM_CFLAGS := $(filter-out -mno-sse -mno-sse2 -mno-mmx -msoft-float,$(LIBOS_CFLAGS)) \
	-Wno-error -I$(DOOM_DIR)/doomgeneric -I$(X11_PREFIX)/include \
	-idirafter /usr/include -DOPEN_SILLICON \
	-DDOOMGENERIC_RESX=$(DOOM_RESX) -DDOOMGENERIC_RESY=$(DOOM_RESY)

X11_DOOM_LIBS := $(X11_PREFIX)/lib/libX11.a $(X11_PREFIX)/lib/libxcb.a \
	$(X11_PREFIX)/lib/libXau.a

$(DOOM_BUILD_DIR):
	mkdir -p $@

$(DOOM_BUILD_DIR)/%.o: $(DOOM_DIR)/doomgeneric/%.c \
		$(wildcard $(DOOM_DIR)/doomgeneric/*.h) | $(DOOM_BUILD_DIR)
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

$(DOOM_BUILD_DIR)/doomgeneric_opensillicon.o: \
		$(DOOM_DIR)/opensillicon/doomgeneric_opensillicon.c \
		$(wildcard $(DOOM_DIR)/doomgeneric/*.h) | $(DOOM_BUILD_DIR)
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

# The X11 client libraries come from the shared X11 build prefix.
$(BUILD_DIR)/doom/doom: $(DOOM_OBJECTS) $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) \
		$(BUILD_DIR)/x11/.apps-ready
	$(LD) $(USER_C_LDFLAGS) $(LIBOS_BUILD_DIR)/crt0.o $(DOOM_OBJECTS) \
		--start-group $(LIBOS_ARCHIVE) $(X11_DOOM_LIBS) --end-group -o $@

LIBZ_SOURCES := gzlib gzread gzclose adler32 crc32 deflate trees inflate inffast inftrees zutil
LIBZ_OBJECTS := $(addprefix $(LIBZ_BUILD_DIR)/,$(addsuffix .o,$(LIBZ_SOURCES)))

$(LIBZ_BUILD_DIR):
	mkdir -p $@

$(LIBZ_BUILD_DIR)/%.o: src/tools/pkg/repo/_src/twm/deps/libz/%.c $(wildcard src/tools/pkg/repo/_src/twm/deps/libz/*.h) \
		$(wildcard src/user/libos/include/* src/user/libos/include/sys/*) | $(LIBZ_BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -DDYNAMIC_CRC_TABLE -DNO_GZCOMPRESS -DZ_HAVE_UNISTD_H \
		-Isrc/tools/pkg/repo/_src/twm/deps/libz -c $< -o $@

$(LIBZ_ARCHIVE): $(LIBZ_OBJECTS)
	ar rcs $@ $^

LIBFONTENC_CPPFLAGS := -nostdinc -I$(abspath src/user/libos/include) \
	-I$(abspath src/tools/pkg/repo/_src/twm/tinyx/include) -I$(abspath src/tools/pkg/repo/_src/twm/deps/libfontenc/include) \
	-isystem $(shell $(CC) -print-file-name=include) -idirafter /usr/include \
	-DFONT_ENCODINGS_DIRECTORY=\"/usr/share/fonts/encodings\"
LIBFONTENC_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -Wall -Wextra
LIBFONTENC_OBJECTS := $(addprefix $(LIBFONTENC_BUILD_DIR)/,fontenc.o encparse.o reallocarray.o)

$(LIBFONTENC_BUILD_DIR):
	mkdir -p $@

$(LIBFONTENC_BUILD_DIR)/%.o: src/tools/pkg/repo/_src/twm/deps/libfontenc/src/%.c \
		$(wildcard src/tools/pkg/repo/_src/twm/deps/libfontenc/src/*.h) \
		$(wildcard src/tools/pkg/repo/_src/twm/deps/libfontenc/include/X11/fonts/*.h) | $(LIBFONTENC_BUILD_DIR)
	$(CC) $(LIBFONTENC_CPPFLAGS) $(LIBFONTENC_CFLAGS) -c $< -o $@

$(LIBFONTENC_ARCHIVE): $(LIBFONTENC_OBJECTS)
	ar rcs $@ $^

LIBXFONT_CPPFLAGS := -nostdinc -I$(abspath src/user/libos/include) \
	-I$(abspath src/tools/pkg/repo/_src/twm/deps/libz) -I$(abspath src/tools/pkg/repo/_src/twm/tinyx/include) \
	-I$(abspath src/tools/pkg/repo/_src/twm/deps/libxfont/include) \
	-isystem $(shell $(CC) -print-file-name=include) \
	-idirafter /usr/include -DOPEN_SILLICON
LIBXFONT_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mno-mmx \
	-msse2 -mfpmath=sse -Wall -Wextra

$(LIBXFONT_BUILD_DIR):
	mkdir -p $@

$(LIBXFONT_ARCHIVE): $(LIBZ_ARCHIVE) $(LIBFONTENC_ARCHIVE) \
		src/tools/pkg/repo/_src/twm/deps/libxfont/configure src/tools/pkg/repo/_src/twm/deps/libxfont/configure.ac \
		$(wildcard src/tools/pkg/repo/_src/twm/deps/libxfont/src/*.[ch]) \
		$(wildcard src/tools/pkg/repo/_src/twm/deps/libxfont/src/*/*.[ch]) \
		$(wildcard src/tools/pkg/repo/_src/twm/deps/libxfont/include/X11/fonts/*.h) | $(LIBXFONT_BUILD_DIR)
	cd $(LIBXFONT_BUILD_DIR) && \
		ac_cv_func_readlink=no ac_cv_lib_z_inflateInit2_=yes \
		ac_cv_lib_m_hypot=yes \
		CPPFLAGS='$(LIBXFONT_CPPFLAGS)' CFLAGS='$(LIBXFONT_CFLAGS)' \
		PKG_CONFIG_PATH=/usr/lib64/pkgconfig:/usr/share/pkgconfig \
		$(abspath src/tools/pkg/repo/_src/twm/deps/libxfont/configure) --host=x86_64-elf \
		--disable-freetype --disable-fc --enable-bdfformat \
		--disable-shared --enable-static
	$(MAKE) -C $(LIBXFONT_BUILD_DIR) ACLOCAL=true AUTOMAKE=true \
		AUTOCONF=true AUTOHEADER=true
	cp $(LIBXFONT_BUILD_DIR)/src/.libs/libXfont.a $@

TINYX_CPPFLAGS := -nostdinc -I$(abspath src/include) \
	-I$(abspath src/user/libos/include) -I$(abspath src/tools/pkg/repo/_src/twm/deps/libz) \
	-I$(abspath src/tools/pkg/repo/_src/twm/deps/libfontenc/include) -I$(abspath src/tools/pkg/repo/_src/twm/deps/libxfont/include) \
	-I$(abspath $(TINYX_SOURCE_DIR)/include) \
	-I$(abspath $(TINYX_BUILD_DIR)/include) \
	-isystem $(shell $(CC) -print-file-name=include) -idirafter /usr/include \
	-DOPEN_SILLICON
TINYX_CFLAGS := -std=gnu11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone \
	-msse2 -mfpmath=sse -Wno-unused-parameter
TINYX_FONT_LIBS := -Wl,--start-group $(abspath $(LIBXFONT_ARCHIVE)) \
	$(abspath $(LIBFONTENC_ARCHIVE)) $(abspath $(LIBZ_ARCHIVE)) \
	$(abspath $(LIBOS_ARCHIVE)) -Wl,--end-group
TINYX_SOURCES := $(shell find src/tools/pkg/repo/_src/twm/tinyx -type f ! -path '*/.git/*' -print)

# Configure only when sources or the build configuration change. Runtime
# archives are link inputs; changing libc must not regenerate the source tree.
TINYX_CONFIG_STAMP := $(TINYX_BUILD_DIR)/.opensillicon-configured
$(TINYX_CONFIG_STAMP): $(TINYX_SOURCES) src/user/pkg/Makefile src/mk/common.mk \
        $(wildcard src/user/libos/include/*.h src/user/libos/include/sys/*.h) \
        $(wildcard src/include/abi/*.h) src/user/c_linker.ld | $(LIBXFONT_ARCHIVE) \
        $(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o
	mkdir -p $(TINYX_SOURCE_DIR) $(TINYX_BUILD_DIR)
	tar --exclude=.git -cf - -C src/tools/pkg/repo/_src/twm/tinyx . | tar -xf - -C $(TINYX_SOURCE_DIR)
	find $(TINYX_SOURCE_DIR) -name Makefile.am -exec touch {} +
	touch $(TINYX_SOURCE_DIR)/configure.ac
	cd $(TINYX_SOURCE_DIR) && autoreconf -fi
	cd $(TINYX_BUILD_DIR) && \
		OPEN_SILLICON_LIBS='$(TINYX_FONT_LIBS)' \
		OPEN_SILLICON_CRT0='$(abspath $(LIBOS_BUILD_DIR)/crt0.o)' \
		OPEN_SILLICON_LDFLAGS='-nostdlib -static -no-pie \
			-Wl,--build-id=none -Wl,-z,noexecstack \
			-Wl,-T,$(abspath src/user/c_linker.ld)' \
		CPPFLAGS='$(TINYX_CPPFLAGS)' CFLAGS='$(TINYX_CFLAGS)' \
		PKG_CONFIG_PATH='$(abspath $(LIBXFONT_BUILD_DIR))'$${PKG_CONFIG_PATH:+:"$$PKG_CONFIG_PATH"} \
		ac_cv_func_readlink=no \
		ac_cv_lib_z_inflateInit2_=yes ac_cv_lib_m_hypot=yes \
		$(abspath $(TINYX_SOURCE_DIR))/configure --host=x86_64-elf \
		--enable-kdrive --enable-xopensillicon \
				--with-fontdir=/usr/share/fonts \
		--with-default-font-path=built-ins,/usr/share/fonts/misc
	touch $@

$(TINYX_BINARY): $(TINYX_CONFIG_STAMP) $(LIBOS_ARCHIVE) $(LIBZ_ARCHIVE) \
        $(LIBFONTENC_ARCHIVE) $(LIBXFONT_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o
	$(MAKE) -C $(TINYX_BUILD_DIR)
	test -x "$@"

$(TINYX_BINARY): | $(BUILD_DIR)

$(BUILD_DIR)/startx.o: src/tools/pkg/repo/_src/twm/startx/startx.c | $(BUILD_DIR)
	$(CC) $(LIBOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/startx.elf: $(BUILD_DIR)/startx.o $(LIBOS_BUILD_DIR)/crt0.o $(LIBOS_ARCHIVE) src/user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(BUILD_DIR)/startx.o $(LIBOS_ARCHIVE) -o $@

.PHONY: $(BUILD_DIR)/x11/.apps-ready
$(BUILD_DIR)/x11/.apps-ready: $(TINYX_BINARY) $(LIBOS_ARCHIVE) $(LIBZ_ARCHIVE) \
		$(LIBFONTENC_ARCHIVE) $(LIBXFONT_ARCHIVE)
	sh src/tools/builders/build_x11_if_needed.sh

.PHONY: $(BUILD_DIR)/netsurf/.ready
$(BUILD_DIR)/netsurf/.ready: $(BUILD_DIR)/x11/.apps-ready $(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o
	sh src/tools/builders/build_netsurf.sh

# Desktop applications are built on demand as packages. The root image does
# not depend on these targets, so none of their binaries or resources ship in
# the base system.
XORG_BINS := $(BUILD_DIR)/x11/.apps-ready $(TINYX_BINARY) $(BUILD_DIR)/startx.elf
XORG_FONTS := $(wildcard src/tools/pkg/repo/_src/twm/fonts/truetype/liberation/*)
XORG_STAGE := $(BUILD_DIR)/pkg/xorg-stage
NETSURF_STAGE := $(BUILD_DIR)/pkg/netsurf-stage
DOOM_STAGE := $(BUILD_DIR)/pkg/doom-stage

$(BUILD_DIR)/pkg/xorg-stage.ready: $(XORG_BINS) $(XORG_FONTS) \
		src/tools/pkg/repo/_src/twm/fonts/gallant.bdf \
		src/tools/pkg/repo/_src/twm/fonts/fonts.dir \
		src/tools/pkg/repo/_src/twm/startx/system.twmrc \
		src/tools/pkg/repo/_src/twm/feh/share/fonts/yudit.ttf \
		src/tools/pkg/repo/_src/twm/feh/share/fonts/NotoSans-Medium.ttf \
		src/tools/pkg/repo/_src/twm/feh/share/images/feh.png \
		src/tools/pkg/repo/_src/twm/startx/background.jpg
	rm -rf $(XORG_STAGE)
	mkdir -p $(XORG_STAGE)/usr/bin $(XORG_STAGE)/usr/etc \
		$(XORG_STAGE)/usr/share/fonts/misc \
		$(XORG_STAGE)/usr/share/fonts/truetype/liberation \
		$(XORG_STAGE)/usr/share/feh/fonts $(XORG_STAGE)/usr/share/feh/images \
		$(XORG_STAGE)/usr/share/xorg
	cp $(TINYX_BINARY) $(XORG_STAGE)/usr/bin/X
	cp $(BUILD_DIR)/startx.elf $(XORG_STAGE)/usr/bin/startx
	cp $(BUILD_DIR)/x11/twm/src/twm $(XORG_STAGE)/usr/bin/twm
	cp $(BUILD_DIR)/x11/xterm/xterm $(XORG_STAGE)/usr/bin/xterm
	cp $(BUILD_DIR)/x11/feh/src/feh $(XORG_STAGE)/usr/bin/feh
	cp src/tools/pkg/repo/_src/twm/startx/system.twmrc $(XORG_STAGE)/usr/etc/twmrc
	cp src/tools/pkg/repo/_src/twm/fonts/gallant.bdf src/tools/pkg/repo/_src/twm/fonts/fonts.dir $(XORG_STAGE)/usr/share/fonts/misc/
	cp $(XORG_FONTS) $(XORG_STAGE)/usr/share/fonts/truetype/liberation/
	cp src/tools/pkg/repo/_src/twm/feh/share/fonts/yudit.ttf src/tools/pkg/repo/_src/twm/feh/share/fonts/NotoSans-Medium.ttf $(XORG_STAGE)/usr/share/feh/fonts/
	cp src/tools/pkg/repo/_src/twm/feh/share/images/feh.png $(XORG_STAGE)/usr/share/feh/images/
	cp src/tools/pkg/repo/_src/twm/startx/background.jpg $(XORG_STAGE)/usr/share/xorg/background.jpg
	touch $@

src/tools/pkg/repo/_pkges/twm-1.0.0.spkg: $(BUILD_DIR)/pkg/xorg-stage.ready $(BUILD_DIR)/pkg/pkg-build
	$(BUILD_DIR)/pkg/pkg-build twm 1.0.0 $(XORG_STAGE) $@ 'TinyX desktop with twm, X11 clients and fonts'

X11_CLIENT_PREFIX := $(BUILD_DIR)/x11/prefix
X11_CLIENT_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin -fno-strict-aliasing \
	-fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -DOPEN_SILLICON \
	-nostdinc -Isrc/user/libos/include -I$(X11_CLIENT_PREFIX)/include \
	-I$(BUILD_DIR)/x11/libX11/include \
	-isystem $(shell $(CC) -print-file-name=include) -idirafter /usr/include
X11_CLIENT_LIBS := -lXaw7 -lXpm -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau
X11_CLIENT_SOURCE := src/tools/pkg/repo/_src

$(BUILD_DIR)/x11/clients/xeyes: private CLIENT_FLAGS := -DPACKAGE_STRING='"xeyes 1.3.1"'
$(BUILD_DIR)/x11/clients/xeyes: private CLIENT_LIBS := -lXi -lXfixes
$(BUILD_DIR)/x11/clients/xeyes: $(wildcard $(X11_CLIENT_SOURCE)/xeyes/*.[ch] $(X11_CLIENT_SOURCE)/xeyes/*.bit)
$(BUILD_DIR)/x11/clients/xclock: private CLIENT_FLAGS := -DNO_I18N -DHAVE_GETPID -DPACKAGE_STRING='"xclock 1.2.0"'
$(BUILD_DIR)/x11/clients/xclock: $(wildcard $(X11_CLIENT_SOURCE)/xclock/*.[ch] $(X11_CLIENT_SOURCE)/xclock/*.bit)

$(addprefix $(BUILD_DIR)/x11/clients/,xeyes xclock): $(BUILD_DIR)/x11/.apps-ready \
		$(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o src/user/c_linker.ld src/user/pkg/Makefile
	mkdir -p $(@D)
	$(CC) $(X11_CLIENT_CFLAGS) $(CLIENT_FLAGS) $(filter %.c,$^) \
		-nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o -L$(X11_CLIENT_PREFIX)/lib \
		-Wl,--start-group $(CLIENT_LIBS) $(X11_CLIENT_LIBS) $(X11_CLIENT_PREFIX)/lib/libi18n.a \
		$(LIBOS_ARCHIVE) -Wl,--end-group -o $@

define X11_CLIENT_PACKAGE
src/tools/pkg/repo/_pkges/$(1)-$(2).spkg: $(BUILD_DIR)/x11/clients/$(1) \
		$(X11_CLIENT_SOURCE)/$(1)/COPYING $(BUILD_DIR)/pkg/pkg-build
	install -D -m 755 $$< $(BUILD_DIR)/pkg/$(1)-stage/usr/bin/$(1)
	install -D -m 644 $(X11_CLIENT_SOURCE)/$(1)/COPYING \
		$(BUILD_DIR)/pkg/$(1)-stage/usr/share/licenses/$(1)/COPYING
	$(BUILD_DIR)/pkg/pkg-build $(1) $(2) $(BUILD_DIR)/pkg/$(1)-stage $$@ '$(3)'
endef

$(eval $(call X11_CLIENT_PACKAGE,xeyes,1.3.1,X11 eyes that follow the pointer))
$(eval $(call X11_CLIENT_PACKAGE,xclock,1.2.0,Analog and digital clock for X11))

QUAKE_DIR := src/tools/pkg/repo/_src/quake
QUAKE_BUILD := $(BUILD_DIR)/quake
QUAKE_STAGE := $(QUAKE_BUILD)/stage
QUAKE_ENGINE := $(wildcard $(QUAKE_DIR)/WinQuake/*.c)
QUAKE_PORT := $(wildcard $(QUAKE_DIR)/opensillicon/*.c)
QUAKE_HEADERS := $(wildcard $(QUAKE_DIR)/WinQuake/*.h $(QUAKE_DIR)/opensillicon/*.h) \
	$(QUAKE_DIR)/WinQuake/progdefs.q1
QUAKE_OBJECTS := $(addprefix $(QUAKE_BUILD)/,$(notdir $(QUAKE_ENGINE:.c=.o) $(QUAKE_PORT:.c=.o)))
QUAKE_CFLAGS := $(X11_CLIENT_CFLAGS) -std=gnu11 -fcommon -Dstricmp=strcasecmp \
	-I$(QUAKE_DIR)/WinQuake -I$(QUAKE_DIR)/opensillicon

$(QUAKE_BUILD)/%.o: $(QUAKE_DIR)/WinQuake/%.c $(QUAKE_HEADERS) src/user/pkg/Makefile
	mkdir -p $(@D)
	$(CC) $(QUAKE_CFLAGS) -include format.h -c $< -o $@

$(QUAKE_BUILD)/%.o: $(QUAKE_DIR)/opensillicon/%.c $(QUAKE_HEADERS) src/user/pkg/Makefile
	mkdir -p $(@D)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/vid_x11.o: | $(BUILD_DIR)/x11/.apps-ready

$(QUAKE_BUILD)/quake: $(QUAKE_OBJECTS) $(BUILD_DIR)/x11/.apps-ready \
		$(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o src/user/c_linker.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o $(QUAKE_OBJECTS) \
		-Wl,--start-group $(X11_DOOM_LIBS) $(X11_PREFIX)/lib/libi18n.a \
		$(LIBOS_ARCHIVE) -Wl,--end-group -lgcc -o $@

src/tools/pkg/repo/_pkges/quake-1.09.spkg: $(QUAKE_BUILD)/quake $(QUAKE_DIR)/COPYING \
		$(QUAKE_DIR)/README.md $(BUILD_DIR)/pkg/pkg-build
	install -D -m 755 $< $(QUAKE_STAGE)/usr/bin/quake
	install -D -m 644 $(QUAKE_DIR)/COPYING $(QUAKE_STAGE)/usr/share/licenses/quake/COPYING
	install -D -m 644 $(QUAKE_DIR)/README.md $(QUAKE_STAGE)/usr/share/doc/quake/README.md
	$(BUILD_DIR)/pkg/pkg-build quake 1.09 $(QUAKE_STAGE) $@ 'Quake software renderer for TinyX'

JAVAC ?= javac
JAR ?= jar
MINIJVM_DIR := src/tools/pkg/repo/_src/minijvm
MINIJVM_BUILD := $(BUILD_DIR)/minijvm
MINIJVM_STAGE := $(MINIJVM_BUILD)/stage
MINIJVM_SOURCES := $(shell find $(MINIJVM_DIR)/vendor $(MINIJVM_DIR)/opensillicon -type f)
MINIJVM_CLASSES := $(shell find $(MINIJVM_DIR)/classlib -type f)
MINIJVM_LWJGL_CLASSES := $(shell find $(MINIJVM_DIR)/lwjgl/java -type f)
MINIJVM_MESA_ARCHIVE := $(MINIJVM_BUILD)/mesa/MesaLib-7.8.2.tar.bz2
MINIJVM_MESA := $(MINIJVM_BUILD)/mesa/Mesa-7.8.2
MINIJVM_OSMESA := $(MINIJVM_MESA)/lib/libOSMesa.a

$(MINIJVM_MESA_ARCHIVE):
	mkdir -p $(@D)
	curl --fail --location --silent --show-error --max-time 120 \
		https://archive.mesa3d.org/older-versions/7.x/7.8.2/MesaLib-7.8.2.tar.bz2 -o $@.tmp
	printf '%s  %s\n' 505bf418dceba05837f4ea1b1972b9620c35f8cb94bc4d1e6d573c15f562576d $@.tmp | sha256sum -c -
	mv $@.tmp $@

$(MINIJVM_OSMESA): $(MINIJVM_MESA_ARCHIVE) $(MINIJVM_DIR)/opensillicon/build-mesa.sh $(LIBOS_ARCHIVE)
	+sh $(MINIJVM_DIR)/opensillicon/build-mesa.sh $(abspath $<) $(abspath $(dir $<)) \
		$(abspath src/user/libos/include) "$(CC)" "$(HOST_CC)"

$(MINIJVM_BUILD)/java: $(MINIJVM_SOURCES) $(LIBOS_ARCHIVE) $(BUILD_DIR)/x11/.apps-ready \
		$(MINIJVM_OSMESA) $(BUILD_DIR)/mbedtls/libmbedcrypto.a $(LIBOS_BUILD_DIR)/crt0.o src/user/c_linker.ld
	sh $(MINIJVM_DIR)/opensillicon/build.sh $(MINIJVM_DIR) $(MINIJVM_BUILD) \
		$(CC) $(LIBOS_ARCHIVE) $(LIBOS_BUILD_DIR)/crt0.o $(X11_PREFIX) $(MINIJVM_MESA) \
		$(MINIJVM_OSMESA) $(X11_DOOM_LIBS) $(X11_PREFIX)/lib/libi18n.a \
		$(X11_PREFIX)/lib/libpng16.a $(LIBZ_ARCHIVE) $(BUILD_DIR)/mbedtls/libmbedcrypto.a

$(MINIJVM_BUILD)/minijvm_rt.jar: $(MINIJVM_CLASSES) $(MINIJVM_DIR)/opensillicon/build-classlib.sh
	sh $(MINIJVM_DIR)/opensillicon/build-classlib.sh $(MINIJVM_DIR)/classlib $@ $(JAVAC) $(JAR)

$(MINIJVM_BUILD)/lwjgl.jar: $(MINIJVM_LWJGL_CLASSES) $(MINIJVM_DIR)/opensillicon/build-classlib.sh
	sh $(MINIJVM_DIR)/opensillicon/build-classlib.sh $(MINIJVM_DIR)/lwjgl $@ $(JAVAC) $(JAR)

src/tools/pkg/repo/_pkges/minijvm-2021.12.spkg: $(MINIJVM_BUILD)/java \
		$(MINIJVM_BUILD)/minijvm_rt.jar $(MINIJVM_BUILD)/lwjgl.jar $(MINIJVM_DIR)/README.md $(MINIJVM_DIR)/LICENSE \
		src/tools/pkg/repo/_src/twm/deps/libpng/LICENSE \
		$(MINIJVM_DIR)/licenses/mbedtls.Apache-2.0.txt $(BUILD_DIR)/pkg/pkg-build
	install -D -m 755 $< $(MINIJVM_STAGE)/usr/bin/java
	install -D -m 644 $(MINIJVM_BUILD)/minijvm_rt.jar $(MINIJVM_STAGE)/usr/share/minijvm/minijvm_rt.jar
	install -D -m 644 $(MINIJVM_BUILD)/lwjgl.jar $(MINIJVM_STAGE)/usr/share/minijvm/lwjgl.jar
	install -D -m 644 $(MINIJVM_DIR)/LICENSE $(MINIJVM_STAGE)/usr/share/licenses/minijvm/LICENSE
	install -D -m 644 $(MINIJVM_MESA)/docs/license.html $(MINIJVM_STAGE)/usr/share/licenses/minijvm/mesa.html
	install -D -m 644 src/tools/pkg/repo/_src/twm/deps/libpng/LICENSE $(MINIJVM_STAGE)/usr/share/licenses/minijvm/libpng.LICENSE
	install -D -m 644 $(MINIJVM_DIR)/licenses/mbedtls.Apache-2.0.txt $(MINIJVM_STAGE)/usr/share/licenses/minijvm/mbedtls.Apache-2.0.txt
	install -D -m 644 $(MINIJVM_DIR)/README.md $(MINIJVM_STAGE)/usr/share/doc/minijvm/README.md
	$(BUILD_DIR)/pkg/pkg-build minijvm 2021.12 $(MINIJVM_STAGE) $@ 'miniJVM with a Java 8 class library subset'

TCC_PACKAGE_DIR := src/tools/pkg/repo/_src/tcc
TCC_SOURCE := $(TCC_PACKAGE_DIR)/tinycc
TCC_PORT := $(TCC_PACKAGE_DIR)/opensillicon
TCC_BUILD := $(BUILD_DIR)/tcc
TCC_STAGE := $(TCC_BUILD)/stage
TCC_HEADERS := $(shell find src/user/libos/include -type f -name '*.h')
TCC_BUILTIN_HEADERS := $(wildcard $(TCC_SOURCE)/include/*.h)
TCC_RUNTIME := $(addprefix $(TCC_BUILD)/,libtcc1.o va_list.o alloca86_64.o)
TCC_CFLAGS := -std=gnu11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pie -mno-red-zone -nostdinc -Isrc/user/libos/include \
	-isystem $(shell $(CC) -print-file-name=include)

$(TCC_BUILD)/tcc: $(wildcard $(TCC_SOURCE)/*.[ch] $(TCC_SOURCE)/*.def) \
		$(TCC_PORT)/config.h $(TCC_PORT)/compat.c $(TCC_HEADERS) $(LIBOS_ARCHIVE) \
		$(LIBOS_BUILD_DIR)/crt0.o src/user/c_linker.ld src/user/pkg/Makefile
	mkdir -p $(@D)
	$(CC) $(TCC_CFLAGS) -I$(TCC_PORT) \
		-nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
		-Wl,-T,src/user/c_linker.ld $(LIBOS_BUILD_DIR)/crt0.o \
		$(TCC_SOURCE)/tcc.c $(TCC_PORT)/compat.c $(LIBOS_ARCHIVE) -lgcc -o $@

$(TCC_BUILD)/%.o: $(TCC_SOURCE)/lib/%.c src/user/pkg/Makefile
	mkdir -p $(@D)
	$(CC) $(TCC_CFLAGS) -c $< -o $@

$(TCC_BUILD)/alloca86_64.o: $(TCC_SOURCE)/lib/alloca86_64.S
	mkdir -p $(@D)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(TCC_BUILD)/libtcc1.a: $(TCC_RUNTIME)
	$(AR) rcs $@ $^

src/tools/pkg/repo/_pkges/tcc-0.9.27.spkg: $(TCC_BUILD)/tcc $(TCC_BUILD)/libtcc1.a \
		$(TCC_BUILTIN_HEADERS) $(TCC_PORT)/include/stdint.h $(TCC_HEADERS) src/include/abi/disk.h \
		$(TCC_SOURCE)/COPYING $(BUILD_DIR)/pkg/pkg-build
	rm -rf $(TCC_STAGE)
	install -D -m 755 $(TCC_BUILD)/tcc $(TCC_STAGE)/usr/bin/tcc
	install -D -m 644 $(LIBOS_ARCHIVE) $(TCC_STAGE)/usr/lib/tcc/lib/liblibos.a
	install -D -m 644 $(TCC_BUILD)/libtcc1.a $(TCC_STAGE)/usr/lib/tcc/lib/libtcc1.a
	install -D -m 644 $(LIBOS_BUILD_DIR)/crt0.o $(TCC_STAGE)/usr/lib/tcc/lib/crt0.o
	install -D -m 644 src/include/abi/disk.h $(TCC_STAGE)/usr/lib/tcc/include/abi/disk.h
	cp -R src/user/libos/include/. $(TCC_STAGE)/usr/lib/tcc/include/
	install -m 644 $(TCC_BUILTIN_HEADERS) $(TCC_PORT)/include/stdint.h $(TCC_STAGE)/usr/lib/tcc/include/
	install -D -m 644 $(TCC_SOURCE)/COPYING $(TCC_STAGE)/usr/share/licenses/tcc/COPYING
	$(BUILD_DIR)/pkg/pkg-build tcc 0.9.27 $(TCC_STAGE) $@ 'Tiny C Compiler for OpenSillicon'

NETSURF_RESOURCES := adblock.css credits.html default.css internal.css \
	licence.html netsurf.png quirks.css welcome.html Messages
$(BUILD_DIR)/pkg/netsurf-stage.ready: $(BUILD_DIR)/netsurf/.ready \
		src/tools/pkg/repo/_src/netsurf/opensillicon/Choices \
		src/tools/pkg/repo/_src/netsurf/netsurf/COPYING
	rm -rf $(NETSURF_STAGE)
	mkdir -p $(NETSURF_STAGE)/usr/bin $(NETSURF_STAGE)/usr/etc/netsurf \
		$(NETSURF_STAGE)/usr/share/netsurf
	cp $(BUILD_DIR)/netsurf/netsurf $(NETSURF_STAGE)/usr/bin/netsurf
	cp src/tools/pkg/repo/_src/netsurf/opensillicon/Choices $(NETSURF_STAGE)/usr/etc/netsurf/Choices
	for resource in $(NETSURF_RESOURCES); do cp $(BUILD_DIR)/netsurf/resources/$$resource $(NETSURF_STAGE)/usr/share/netsurf/$$resource; done
	cp src/tools/pkg/repo/_src/netsurf/netsurf/COPYING $(NETSURF_STAGE)/usr/share/netsurf/COPYING
	touch $@

src/tools/pkg/repo/_pkges/netsurf-1.0.0.spkg: $(BUILD_DIR)/pkg/netsurf-stage.ready $(BUILD_DIR)/pkg/pkg-build
	$(BUILD_DIR)/pkg/pkg-build netsurf 1.0.0 $(NETSURF_STAGE) $@ 'NetSurf web browser for X11'

$(BUILD_DIR)/pkg/doom-stage.ready: $(BUILD_DIR)/doom/doom \
		src/tools/pkg/repo/_src/doom/doom1.wad
	rm -rf $(DOOM_STAGE)
	mkdir -p $(DOOM_STAGE)/usr/bin $(DOOM_STAGE)/usr/share/doom
	cp $(BUILD_DIR)/doom/doom $(DOOM_STAGE)/usr/bin/doom
	cp src/tools/pkg/repo/_src/doom/doom1.wad $(DOOM_STAGE)/usr/share/doom/doom.wad
	touch $@

src/tools/pkg/repo/_pkges/doom-1.0.0.spkg: $(BUILD_DIR)/pkg/doom-stage.ready $(BUILD_DIR)/pkg/pkg-build
	$(BUILD_DIR)/pkg/pkg-build doom 1.0.0 $(DOOM_STAGE) $@ 'Doom with shareware game data'

$(BUILD_DIR)/pkg/pkg-vfs-test: src/tests/pkg_vfs_test.c src/kernel/fs/vfs/vfs.c \
		src/kernel/fs/ramfs/ramfs.c src/kernel/proc/fd.c \
		src/include/kernel/vfs.h src/include/kernel/fd.h
	mkdir -p $(@D)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections \
		-fdata-sections -Isrc/include -Isrc/limine $(filter %.c,$^) \
		-Wl,--gc-sections -o $@

-include $(PKG_OBJECTS:.o=.d)
