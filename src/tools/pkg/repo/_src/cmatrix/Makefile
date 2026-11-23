ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=cmatrix PKG_VERSION=2.0.1+abi23 PKG_STAGE=$(ROOT)/src/build/pkg/cmatrix-stage PKG_DESCRIPTION='Matrix rain terminal animation'
