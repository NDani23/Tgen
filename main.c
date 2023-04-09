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
#include <netinet/tcp.h>

#include "ff_config.h"
#include "ff_api.h"

#define MAX_EVENTS 1024
#define BUF_SIZE 60000

/* kevent set */
struct kevent kevSet;
/* events */
struct kevent events[MAX_EVENTS];
/* kq */
int kq;
int sockfd;

char buf[BUF_SIZE];
unsigned long received_bytes=0;

void set_nonblocking(int fd)
{
    int on = 1;
    if(ff_ioctl(fd, FIONBIO, &on) == -1){
        printf("Failed to set nonblocking!\n");
        ff_close(fd);
        exit(1);
    }
}

void set_nodelay(int fd)
{
    int flag = 1;

    int ret = ff_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    if(ret < 0 ){
        printf("Failed to set nodelay!\n");
        ff_close(fd);
        exit(1);
    }
}

void set_address(struct sockaddr_in *addr, char ip[], int port)
{
    bzero((char *)addr, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;

    if(strcmp("ANY", ip) == 0)
    {
        
        addr->sin_addr.s_addr = htonl(INADDR_ANY);  
    }
    else
    {
        addr->sin_addr.s_addr = inet_addr(ip);
    }
    
    addr->sin_port = htons(port);
}

void setup_server(int server_fd, struct linux_sockaddr *addr){

    int ret = ff_bind(server_fd, addr, sizeof(*addr));
    if (ret < 0) {
        printf("ff_bind failed, sockfd:%d, errno:%d, %s\n", sockfd, errno, strerror(errno));
        exit(1);
    }

    ret = ff_listen(server_fd, MAX_EVENTS);
    if (ret < 0) {
        printf("ff_listen failed, sockfd:%d, errno:%d, %s\n", sockfd, errno, strerror(errno));
        exit(1);
    }

    //setting up read event for server socket
    EV_SET(&kevSet, sockfd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
    /* Update kqueue */
    ff_kevent(kq, &kevSet, 1, NULL, 0, NULL);
    
}

void create_timer(int queue_fd, int period, int one_shot){

    struct kevent events[1];
    if(one_shot){
        EV_SET(&events[0], 1234, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, period, NULL);
    }
    else{
        EV_SET(&events[0], 1234, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, period, NULL);
    }

    if(ff_kevent(queue_fd, events, 1, NULL, 0, NULL) != 0){
        printf("failed to add timer to kevent!\n");
        ff_close(sockfd);
        ff_close(queue_fd);
        exit(1);
    }

}

void handle_new_connection(struct kevent *event){

    int clientfd = (int)event->ident;
    int available = (int)event->data;

    do {
        int nclientfd = ff_accept(clientfd, NULL, NULL);
        if (nclientfd < 0) {
            printf("ff_accept failed:%d, %s\n", errno,
            strerror(errno));
            break;
        }

        /* Add to event list */
        EV_SET(&kevSet, nclientfd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);

        if(ff_kevent(kq, &kevSet, 1, NULL, 0, NULL) < 0) {
            printf("ff_kevent error:%d, %s\n", errno,
                strerror(errno));
            break;
        }

        available--;

        printf("Connected with client!\n");
    } while (available);

}

void read_handler(int fd)
{
    
    ssize_t readlen;
    while(1)
    {
        readlen = ff_read(fd, buf, sizeof(buf));
        received_bytes += readlen;

        if(readlen == 0){
            printf("Client finished sending!\n");
            return;
        } else if(readlen < 0 && errno == EAGAIN){
            //printf("Socket read buffer is empty!\n");
            return;        
        }
    }    
}

void timer_handler()
{
    //printf("Received bytes: %uld\n", received_bytes);
    received_bytes = 0;
}

int loop(void *arg)
{
    /* Wait for events to happen */
    int nevents = ff_kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);
    int i;
    

    if (nevents < 0) {
        printf("ff_kevent failed:%d, %s\n", errno,
                        strerror(errno));
        return -1;
    }

    for (i = 0; i < nevents; ++i) {
        struct kevent event = events[i];
        int clientfd = (int)event.ident;

        /* Handle disconnect */
        if (event.flags & EV_EOF) {
            /* Simply close socket */
            ff_close(clientfd);
            

        } else if (clientfd == sockfd) {

            handle_new_connection(&event);

        } else if (event.filter == EVFILT_READ) {

            //event_count++;

            //printf("Read count: %d\n", event_count);
            read_handler(clientfd);
           
        } else if (event.filter == EVFILT_TIMER){

            timer_handler();
            
        } 
        else {
            printf("unknown event: %8.8X\n", event.flags);
        }
    }

    return 0;
}

int main(int argc, char * argv[])
{
    ff_init(argc, argv);

    //Creating kernel queue
    kq = ff_kqueue();
    if (kq < 0) {
        printf("ff_kqueue failed, errno:%d, %s\n", errno, strerror(errno));
        exit(1);
    }

    //Creating server socket
    sockfd = ff_socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("ff_socket failed, sockfd:%d, errno:%d, %s\n", sockfd, errno, strerror(errno));
        exit(1);
    }


    set_nonblocking(sockfd);

    set_nodelay(sockfd);

    struct sockaddr_in my_addr;
    set_address(&my_addr, "ANY", 8080);
    
    setup_server(sockfd, (struct linux_sockaddr*)&my_addr);

    create_timer(kq, 1, 0);

    //start callback loop
    ff_run(loop, NULL);
    return 0;
}
