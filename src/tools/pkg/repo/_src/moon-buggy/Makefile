ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=moon-buggy PKG_VERSION=1.1.1 PKG_STAGE=$(ROOT)/src/build/pkg/moon-buggy-stage PKG_DESCRIPTION='Text mode moon driving game'
