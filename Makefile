# kernel module build
obj-m += mydriver.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	gcc -Wall -o test_driver test_driver.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f test_driver