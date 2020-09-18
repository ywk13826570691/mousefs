obj-m := msfs.o
obj-m += drv.o
drv-objs := driver.o tool.o
msfs-objs := fs.o inode.o op.o

$(info $(tool-objs))
KERNELDIR = /home/wyang/Desktop/IDM/iDM/trunk/linux-toradex/
PWD = $(shell pwd)
all:
	make -C $(KERNELDIR) M=$(PWD) modules
tool:
	#rm tool.o
	#gcc tool.c -DCONFIG_MSFS_TOOL -o tool
	$(info "pls compiler test tools")
clean:
	rm *.o *.ko *.mod.c *.order *.symvers
	find -type d,f -name ".[a-z]*" | xargs rm -rf

