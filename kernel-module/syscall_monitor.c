#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/sched.h>

#define DEVICE_NAME "syscall_monitor"
#define CLASS_NAME "syscall_mon"

// Module modes
#define MODE_OFF 0
#define MODE_LOG 1
#define MODE_BLOCK 2

// Syscall types
#define SYSCALL_OPEN 0
#define SYSCALL_READ 1
#define SYSCALL_WRITE 2


static int current_mode = MODE_OFF;
static int target_syscall = SYSCALL_OPEN;
static pid_t target_pid = -1;  

static int major_number;
static struct class* syscall_class = NULL;
static struct device* syscall_device = NULL;

static struct kprobe kp_open;
static struct kprobe kp_read;
static struct kprobe kp_write;

// IOCTL commands
#define IOCTL_SET_MODE _IOW('s', 1, int)
#define IOCTL_SET_SYSCALL _IOW('s', 2, int)
#define IOCTL_SET_PID _IOW('s', 3, pid_t)

// open syscall
static int handler_pre_open(struct kprobe *p, struct pt_regs *regs)
{
    if (current_mode == MODE_OFF)
        return 0;
    
    if (current_mode == MODE_LOG && target_syscall == SYSCALL_OPEN) {
        printk(KERN_INFO "SYSCALL_MONITOR: PID=%d called open()\n", current->pid);
    }
    
    if (current_mode == MODE_BLOCK && target_syscall == SYSCALL_OPEN) {
        if (target_pid == -1 || target_pid == current->pid) {
            printk(KERN_INFO "SYSCALL_MONITOR: Blocking open() for PID=%d\n", current->pid);
            return -1;
        }
    }
    
    return 0;
}

// read syscall
static int handler_pre_read(struct kprobe *p, struct pt_regs *regs)
{
    if (current_mode == MODE_OFF)
        return 0;
    
    if (current_mode == MODE_LOG && target_syscall == SYSCALL_READ) {
        printk(KERN_INFO "SYSCALL_MONITOR: PID=%d called read()\n", current->pid);
    }
    
    if (current_mode == MODE_BLOCK && target_syscall == SYSCALL_READ) {
        if (target_pid == -1 || target_pid == current->pid) {
            printk(KERN_INFO "SYSCALL_MONITOR: Blocking read() for PID=%d\n", current->pid);
        }
    }
    
    return 0;
}

// write syscall
static int handler_pre_write(struct kprobe *p, struct pt_regs *regs)
{
    if (current_mode == MODE_OFF)
        return 0;
    
    if (current_mode == MODE_LOG && target_syscall == SYSCALL_WRITE) {
        printk(KERN_INFO "SYSCALL_MONITOR: PID=%d called write()\n", current->pid);
    }
    
    if (current_mode == MODE_BLOCK && target_syscall == SYSCALL_WRITE) {
        if (target_pid == -1 || target_pid == current->pid) {
            printk(KERN_INFO "SYSCALL_MONITOR: Blocking write() for PID=%d\n", current->pid);
        }
    }
    
    return 0;
}

// ioctl handler
static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int value;
    
    switch(cmd) {
        case IOCTL_SET_MODE:
            if (copy_from_user(&value, (int __user *)arg, sizeof(int)))
                return -EFAULT;
            if (value >= MODE_OFF && value <= MODE_BLOCK) {
                current_mode = value;
                printk(KERN_INFO "SYSCALL_MONITOR: Mode changed to %d\n", value);
            }
            break;
            
        case IOCTL_SET_SYSCALL:
            if (copy_from_user(&value, (int __user *)arg, sizeof(int)))
                return -EFAULT;
            if (value >= SYSCALL_OPEN && value <= SYSCALL_WRITE) {
                target_syscall = value;
                printk(KERN_INFO "SYSCALL_MONITOR: Target syscall changed to %d\n", value);
            }
            break;
            
        case IOCTL_SET_PID:
            if (copy_from_user(&target_pid, (pid_t __user *)arg, sizeof(pid_t)))
                return -EFAULT;
            printk(KERN_INFO "SYSCALL_MONITOR: Target PID changed to %d\n", target_pid);
            break;
            
        default:
            return -EINVAL;
    }
    
    return 0;
}

static struct file_operations fops = {
    .unlocked_ioctl = device_ioctl,
};

// Module initialization
static int __init syscall_monitor_init(void)
{
    int ret;
    
    printk(KERN_INFO "SYSCALL_MONITOR: Initializing module\n");
    
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "SYSCALL_MONITOR: Failed to register device\n");
        return major_number;
    }
    
    syscall_class = class_create( CLASS_NAME);
    if (IS_ERR(syscall_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(syscall_class);
    }
    
    syscall_device = device_create(syscall_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(syscall_device)) {
        class_destroy(syscall_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(syscall_device);
    }
    
    // open
    kp_open.symbol_name = "__x64_sys_openat";
    kp_open.pre_handler = handler_pre_open;
    ret = register_kprobe(&kp_open);
    if (ret < 0) {
        printk(KERN_INFO "SYSCALL_MONITOR: Trying alternative open symbol\n");
        kp_open.symbol_name = "__x64_sys_openat";
        ret = register_kprobe(&kp_open);
        if (ret < 0) {
            printk(KERN_ERR "SYSCALL_MONITOR: Failed to register kprobe for open\n");
        }
    }
    
    // read
    kp_read.symbol_name = "__x64_sys_read";
    kp_read.pre_handler = handler_pre_read;
    ret = register_kprobe(&kp_read);
    if (ret < 0) {
        printk(KERN_ERR "SYSCALL_MONITOR: Failed to register kprobe for read\n");
    }
    
    // write
    kp_write.symbol_name = "__x64_sys_write";
    kp_write.pre_handler = handler_pre_write;
    ret = register_kprobe(&kp_write);
    if (ret < 0) {
        printk(KERN_ERR "SYSCALL_MONITOR: Failed to register kprobe for write\n");
    }
    
    printk(KERN_INFO "SYSCALL_MONITOR: Device created: /dev/%s\n", DEVICE_NAME);
    printk(KERN_INFO "SYSCALL_MONITOR: Module loaded successfully\n");
    
    return 0;
}

// Module cleanup
static void __exit syscall_monitor_exit(void)
{
    unregister_kprobe(&kp_open);
    unregister_kprobe(&kp_read);
    unregister_kprobe(&kp_write);
    
    device_destroy(syscall_class, MKDEV(major_number, 0));
    class_destroy(syscall_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    
    printk(KERN_INFO "SYSCALL_MONITOR: Module unloaded\n");
}

module_init(syscall_monitor_init);
module_exit(syscall_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kiana Katouzian");
MODULE_DESCRIPTION("Syscall Monitor using Kprobes");
MODULE_VERSION("1.0");
