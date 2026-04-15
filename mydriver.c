#include <asm/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "mydriver.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Character device driver with IRQ simulation and mmap");
MODULE_VERSION("1.0");

/* ─── global state ──────────────────────────────────────────── */
static dev_t         dev_num;          /* major:minor number             */
static struct cdev   my_cdev;
static struct class  *my_class  = NULL;
static struct device *my_device = NULL;

static char          *shared_buf = NULL; /* page-aligned kernel buffer    */
static spinlock_t    buf_lock;
static atomic_t      irq_count   = ATOMIC_INIT(0);

static struct timer_list sim_timer;    /* simulates periodic "hardware"  */
static struct proc_dir_entry *proc_entry;

/* ─── simulated interrupt handler ───────────────────────────── */
/*
 * In a real driver this would be: irqreturn_t my_irq_handler(int irq, void *dev)
 * Here we use a kernel timer to fire every 5 s as a stand-in.
 */
static void sim_irq_handler(struct timer_list *t)
{
    unsigned long flags;

    atomic_inc(&irq_count);

    spin_lock_irqsave(&buf_lock, flags);
    snprintf(shared_buf, MMAP_SIZE,
             "IRQ #%d fired at jiffies=%lu", atomic_read(&irq_count), jiffies);
    spin_unlock_irqrestore(&buf_lock, flags);

    printk(KERN_INFO "[mydriver] simulated IRQ fired — count=%d | buf: '%s'\n",
           atomic_read(&irq_count), shared_buf);

    /* re-arm: fire again in 5 seconds */
    mod_timer(&sim_timer, jiffies + msecs_to_jiffies(5000));
}

/* ─── file_operations callbacks ─────────────────────────────── */

static int mydev_open(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "[mydriver] device opened by PID %d\n", current->pid);
    return 0;
}

static int mydev_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "[mydriver] device closed\n");
    return 0;
}

static ssize_t mydev_read(struct file *filp, char __user *buf,
                          size_t len, loff_t *off)
{
    unsigned long flags;
    size_t to_copy;
    int ret;

    if (*off >= MMAP_SIZE)
        return 0;

    to_copy = min(len, (size_t)(MMAP_SIZE - *off));

    spin_lock_irqsave(&buf_lock, flags);
    ret = copy_to_user(buf, shared_buf + *off, to_copy);
    spin_unlock_irqrestore(&buf_lock, flags);

    if (ret)
        return -EFAULT;

    *off += to_copy;
    printk(KERN_INFO "[mydriver] read %zu bytes\n", to_copy);
    return to_copy;
}

static ssize_t mydev_write(struct file *filp, const char __user *buf,
                           size_t len, loff_t *off)
{
    unsigned long flags;
    size_t to_copy;
    int ret;

    to_copy = min(len, (size_t)(MMAP_SIZE - 1));

    spin_lock_irqsave(&buf_lock, flags);
    ret = copy_from_user(shared_buf, buf, to_copy);
    shared_buf[to_copy] = '\0';
    spin_unlock_irqrestore(&buf_lock, flags);

    if (ret)
        return -EFAULT;

    printk(KERN_INFO "[mydriver] written %zu bytes: '%s'\n", to_copy, shared_buf);
    return to_copy;
}

static long mydev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    unsigned long flags;
    int count;

    switch (cmd) {

    case IOCTL_TRIGGER_IRQ:
        /* manually fire the simulated IRQ */
        sim_irq_handler(&sim_timer);
        printk(KERN_INFO "[mydriver] IOCTL_TRIGGER_IRQ: manual trigger\n");
        break;

    case IOCTL_RESET_BUF:
        spin_lock_irqsave(&buf_lock, flags);
        memset(shared_buf, 0, MMAP_SIZE);
        spin_unlock_irqrestore(&buf_lock, flags);
        printk(KERN_INFO "[mydriver] IOCTL_RESET_BUF: buffer cleared\n");
        break;

    case IOCTL_GET_IRQ_COUNT:
        count = atomic_read(&irq_count);
        if (copy_to_user((int __user *)arg, &count, sizeof(count)))
            return -EFAULT;
        printk(KERN_INFO "[mydriver] IOCTL_GET_IRQ_COUNT: %d\n", count);
        break;

    default:
        return -ENOTTY;
    }
    return 0;
}

