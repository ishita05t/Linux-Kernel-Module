#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "mydriver.h"

#define DEV_PATH "/dev/mydevice"

int main(void)
{
    int   fd, irq_count;
    char  *mapped;
    char   rbuf[256];
    ssize_t n;

    /* ── 1. open the device ── */
    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    printf("[test] opened %s\n", DEV_PATH);

    /* ── 2. write something ── */
    const char *msg = "Hello from userspace!";
    n = write(fd, msg, strlen(msg));
    printf("[test] wrote %zd bytes\n", n);

    /* ── 3. read it back ── */
    lseek(fd, 0, SEEK_SET);
    n = read(fd, rbuf, sizeof(rbuf) - 1);
    if (n > 0) { rbuf[n] = '\0'; printf("[test] read: '%s'\n", rbuf); }

    /* ── 4. mmap — zero-copy access to kernel buffer ── */
    mapped = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) { perror("mmap"); goto cleanup; }
    printf("[test] mmap succeeded — kernel buffer visible at %p\n", mapped);
    printf("[test] mmap content: '%s'\n", mapped);

    /* write via mmap directly */
    snprintf(mapped, MMAP_SIZE, "Written via mmap!");
    printf("[test] wrote to mmap region\n");

    /* ── 5. ioctl: trigger simulated IRQ ── */
    if (ioctl(fd, IOCTL_TRIGGER_IRQ) < 0) perror("ioctl TRIGGER_IRQ");
    else printf("[test] IOCTL_TRIGGER_IRQ sent\n");

    /* ── 6. ioctl: read IRQ count ── */
    if (ioctl(fd, IOCTL_GET_IRQ_COUNT, &irq_count) < 0)
        perror("ioctl GET_IRQ_COUNT");
    else
        printf("[test] IRQ count = %d\n", irq_count);

    /* ── 7. ioctl: reset buffer ── */
    if (ioctl(fd, IOCTL_RESET_BUF) < 0) perror("ioctl RESET_BUF");
    else printf("[test] buffer reset via ioctl\n");
    printf("[test] mmap after reset: '%s'\n", mapped[0] ? mapped : "(empty)");

    /* ── 8. check /proc ── */
    printf("\n[test] /proc/mydriver:\n");
    system("cat /proc/mydriver");

    munmap(mapped, MMAP_SIZE);
cleanup:
    close(fd);
    printf("[test] done\n");
    return 0;
}