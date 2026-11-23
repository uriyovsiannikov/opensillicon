ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=hexedit PKG_VERSION=1.0.0+abi23 PKG_STAGE=$(ROOT)/src/build/pkg/hexedit-stage PKG_DESCRIPTION='Terminal hexadecimal file editor'
