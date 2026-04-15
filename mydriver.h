#ifndef MYDRIVER_H
#define MYDRIVER_H

#include <linux/ioctl.h>

#define DEVICE_NAME     "mydevice"
#define CLASS_NAME      "mydrv_class"
#define MMAP_SIZE       4096        /* one page of shared memory */

/* ioctl magic number — pick something unique */
#define MY_IOC_MAGIC    'k'

/* ioctl commands */
#define IOCTL_TRIGGER_IRQ   _IO(MY_IOC_MAGIC,  0)   /* simulate interrupt    */
#define IOCTL_RESET_BUF     _IO(MY_IOC_MAGIC,  1)   /* zero shared buffer    */
#define IOCTL_GET_IRQ_COUNT _IOR(MY_IOC_MAGIC, 2, int) /* read IRQ counter    */

#endif /* MYDRIVER_H */