ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=xedit PKG_VERSION=1.2.4+abi23 PKG_STAGE=$(ROOT)/src/build/pkg/xedit-stage PKG_DESCRIPTION='Text editor for the X Window System'
