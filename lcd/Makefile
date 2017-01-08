KERN_DIR=/home/maciej/linux
TARGET_MODULE:=rpilcd-module

$(TARGET_MODULE)-objs := main.o device_file.o
obj-m := $(TARGET_MODULE).o

all:
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -C $(KERN_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) clean
