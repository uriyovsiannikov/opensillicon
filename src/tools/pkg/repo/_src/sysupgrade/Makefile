ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
NAME := sysupgrade
VERSION := 1.0.0
DESCRIPTION := Signed BSD style kernel slot updater
STAGE := $(ROOT)/src/build/pkg/sysupgrade-stage

.DEFAULT_GOAL := all
.PHONY: all clean

all:
	rm -rf "$(STAGE)"
	mkdir -p "$(STAGE)/usr/bin"
	sh "$(CURDIR)/build.sh" "$(STAGE)"
	$(MAKE) -C "$(ROOT)" pkg-from-stage PKG_NAME="$(NAME)" \
		PKG_VERSION="$(VERSION)" PKG_STAGE="$(STAGE)" \
		PKG_DESCRIPTION="$(DESCRIPTION)"

clean:
	rm -rf "$(STAGE)"
