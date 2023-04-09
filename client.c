#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/tcp.h>

#include "ff_config.h"
#include "ff_api.h"

#define DEFAULT_PORT 8080
#define MAX_CONN 16
#define MAX_EVENTS 1024
#define BUF_SIZE 16384

//struct epoll_event events[MAX_EVENTS];
struct kevent events[MAX_EVENTS];

int n;
//int epfd;
int kq;
int nfds;
int sockfd;

int event_count=0;

char buf[BUF_SIZE];

struct sockaddr_in srv_addr;

struct timespec *timeout=NULL;

void set_nodelay(int fd)
{
    int flag = 1;
    int ret = ff_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    if(ret < 0){
        printf("failed to set nodelay!\n");
        ff_close(fd);
        ff_close(kq);
        exit(1);
    }
}

void set_nonblocking(int fd){
    int on = 1;
    if(ff_ioctl(fd, FIONBIO, &on) < 0){
        printf("Failed to set nonblocking!\n");
        ff_close(fd);
        ff_close(kq);
        exit(1);
    }
}

void set_reuseaddr(int fd){
    if(ff_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1){
        printf("Failed to set reuseaddr!\n");
        ff_close(sockfd);
        ff_close(kq);
        exit(1);
    }
}

void set_kqueue(){

    kq = ff_kqueue();
    if(kq < 0){
        printf("Failed to create kq!\n");
        ff_close(sockfd);
        exit(1);
    }

    struct kevent con_events[1];
    EV_SET(&con_events[0], sockfd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if(ff_kevent(kq, con_events, 1, NULL, 0, NULL) == -1){

        printf("Failed to add event to kq!\n");
        ff_close(sockfd);
        ff_close(kq);
        exit(1);

    }
}

void connect_to_server(char *addr, int port){

    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(addr);

    int ret = ff_connect(sockfd, (struct linux_sockaddr *)&serv_addr, sizeof(serv_addr));

}

void write_handler(){

    int offset = 0;
    ssize_t writelen;

    while(1){
        writelen = ff_write(sockfd, buf+offset, BUF_SIZE-offset);

        if(writelen > 0){
            offset += writelen;

            if(offset >= BUF_SIZE-1)
                return;
        } else if (writelen < 0 && errno == EAGAIN) {
            return;
        } else {
            printf("FATAL!\n");
            ff_close(sockfd);
            ff_close(kq);
            exit(1);
        }
        

    } 
    
}

void connect_handler(){

    if(events[0].filter == EVFILT_WRITE){

        errno = 0;
        if(events[0].flags & EV_EOF)
            {
                errno = events[0].fflags;
                //TODO: handle connect errors
            }
            else 
            {
                printf("Connected succesfully!\n");
            }           
    }

   write_handler();
}

int loop(void *arg)
{

    int nevents = ff_kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);

    if(nevents < 0){
        printf("ff_kevent failed!\n");
        ff_close(sockfd);
        ff_close(kq);
        return -1;
    }
    
    for(int i=0; i < nevents; i++){

        if (errno == EINPROGRESS){

            connect_handler();
            
        } else if (events[i].flags & EV_EOF){
            printf("Server closed the connection!\n");
            ff_close(sockfd);
            ff_close(kq);
            return -1;
        } 
        else if (events[i].filter == EVFILT_WRITE){

            event_count++;

            //printf("Write count:%d\n", event_count);
            
            write_handler();

        }
    }  
}


int main(int argc, char * argv[])
{
    ff_init(argc, argv);

    sockfd = ff_socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("ff_socket failed, sockfd:%d, errno:%d, %s\n", sockfd, errno, strerror(errno));
        exit(1);
    }

    set_kqueue();

    set_nodelay(sockfd);

    set_nonblocking(sockfd);

    set_reuseaddr(sockfd);

    connect_to_server("192.168.0.194", 8080);
    
    ff_run(loop, NULL);

    return 0;
}
