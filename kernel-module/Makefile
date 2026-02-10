obj-m += syscall_monitor.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	sudo insmod syscall_monitor.ko

uninstall:
	sudo rmmod syscall_monitor

reload: uninstall install
