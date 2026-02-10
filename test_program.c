#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    for (int i = 0; i < 100000; i++) {
        int fd = open("/etc/hostname", O_RDONLY);
        if (fd >= 0) {
            char buf[64];
            read(fd, buf, sizeof(buf));
            close(fd);
        }
    }
    return 0;
}
