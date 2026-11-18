ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
NAME := os-kernel
VERSION ?= $(shell date -u +%Y%m%d%H%M%S)
DESCRIPTION := OpenSillicon signed kernel and initramfs release
STAGE := $(ROOT)/src/build/pkg/os-kernel-stage
SIGNING_KEY ?= $(HOME)/.config/opensillicon/sysupgrade-signing.pem

.DEFAULT_GOAL := all
.PHONY: all clean

all:
	test -r "$(SIGNING_KEY)" || { echo "Set SIGNING_KEY to the private sysupgrade signing key" >&2; exit 1; }
	rm -rf "$(STAGE)"
	mkdir -p "$(STAGE)/opt/os-kernel"
	sh "$(CURDIR)/build.sh" "$(STAGE)"
	{ \
		echo OSUPG1; \
		echo version=$(VERSION); \
		echo driver-api=2.24; \
		echo kernel-size=$$(wc -c < "$(STAGE)/opt/os-kernel/kernel.elf" | tr -d ' '); \
		echo kernel-sha256=$$(sha256sum "$(STAGE)/opt/os-kernel/kernel.elf" | cut -d ' ' -f 1); \
		echo init-size=$$(wc -c < "$(STAGE)/opt/os-kernel/init.cpi" | tr -d ' '); \
		echo init-sha256=$$(sha256sum "$(STAGE)/opt/os-kernel/init.cpi" | cut -d ' ' -f 1); \
	} > "$(STAGE)/opt/os-kernel/manifest"
	openssl dgst -sha256 -sign "$(SIGNING_KEY)" -out "$(STAGE)/opt/os-kernel/manifest.sig" "$(STAGE)/opt/os-kernel/manifest"
	$(MAKE) -C "$(ROOT)" pkg-from-stage PKG_NAME="$(NAME)" \
		PKG_VERSION="$(VERSION)" PKG_STAGE="$(STAGE)" \
		PKG_DESCRIPTION="$(DESCRIPTION)"

clean:
	rm -rf "$(STAGE)"