/* ─── memory mapping ─────────────────────────────────────────── */
/*
 * remap_pfn_range maps the kernel buffer page(s) directly into
 * the userspace VMA — zero-copy sharing.
 */
static int mydev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long pfn;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (size > MMAP_SIZE) {
        printk(KERN_WARNING "[mydriver] mmap: requested size too large\n");
        return -EINVAL;
    }

    pfn = virt_to_phys(shared_buf) >> PAGE_SHIFT;

    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
        printk(KERN_ERR "[mydriver] mmap: remap_pfn_range failed\n");
        return -EAGAIN;
    }

    printk(KERN_INFO "[mydriver] mmap: %lu bytes mapped to userspace @ 0x%lx\n",
           size, vma->vm_start);
    return 0;
}

/* ─── /proc interface ────────────────────────────────────────── */
static int proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "IRQ count  : %d\n", atomic_read(&irq_count));
    seq_printf(m, "Buffer     : %s\n", shared_buf[0] ? shared_buf : "(empty)");
    return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

static const struct proc_ops proc_fops = {
    .proc_open    = proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ─── file_operations table ──────────────────────────────────── */
static const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = mydev_open,
    .release        = mydev_release,
    .read           = mydev_read,
    .write          = mydev_write,
    .unlocked_ioctl = mydev_ioctl,
    .mmap           = mydev_mmap,
};

/* ─── module init / exit ─────────────────────────────────────── */
static int __init mydriver_init(void)
{
    int ret;

    printk(KERN_INFO "[mydriver] initialising module\n");

    /* 1. allocate a page-aligned buffer for shared memory */
    shared_buf = (char *)get_zeroed_page(GFP_KERNEL);
    if (!shared_buf) {
        printk(KERN_ERR "[mydriver] failed to allocate shared buffer\n");
        return -ENOMEM;
    }
    spin_lock_init(&buf_lock);
    printk(KERN_INFO "[mydriver] shared buffer allocated at %p\n", shared_buf);

    /* 2. dynamically allocate major:minor */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "[mydriver] alloc_chrdev_region failed: %d\n", ret);
        goto err_free_buf;
    }
    printk(KERN_INFO "[mydriver] registered with major=%d minor=%d\n",
           MAJOR(dev_num), MINOR(dev_num));

    /* 3. initialise and add the cdev */
    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;
    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "[mydriver] cdev_add failed: %d\n", ret);
        goto err_unregister;
    }

    /* 4. create device class and node → /dev/mydevice */
    my_class = class_create(CLASS_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        goto err_cdev_del;
    }

    my_device = device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(my_device)) {
        ret = PTR_ERR(my_device);
        goto err_class_destroy;
    }
    printk(KERN_INFO "[mydriver] /dev/%s created\n", DEVICE_NAME);

    /* 5. /proc entry */
    proc_entry = proc_create("mydriver", 0444, NULL, &proc_fops);
    if (!proc_entry)
        printk(KERN_WARNING "[mydriver] /proc/mydriver creation failed\n");

    /* 6. arm the simulated IRQ timer (fires every 5 s) */
    timer_setup(&sim_timer, sim_irq_handler, 0);
    mod_timer(&sim_timer, jiffies + msecs_to_jiffies(5000));
    printk(KERN_INFO "[mydriver] simulated IRQ timer armed (5 s interval)\n");

    printk(KERN_INFO "[mydriver] module loaded successfully\n");
    return 0;

err_class_destroy:
    class_destroy(my_class);
err_cdev_del:
    cdev_del(&my_cdev);
err_unregister:
    unregister_chrdev_region(dev_num, 1);
err_free_buf:
    free_page((unsigned long)shared_buf);
    return ret;
}

static void __exit mydriver_exit(void)
{
    del_timer_sync(&sim_timer);
    proc_remove(proc_entry);
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);
    free_page((unsigned long)shared_buf);
    printk(KERN_INFO "[mydriver] module unloaded\n");
}

module_init(mydriver_init);
module_exit(mydriver_exit);