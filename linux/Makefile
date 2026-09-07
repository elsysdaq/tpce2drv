MODULE := tpce2drv

$(MODULE)-objs := main.o request.o dma.o dma_descriptors.o

obj-m := $(MODULE).o

ccflags-y := -DDYNAMIC_DEBUG_MODULE

# Optional 32-bit build options
#   make LEGACY32=1
LEGACY32 ?= 0
ifeq ($(LEGACY32),1)
ccflags-y += -m32 -Wno-declaration-after-statement
endif

# Optional Debug build options
#   make BUILD=Debug
BUILD ?= Release
ifeq ($(BUILD),Debug)
ccflags-y += -DDEBUG -g
endif

MODDIR := $(shell uname -r)

KERNELVER ?= $(MODDIR)
KDIR ?= /lib/modules/$(KERNELVER)/build

PWD   := $(CURDIR)

all: modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -f Module.symvers Module.markers modules.order

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a $(KERNELVER)
	modprobe $(MODULE)
modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
