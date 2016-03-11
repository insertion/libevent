/*
epoll_ctl(2) 用来添加或删除监听 epoll 实例的描述符。
epoll_wait(2) 用来等待被监听的描述符事件，一直阻塞到事件可用
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
//首先通过create_epoll(int maxfds)来创建一个epoll的句柄。
//这个函数会返回一个新的epoll句柄，之后的所有操作将通过这个句柄来进行操作。
//在用完之后，记得用close()来关闭这个创建出来的epoll句柄。
/*
1、int epoll_create(int size)

创建一个epoll句柄，参数size用来告诉内核监听的数目。

2、int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)

epoll事件注册函数，

　　参数epfd为epoll的句柄；

　　参数op表示动作，用3个宏来表示：EPOLL_CTL_ADD(注册新的fd到epfd)，EPOLL_CTL_MOD(修改已经注册的fd的监听事件)，EPOLL_CTL_DEL(从epfd删除一个fd)；

　　参数fd为需要监听的标示符；

　　参数event告诉内核需要监听的事件，event的结构如下
*/
#include <errno.h>

#define MAXEVENTS 64

static int make_socket_non_blocking (int sfd)
{
  int flags, s;

  flags = fcntl (sfd, F_GETFL, 0);
  if (flags == -1)
    {
      perror ("fcntl");
      return -1;
    }

  flags |= O_NONBLOCK;
  s = fcntl (sfd, F_SETFL, flags);
  if (s == -1)
    {
      perror ("fcntl");
      return -1;
    }

  return 0;
}

static int create_and_bind (char *port)
{
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int s, sfd;

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
  hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
  hints.ai_flags = AI_PASSIVE;     /* All interfaces */

  s = getaddrinfo (NULL, port, &hints, &result);
  if (s != 0)
    {
      fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
      return -1;
    }

  for (rp = result; rp != NULL; rp = rp->ai_next)
    {
      sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sfd == -1)
        continue;

      s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
      if (s == 0)
        {
          /* We managed to bind successfully! */
          break;
        }

      close (sfd);
    }

  if (rp == NULL)
    {
      fprintf (stderr, "Could not bind\n");
      return -1;
    }

  freeaddrinfo (result);

  return sfd;
}

