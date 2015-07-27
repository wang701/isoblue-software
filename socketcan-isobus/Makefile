KERNEL_DIR = /home/yang/Documents/android_kernel_asus_flo
PWD := $(shell pwd)

obj-m	+= can-isobus.o
can-isobus-y	:= isobus.o

all:
	make ARCH=arm SUBARCH=arm CROSS_COMPILE=/home/yang/Documents/arm-eabi-4.6/bin/arm-eabi- -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules
clean:
	make -C $(KERNEL_DIR) SUBDIRS=$(PWD) clean