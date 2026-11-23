ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
PACKAGE := src/tools/pkg/repo/_pkges/quake-1.09+abi23.spkg
.DEFAULT_GOAL := all
.PHONY: all

all:
	$(MAKE) -C $(ROOT) $(PACKAGE)
