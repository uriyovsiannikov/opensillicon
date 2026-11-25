ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=pipes.sh PKG_VERSION=1.3.1 PKG_STAGE=$(ROOT)/src/build/pkg/pipes.sh-stage PKG_DESCRIPTION='Animated pipes terminal screensaver'
