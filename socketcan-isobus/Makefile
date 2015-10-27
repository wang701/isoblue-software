KERNEL_DIR = /home/yang/Documents/tegra
PWD := $(shell pwd)

obj-m	+= can-isobus.o
can-isobus-y	:= isobus.o

all:
	make ARCH=arm64 SUBARCH=arm64 CROSS_COMPILE=aarch64-linux-android- -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules
clean:
	make -C $(KERNEL_DIR) SUBDIRS=$(PWD) clean
