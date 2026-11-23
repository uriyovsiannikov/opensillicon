ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
PACKAGE := src/tools/pkg/repo/_pkges/fetch-1.0.0+abi23.spkg
.DEFAULT_GOAL := all
.PHONY: all

all:
	$(MAKE) -C $(ROOT) $(PACKAGE)
