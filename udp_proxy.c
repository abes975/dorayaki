#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "socket_pool.h"
#include "debug.h"

int fake_server()
{
    socket_pool_t* pool;
    int pool_capacity = 10;
    int maxfd = -1;
    fd_set rd_set;
   
    int sockfd,n;
    struct sockaddr_in servaddr, remservaddr;
    socklen_t len;
    //socklen_t len1;
    //socklen_t len3;
    int rcv_bytes;
    u_int16_t port = 53;
    char msg[1000];
    char msg1[1000];


    // proxy socket
    pool = socket_pool_create(pool_capacity);
    if (!pool) {
        perror("Socket pool is null....");
        exit(-1);
    }
    
    // this is the listening socket
    sockfd=socket(AF_INET, SOCK_DGRAM, 0);

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.2", &servaddr.sin_addr.s_addr);
    //servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(port);
    
    if (bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr))) {
        perror("Cannot complete bind...maybe socket already in use or maybe you're not root :)");
        close(sockfd);
    }
    
    // Relay socket part
    memset(&remservaddr, 0, sizeof(struct sockaddr_in));
    remservaddr.sin_family = AF_INET;
    remservaddr.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remservaddr.sin_addr.s_addr);
    // end relay socket

    FD_ZERO(&rd_set);
    maxfd = (sockfd > socket_pool_max_fd_used(pool)) ? sockfd + 1: socket_pool_max_fd_used(pool) + 1;

    for (;;)
    {
        // copy all the set from the pool :)
        rd_set = pool->rd_set;
        FD_SET(sockfd, &rd_set);
        int i;
        char straddr[INET_ADDRSTRLEN];
        if (select(maxfd, &rd_set, NULL, NULL, NULL) == -1) {
            ERROR_MSG(stderr, "Select returned -1 (%s)\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        // we have a new request from attackers ;-)
        if (FD_ISSET(sockfd, &rd_set)) {
            conversation_t* proxy = socket_pool_acquire(pool);
            if (proxy) {
                //now relay traffic
                len = sizeof(proxy->clientaddr);
                n = recvfrom(sockfd, msg, 1000, 0, (struct sockaddr *)&(proxy->clientaddr), &len);
                inet_ntop(AF_INET, &(proxy->clientaddr.sin_addr), straddr, INET_ADDRSTRLEN);
                printf("-------------------------------------------------------\n");
                msg[n] = '\0';
               
                for (i = 0; i < n; i++)
                    printf("%2X", (int)msg[i]);
                printf("\n");
                printf("-------------------------------------------------------\n");


                if(sendto(proxy->sock_fd, msg, n, 0, 
                    (struct sockaddr *)&remservaddr, sizeof(remservaddr)) == -1) {
                    perror("Errore mentre effettuo il relay");
                    continue;
                } else {
                    inet_ntop(AF_INET, &remservaddr.sin_addr, straddr, INET_ADDRSTRLEN);
                    printf("Ho spedito %d bytes verso %s\n", n, straddr);
                }
            } else {
                printf("We don't have elements available or pool is null discard message\n");
            }
        }
        // see if we can free some element 
        conversation_t* elem;
        for (elem = pool->used_head; elem != NULL; elem = elem->next) {
            printf("Checking for element %d\n", elem->sock_fd);
            if(FD_ISSET(elem->sock_fd, &rd_set)) {
                // we got an answer
                // end relay...give response back
                printf("Got a message from fd = %d\n", elem->sock_fd);
                printf("Now we have %d used element, %d free element\n", 
                    socket_pool_how_many_used(pool), socket_pool_how_many_free(pool));
                rcv_bytes = recvfrom(elem->sock_fd,  msg1, 1000, 0, NULL, NULL);
                // send back response and then release socket
                sendto(sockfd, msg1, rcv_bytes, 0, (struct sockaddr *)&elem->clientaddr, sizeof(elem->clientaddr));
                socket_pool_release(pool, elem);
                printf("Release: Now we have %d used element, %d free element\n", 
                    socket_pool_how_many_used(pool),socket_pool_how_many_free(pool));
                break;
            }
        }
    
    }
    close(sockfd);
    socket_pool_free(pool);
}

int main(int argc, char** argv)
{
    fake_server();
    return 1;
}

