#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>

#include "socket_pool.h"
#include "debug.h"

#define MAX_SONS 10

volatile sig_atomic_t keep_going = 1;
sem_t read_sem;
sem_t write_sem;

void break_loop(int sig_num);
int instantiate_proxy(char* forwarder, int read_from_sockfd);
int fake_server(char* listener, char* forwarder);


void break_loop(int sig_num)
{
    keep_going = 0;
    signal(SIGINT, break_loop);
}

int fake_server(char* listener, char* forwarder)
{
    pid_t cpid[MAX_SONS];
    pid_t wpid;
    int status = -1;
    int i;

    int sockfd;
    struct sockaddr_in servaddr;
    u_int16_t port = 53;
    

    /* listening socket */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port=htons(port);
    if (inet_pton(AF_INET, listener, &servaddr.sin_addr.s_addr) != 1) {
        perror("Cannot convert listener exiting\n");
        exit(EXIT_FAILURE);
    }

    // this is the listening socket
    sockfd=socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("Cannot allocate listening socket. Exiting\n");
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) == -1) {
        perror("Cannot complete bind...");
        fprintf(stderr, "%s", strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if(sem_init(&read_sem, 1, 1) < 0)
    {
      perror("Read semaphore initilization");
      exit(EXIT_FAILURE);
    }

    if(sem_init(&write_sem, 1, 1) < 0)
    {
      perror("Read semaphore initilization");
      exit(EXIT_FAILURE);
    }

    // install a signal handler in oder to break endless loop
    signal(SIGINT, break_loop);

    for (i = 0; i < MAX_SONS; i++) {
        
        if ((cpid[i] = fork()) == - 1) {
            perror("Cannot complete fork for all children. Exiting\n");
            exit(EXIT_FAILURE);
        } else if (cpid[i] == 0) {
            fprintf(stderr, "I'm the son %d...let's do some dirty work... :( \n", getpid());
            instantiate_proxy(forwarder, sockfd);
            return 1;
        } 
    }
  
    while(keep_going) {
        fprintf(stderr, "I'm the father...so...I will sleep for a while\n");                  
        sleep(13);
    }
    while ((wpid = wait(&status)) > 0)
    {
        if(WIFEXITED(status)) {
            fprintf(stderr, "Process %d had a clean exit and returned %d\n", 
                (int)wpid, WEXITSTATUS(status));
        } else {
            fprintf(stderr, "Process %d exited not due to a return call\n", 
            (int)wpid);
        }
    }
    return 1;
}


int instantiate_proxy(char* forwarder, int read_from_sockfd)
{   
    socket_pool_t* pool;
    int pool_capacity = 10;
    /* number of consecutive timeouts in order to clean used queue */
    uint32_t tmouts = 0;
    int maxfd = -1;

    fd_set rd_set;
   
    struct sockaddr_in remservaddr;
    socklen_t len;
    int rcv_bytes;
    char msg[1000];
    char msg1[1000];
    pid_t pid;

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    int n;
    int sockfd = read_from_sockfd;

    pid = getpid();

    /* forwarding socket */
    memset(&remservaddr, 0, sizeof(struct sockaddr_in));
    remservaddr.sin_family = AF_INET;
    remservaddr.sin_port = htons(53);
    // end relay socket
    if (inet_pton(AF_INET, forwarder, &remservaddr.sin_addr.s_addr) != 1) {
        perror("Cannot convert forwarder exiting\n");
        return EXIT_FAILURE;
    }

    // proxy socket
    pool = socket_pool_create(pool_capacity);
    if (!pool) {
        perror("Socket pool is null....");
        return EXIT_FAILURE;
    }

    FD_ZERO(&rd_set);
    /* set read file descriptor set for select driven loop */
    rd_set = pool->rd_set;        
    FD_SET(sockfd, &rd_set);

    maxfd = (sockfd > socket_pool_max_fd_used(pool)) ? 
        sockfd + 1: socket_pool_max_fd_used(pool) + 1;
    

    while (keep_going)
    {
        int i;
        int ret = 0;
        char straddr[INET_ADDRSTRLEN];
        
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        
        if ((ret = select(maxfd, &rd_set, NULL, NULL, &timeout)) == -1) {
            if (errno == EINTR)
                return 1;
            else {
                ERROR_MSG(stderr, "Select returned -1 %d(%s)\n", errno, strerror(errno));
                return EXIT_FAILURE;
            }
        } else if (!ret) { // timeout ;-)
            ERROR_MSG(stderr, "Timeout from child %d consecutive timeouts %u\n",
                 pid, tmouts);
            if (socket_pool_how_many_used(pool) == (socket_pool_capacity(pool) 
                - (tmouts % socket_pool_capacity(pool)))) {
                tmouts++;
                DEBUG_MSG(stderr, "\t Child %d least recently used used element\n", pid);
                    socket_pool_release(pool, pool->used_tail);
                ERROR_MSG(stderr, "\t Child %d Now %d used element left\n", pid, 
                    socket_pool_how_many_used(pool));
            }
            /* set read file descriptor set for next iteration */
            rd_set = pool->rd_set;    
            FD_SET(sockfd, &rd_set);
            // skip to next iteration
            continue;
        }

        tmouts = 0;
        // we have a new request from attackers ;-)
        if (FD_ISSET(sockfd, &rd_set)) {
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;

            conversation_t* proxy = socket_pool_acquire(pool);
            if (proxy) {
                //now relay traffic
                len = sizeof(proxy->clientaddr);
                fprintf(stderr, "Child %d want to read...\n", pid);
                sem_wait(&read_sem);
                fprintf(stderr, "\tChild %d entered read critical section...\n", pid);
                n = recvfrom(sockfd, msg, 1000, 0, (struct sockaddr *)&(proxy->clientaddr), &len);
                fprintf(stderr, "\tChild %d finished read critical section...\n", pid);
                sem_post(&read_sem);
                fprintf(stderr, "Child %d exited read critical section...\n", pid);
                inet_ntop(AF_INET, &(proxy->clientaddr.sin_addr), straddr, INET_ADDRSTRLEN);

                FD_SET(sockfd, &rd_set);

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
                } //else {
                  //  inet_ntop(AF_INET, &remservaddr.sin_addr, straddr, INET_ADDRSTRLEN);
//                    printf("Ho spedito %d bytes verso %s\n", n, straddr);
//                }
            } 
            else {
                fprintf(stderr, "We don't have elements available or pool is null discard message\n");
                FD_CLR(sockfd, &rd_set);
            }
            
        }
        // see if we can free some element 
        conversation_t* elem;
        // go from the tail to the head...tail element is the oldest...
        for (elem = pool->used_tail; elem != NULL; elem = elem->prev) {
            //printf("Child %d checking for element %d\n", pid, elem->sock_fd);
            if(FD_ISSET(elem->sock_fd, &rd_set)) {
                // we got an answer
                // end relay...give response back
                printf("Got a message from fd = %d\n", elem->sock_fd);
                printf("Now we have %d used element, %d free element\n", 
                    socket_pool_how_many_used(pool), socket_pool_how_many_free(pool));
                rcv_bytes = recvfrom(elem->sock_fd,  msg1, 1000, 0, NULL, NULL);
                // send back response and then release socket
                fprintf(stderr, "Child %d want to write...\n", pid);
                sem_wait(&write_sem);
                fprintf(stderr, "\tChild %d entered write critical section...\n", pid);
                sendto(sockfd, msg1, rcv_bytes, 0, (struct sockaddr *)&elem->clientaddr, sizeof(elem->clientaddr));
                fprintf(stderr, "\tChild %d finished write critical section...\n", pid);
                sem_post(&write_sem);
                fprintf(stderr, "Child %d exited write critical section...\n", pid);
                socket_pool_release(pool, elem);
                fprintf(stderr, "Release: Now we have %d used element, %d free element\n", 
                    socket_pool_how_many_used(pool),socket_pool_how_many_free(pool));
                //break;
            }
        }
    }
    close(sockfd);
    socket_pool_free(pool);
    return 1;
    //exit(EXIT_SUCCESS);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        FATAL_ERROR(stderr, "\nPlease run with %s <listen address> <forward address>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    fake_server(argv[1], argv[2]);
    return 1;
}

