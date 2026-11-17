ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
PACKAGE := src/tools/pkg/repo/_pkges/minijvm-2021.12.spkg
.DEFAULT_GOAL := all
.PHONY: all

all:
	$(MAKE) -C $(ROOT) $(PACKAGE)
