obj-m += lcd_driver.o

KDIR := $(HOME)/linux_embedded_tasks/Task4/buildroot/output/build/linux-custom

PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=arm \
		CROSS_COMPILE=$(HOME)/x-tools/arm-unknown-linux-musleabihf/bin/arm-unknown-linux-musleabihf- \
		modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean