ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
PORT := $(ROOT)/src/tools/pkg/repo/_src/openssh
STAGE := $(ROOT)/src/build/pkg/openssh-client-stage
.DEFAULT_GOAL := all
.PHONY: all build

build:
	sh $(PORT)/build.sh

all: build
	install -d $(STAGE)/usr/bin $(STAGE)/usr/share/licenses/openssh
	install -m 755 $(addprefix $(ROOT)/src/build/openssh-port/,ssh ssh-keygen scp sftp ssh-agent ssh-add) $(STAGE)/usr/bin/
	install -m 644 $(PORT)/upstream/LICENCE $(STAGE)/usr/share/licenses/openssh/LICENCE
	$(MAKE) -C $(ROOT) pkg-from-stage PKG_NAME=openssh-client PKG_VERSION=10.6.2 PKG_STAGE=$(STAGE) PKG_DESCRIPTION='Experimental OpenSSH client tools'
