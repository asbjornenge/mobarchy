/* reboot-to <target>: reboot with a target argument (e.g. "bootloader", "recovery"),
 * which busybox's reboot cannot pass. Used by the debug init on phones. */
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/reboot.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: reboot-to <target>\n");
        return 2;
    }
    sync();
    syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, argv[1]);
    perror("reboot");
    return 1;
}
