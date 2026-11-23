ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=cowsay PKG_VERSION=3.8.4+abi23 PKG_STAGE=$(ROOT)/src/build/pkg/cowsay-stage PKG_DESCRIPTION='ASCII speech balloon and cow display'
