ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=xlogo PKG_VERSION=1.0.7+abi23 PKG_STAGE=$(ROOT)/src/build/pkg/xlogo-stage PKG_DESCRIPTION='X Window System logo viewer'
