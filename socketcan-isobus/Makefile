KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

modules :

include ../buildnum.mk

% :
	$(MAKE) -C $(KDIR) M=$(PWD) CONFIG_MODULE_SIG=n BUILD_NUM=$(BUILD_NUM) $@
