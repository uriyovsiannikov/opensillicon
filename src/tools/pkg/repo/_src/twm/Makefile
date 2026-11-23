ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
PACKAGE := src/tools/pkg/repo/_pkges/twm-1.0.0+abi23.1.spkg
.DEFAULT_GOAL := all
.PHONY: all

all:
	$(MAKE) -C $(ROOT) $(PACKAGE)