int main (int argc, char *argv[])
{
  int sfd, s;
  int efd;
  struct epoll_event event;
  /*
   typedef union epoll_data {
        void *ptr;
         int fd;
         __uint32_t u32;
         __uint64_t u64;
     } epoll_data_t;

  struct epoll_event {
         __uint32_t events;      
         epoll_data_t data;     
     };
   其中events表示感兴趣的事件和被触发的事件，可能的取值为：
   EPOLLIN：表示对应的文件描述符可以读；
   EPOLLOUT：表示对应的文件描述符可以写；
   EPOLLPRI：表示对应的文件描述符有紧急的数可读；
   EPOLLERR：表示对应的文件描述符发生错误；
   EPOLLHUP：表示对应的文件描述符被挂断；
   EPOLLET ：ET的epoll工作模式；
  */
  struct epoll_event *events;

  if (argc != 2)//需要输入端口号
    {
      fprintf (stderr, "Usage: %s [port]\n", argv[0]);
      exit (EXIT_FAILURE);
    }

  sfd = create_and_bind (argv[1]);//绑定端口号
  if (sfd == -1)
    abort ();

  s = make_socket_non_blocking (sfd);
  if (s == -1)
    abort ();

  s = listen (sfd, SOMAXCONN);
  if (s == -1)
    {
      perror ("listen");
      abort ();
    }
/*epoll start */
  efd = epoll_create1 (0);
 /*epoll_create函数 函数声明：int epoll_create(int size) 该函数生成一个epoll专用的文件描述符。
 它其实是在内核申请一空间，用来存放你想关注的socket fd上是否发生以及发生了什么事件。
 size就是你在这个epoll fd上能关注的最大socket fd数
 */
  if (efd == -1)
    {
      perror ("epoll_create");
      abort ();
    }
//===============================================================
  event.data.fd = sfd;//event 关注的第一个fd是sfd
  event.events = EPOLLIN | EPOLLET;
  //events表示感兴趣的事件和被触发的事件
  s = epoll_ctl (efd, EPOLL_CTL_ADD, sfd, &event);
//================================================================
  if (s == -1)
    {
      perror ("epoll_ctl");
      abort ();
    }

  /* Buffer where events are returned */
  events = calloc (MAXEVENTS, sizeof event);

  /* The event loop */
  while (1)
    {
      int n, i;

      n = epoll_wait (efd, events, MAXEVENTS, -1);
      //返回值是事件数
     //events 用于回传待处理事件的数组，所有准备好的事件都保存在events中
      for (i = 0; i < n; i++)
	{
	  if ((events[i].events & EPOLLERR) ||
              (events[i].events & EPOLLHUP) ||
              (!(events[i].events & EPOLLIN)))
	    {
              /* An error has occured on this fd, or the socket is not
                 ready for reading (why were we notified then?) */
	      fprintf (stderr, "epoll error\n");
	      close (events[i].data.fd);
	      continue;
	    }

	  else if (sfd == events[i].data.fd)
	    {
              /* We have a notification on the listening socket, which
                 means one or more incoming connections. */
              while (1)
                {
                  struct sockaddr in_addr;
                  socklen_t in_len;
                  int infd;
                  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                  in_len = sizeof in_addr;
                  infd = accept (sfd, &in_addr, &in_len);
                  if (infd == -1)
                    {
                      if ((errno == EAGAIN) ||
                          (errno == EWOULDBLOCK))
                        {
                          /* We have processed all incoming
                             connections. */
                          break;
                        }
                      else
                        {
                          perror ("accept");
                          break;
                        }
                    }

                  s = getnameinfo (&in_addr, in_len,
                                   hbuf, sizeof hbuf,
                                   sbuf, sizeof sbuf,
                                   NI_NUMERICHOST | NI_NUMERICSERV);
                  if (s == 0)
                    {
                      printf("Accepted connection on descriptor %d "
                             "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                    }

                  /* Make the incoming socket non-blocking and add it to the
                     list of fds to monitor. */
                  s = make_socket_non_blocking (infd);
                  if (s == -1)
                    abort ();
//===========================================================================
                  event.data.fd = infd;
                  event.events = EPOLLIN | EPOLLET;
                  /*
                  event.data.fd=infd意义在于绑定事件和fd,说明事件是属于哪个fd的
                  */
                  s = epoll_ctl (efd, EPOLL_CTL_ADD, infd, &event);
                  /*
                  这里的第三个参数是用来添加到efd的关注列表里的，与event没有关系，所以与之前的event.data.fd=infd并补重复
                  */
//=========================================================================
                  if (s == -1)
                    {
                      perror ("epoll_ctl");
                      abort ();
                    }
                }
              continue;
            }
          else
            {
              /* We have data on the fd waiting to be read. Read and
                 display it. We must read whatever data is available
                 completely, as we are running in edge-triggered mode
                 and won't get a notification again for the same
                 data. */
              int done = 0;

              while (1)
                {
                  ssize_t count;
                  char buf[512];

                  count = read (events[i].data.fd, buf, sizeof buf);
                  if (count == -1)
                    {
                      /* If errno == EAGAIN, that means we have read all
                         data. So go back to the main loop. */
                      if (errno != EAGAIN)
                        {
                          perror ("read");
                          done = 1;
                        }
                      break;
                    }
                  else if (count == 0)
                    {
                      /* End of file. The remote has closed the
                         connection. */
                      done = 1;
                      break;
                    }

                  /* Write the buffer to standard output */
                  s = write (1, buf, count);
                  if (s == -1)
                    {
                      perror ("write");
                      abort ();
                    }
                }

              if (done)
                {
                  printf ("Closed connection on descriptor %d\n",
                          events[i].data.fd);

                  /* Closing the descriptor will make epoll remove it
                     from the set of descriptors which are monitored. */
                  close (events[i].data.fd);
                }
            }
        }
    }

  free (events);

  close (sfd);

  return EXIT_SUCCESS;
}
