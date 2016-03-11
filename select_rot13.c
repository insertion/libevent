/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <fcntl.h>
/* for select */
#include <sys/select.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define MAX_LINE 16384

char rot13_char(char c)
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
struct fd_state //自己定义的结构体
{
    char buffer[MAX_LINE];
    size_t buffer_used;

    int writing;
    size_t n_written;
    size_t write_upto;
};

struct fd_state * alloc_fd_state(void)
{
    struct fd_state *state = malloc(sizeof(struct fd_state));
    if (!state)
        return NULL;
    state->buffer_used = state->n_written = state->writing =
        state->write_upto = 0;
    return state;
}

void free_fd_state(struct fd_state *state)
{
    free(state);
}

void make_nonblocking(int fd)
{
    fcntl(fd, F_SETFL, O_NONBLOCK);
}
int do_read(int fd, struct fd_state *state)
{
    char buf[1024];
    int i;
    ssize_t result;
    while (1) {
        result = recv(fd, buf, sizeof(buf), 0);
        if (result <= 0)
            break;

        for (i=0; i < result; ++i)  {
            if (state->buffer_used < sizeof(state->buffer))
                state->buffer[state->buffer_used++] = rot13_char(buf[i]);
            if (buf[i] == '\n') {
                state->writing = 1;
                state->write_upto = state->buffer_used;
            }
        }
    }

    if (result == 0) {
        return 1;
    } else if (result < 0) {
        if (errno == EAGAIN)
            return 0;
        return -1;
    }

    return 0;
}
int do_write(int fd, struct fd_state *state)
{
    while (state->n_written < state->write_upto) {
        ssize_t result = send(fd, state->buffer + state->n_written,
                              state->write_upto - state->n_written, 0);
        if (result < 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }
        assert(result != 0);

        state->n_written += result;
    }

    if (state->n_written == state->buffer_used)
        state->n_written = state->write_upto = state->buffer_used = 0;

    state->writing = 0;

    return 0;
}
/*
   清空集合                                  FD_ZERO(fd_set*)，
   将一个给定的文件描述符加入集合之中       FD_SET(int,fd_set*)，
   将一个给定的文件描述符从集合中删除       FD_CLR(int,fd_set*)，
   检查集合中指定的文件描述符是否可以读写  FD_ISSET(int,fd_set*)
   
   select 返回值：返回状态发生变化的描述符总数。
   负值：select错误
   正值：某些文件可读写或出错
   0：等待超时，没有可读写或错误的文件
*/
void run(void)
{
    int listener;
    struct fd_state *state[FD_SETSIZE];
    struct sockaddr_in sin;
    int i, maxfd;
    fd_set readset, writeset, exset;//定义描述符集

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(40713);

    for (i = 0; i < FD_SETSIZE; ++i)
        state[i] = NULL;//状态指针初始化为0

    listener = socket(AF_INET, SOCK_STREAM, 0);
    make_nonblocking(listener);//使其是非阻塞的

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return;
    }

    if (listen(listener, 16)<0) {
        perror("listen");
        return;
    }
//使用select，我们有O(n)的无差别轮询复杂度
//poll之会把哪个流发生了怎样的I/O事件通知我们。此时我们对这些流的操作都是有意义的。（复杂度降低到了O(1)）
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&exset);

    while (1) {
        maxfd = listener;

        FD_ZERO(&readset);//每次循环都要清空集合，否则不能检测描述符变化 
        FD_ZERO(&writeset);
        FD_ZERO(&exset);

        FD_SET(listener, &readset);
/*
select模型理解：在调用select函数之前，把要关注的fd在set中置1，select返回后，把未准备好的fd置0。
*/
        for (i=0; i < FD_SETSIZE; ++i) //从0到fd最大值，轮询
        {
            if (state[i])//指针不为0
            {
                if (i > maxfd)
                    maxfd = i;
                FD_SET(i, &readset);
                if (state[i]->writing) 
                {
                    FD_SET(i, &writeset);
                }
            }
        }
        //maxfd是指最大的描述符值+1
           if (select(maxfd+1, &readset, &writeset, &exset, NULL) < 0)
/*  struct timeval* timeout是select的超时时间，这个参数至关重要，它可以使select处于三种状态：
第一，若将NULL以形参传入，即不传入时间结构，就是将select置于阻塞状态，一定等到监视文件描述符集合中某个文件描述符发生变化为止；
第二，若将时间值设为0秒0毫秒，非阻塞函数，不管文件描述符是否有变化，都立刻返回继续执行，文件无变化返回0，有变化返回一个正值；
第三，timeout的值大于0，select在timeout时间内阻塞，超时时间之内有事件到来就返回了，否则在超时后一定返回，返回值同上述。
           */ 
           {
            perror("select");
            return;
            }

        if (FD_ISSET(listener, &readset))//相当于轮询了
         {
            struct sockaddr_storage ss;
            socklen_t slen = sizeof(ss);
            int fd = accept(listener, (struct sockaddr*)&ss, &slen);//非阻塞的
            if (fd < 0) {
                perror("accept");
            } 
            else if (fd > FD_SETSIZE)//如果超过select监听的数目，关闭fd
             {
                close(fd);
             }
            else 
            {
                make_nonblocking(fd);
                state[fd] = alloc_fd_state();//初始化该fd状态，所有属性为0,相当与把该fd添加到轮询队列中
                assert(state[fd]);/*XXX*/
            }
        }

        for (i=0; i < maxfd+1; ++i) {
            int r = 0;
            if (i == listener)
                continue;//listener不参与读写

            if (FD_ISSET(i, &readset)) {
                r = do_read(i, state[i]);
            }//是读的就读，读好了返回0
            if (r == 0 && FD_ISSET(i, &writeset)) {
                r = do_write(i, state[i]);
            }
            if (r) {//如果r不等与0，表示读写错误
                free_fd_state(state[i]);//清除fd状态
                state[i] = NULL;
                close(i);
            }
        }
    }
}
int
main(int c, char **v)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    run();
    return 0;
}