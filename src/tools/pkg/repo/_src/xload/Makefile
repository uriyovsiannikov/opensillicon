ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=xload PKG_VERSION=1.1.4+abi23 PKG_STAGE=$(ROOT)/src/build/pkg/xload-stage PKG_DESCRIPTION='X11 view of runnable process count'
