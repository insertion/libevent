/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_LINE 16384
char rot13_char(char c)//回转13位编码
{
    /* We don't want to use isalpha here; setting the locale would change
     * which characters are considered alphabetical. */
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
        return c + 13;
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
        return c - 13;
    else
        return c;
}

void  child(int fd)
{
    char outbuf[MAX_LINE+1];
    size_t outbuf_used = 0;//无符号
    ssize_t result;//有符号

    while (1) {
        char ch;
        result = recv(fd, &ch, 1, 0);
        if (result == 0) {
            break;
        } else if (result == -1) {
            perror("read");
            break;
        }

        /* We do this test to keep the user from overflowing the buffer. */
        if (outbuf_used < sizeof(outbuf)) {
            outbuf[outbuf_used++] = rot13_char(ch);
        }//防止缓冲区溢出

        if (ch == '\n') {//遇到回车就发送出去
            send(fd, outbuf, outbuf_used, 0);
            outbuf_used = 0;
            continue;
        }
    }
}

void run(void)
{
    int listener;
    struct sockaddr_in sin;//定义socket地址结构

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;//uint32 sin_addr是一个将结构体，该结构体元素为s_addr，类型为in_addr_t (192.168.3.144记为0xc0a80390)
    /*
    inet_aton("127.0.0.1",&sin.sin_addr);
    用来转换ascill和numeric
    */
    sin.sin_port = htons(40713);

    listener = socket(AF_INET, SOCK_STREAM, 0);

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");//地址空间和描述符绑定
        return;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        return;
    }
    while (1) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(listener, (struct sockaddr*)&ss, &slen);
        //参数ss和slen存放客户方的地址信息。调用前，参数ss指向一个初始值为空的地址结构，而slen的初始值为0
        //accept 新建了一个fd
        if (fd < 0) {
            perror("accept");
        } 
        else 
        {
            if (fork() == 0)
             {//fork==0表示子进程
                child(fd);
                exit(0);
             }
        }
    }
}

int main(int c, char **v)
{
    run();
    return 0;
}