#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define CLOSE_CMD       (_IO(0xEF, 0x01))       /* 关闭定时器 */
#define OPEN_CMD        (_IO(0xEF, 0x02))       /* 打开定时器 */
#define SETPERIOD_CMD   (_IO(0xEF, 0x03))       /* 设置定时器周期指令 */

int main(int argc, char *argv[])
{
        int ret = 0;
        int fd;
        int arg = 0, cmd = 0;
        char *filename = NULL;
        unsigned char str[10] = {0};

        if (argc != 2) {
                printf("Error Usage!\n"
                       "Usage %s filename 0:1\n"
                       ,argv[0]);
                ret = -1;
                goto error;
        }

        filename = argv[1];
        fd = open(filename, O_RDWR);
        if (fd == -1) {
                perror("open failed!\n");
                ret = -1;
                goto error;
        }

        while (1) {
                printf("Input CMD:");
                ret = scanf("%d", &cmd);
                printf("ret = %d\n", ret);
                if (cmd == 1) {
                        cmd = CLOSE_CMD;
                        ioctl(fd, (int)&cmd, (int)&arg);
                } else if (cmd == 2) {
                        cmd = OPEN_CMD;
                        ioctl(fd, (int)&cmd, (int)&arg);
                } else if (cmd == 3) {
                        printf("Input arg:");
                        scanf("%d", &arg);
                        printf("arg = %d\n", arg);
                        cmd = SETPERIOD_CMD;
                        ioctl(fd, (int)&cmd, (int)&arg);
                }
        }

error:
        close(fd);
        return ret;
}
