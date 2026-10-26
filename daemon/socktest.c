/* 直通socket测试客户端: ./socktest <S|G|A> <强> <弱> <ms> */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "用法: %s S|G|A 强 弱 ms\n", argv[0]); return 1; }
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    a.sun_path[0] = '\0';
    strcpy(a.sun_path + 1, "rumble_driver");
    socklen_t l = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen("rumble_driver"));
    if (connect(s, (struct sockaddr *)&a, l) < 0) { perror("connect"); return 1; }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %d %d %d\n", argv[1], atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    if (write(s, buf, strlen(buf)) < 0) { perror("write"); return 1; }
    close(s);
    printf("已发送: %s", buf);
    return 0;
}
