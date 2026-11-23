ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=nsnake PKG_VERSION=3.0.2+abi23 PKG_STAGE=$(ROOT)/src/build/pkg/nsnake-stage PKG_DESCRIPTION='Classic snake game for the terminal'
