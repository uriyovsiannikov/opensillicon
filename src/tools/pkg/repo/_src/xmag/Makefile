ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
.DEFAULT_GOAL := all
.PHONY: all
all:
	sh $(CURDIR)/build.sh
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=xmag PKG_VERSION=1.0.8 PKG_STAGE=$(ROOT)/src/build/pkg/xmag-stage PKG_DESCRIPTION='Magnify a region of the X screen'
