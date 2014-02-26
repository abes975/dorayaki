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

#include "debug.h"
#include "socket_pool.h"
#include "udp_broker.h"
#include "tcp_broker.h"

#define MAX_SONS 10


volatile sig_atomic_t keep_going = 1;
static int mitosis(int max_children, udp_broker_t* udpb, tcp_broker_t* tcpb);
static int fake_dns(udp_broker_t* udpb, tcp_broker_t* tcpb);
void break_loop(int signum);

sem_t read_sem;
sem_t write_sem;

int main(int argc, char** argv)
{
    udp_broker_t udp_broker;
    tcp_broker_t tcp_broker;
    
    /* FIX ME!!! Use getopt!!! */
    if (argc < 3) {
        FATAL_ERROR(stderr, EXIT_FAILURE, "\nPlease run with %s "
            "<listen udp address> <forward udp address>\n", argv[0]);
    }
    
    /* FIX ME...maybe these calls can be moved inside mitosis */
    if(udp_broker_create(&udp_broker, 10) == EXIT_FAILURE)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Cannot create udp socket pool for "
            "UDP server...giving up\n");
   
    if(udp_broker_listener(&udp_broker, argv[1], argv[2]) == -1)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Cannot create listening "
            "UDP server...giving up\n");

    if(tcp_broker_create(&tcp_broker, 10) == EXIT_FAILURE)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Cannot create tcp socket pool for "
            "TCP server...giving up\n");
   
    if(tcp_broker_listener(&tcp_broker, argv[1], argv[2]) == -1)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Cannot create listening "
            "TCP server...giving up\n");
 
    
    /* do some fish and bread multiplication ;- */
    mitosis(MAX_SONS, &udp_broker, &tcp_broker);
    return EXIT_SUCCESS;
}


void break_loop(int sig_num)
{
    keep_going = 0;
    printf("Break loop chiamata\n");
    signal(SIGINT, break_loop);
}

int mitosis(int max_children, udp_broker_t* udpb, tcp_broker_t* tcpb)
{
    pid_t cpid[MAX_SONS];
    pid_t wpid;
    int status = -1;
    int i;

    if(sem_init(&read_sem, 1, 1) < 0)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Read udp semaphore initilization");

    if(sem_init(&write_sem, 1, 1) < 0)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Write udp semaphore initilization");

    /* very nice signal handler to break endless loop */
    signal(SIGINT, break_loop);

    for (i = 0; i < max_children; i++) {
        
        if ((cpid[i] = fork()) == - 1) {
            FATAL_ERROR(stderr, EXIT_FAILURE, "Cannot complete fork"
                " for child (%d). Exiting\n", i);
        } else if (cpid[i] == 0) {
            INFO_MSG(stderr, "I'm the son %d...Faithfull and ready to serve "
                "you...:(\n", getpid());
            fake_dns(udpb, tcpb);
            return EXIT_SUCCESS;
        } 
    }
  
    // FIX ME...hereafter will be substituted by father "manager" ;-))
    while(keep_going) {
        INFO_MSG(stderr, "I'm the father...so...I will sleep for a while\n");                  
        sleep(13);
    }
    
    while ((wpid = wait(&status)) > 0)
    {
        if(WIFEXITED(status)) {
            INFO_MSG(stderr, "Process %d had a clean exit and returned %d\n", 
                (int)wpid, WEXITSTATUS(status));
        } else {
            INFO_MSG(stderr, "Process %d exited not due to a return call\n", 
            (int)wpid);
        }
    }
    return EXIT_SUCCESS;
}


int fake_dns(udp_broker_t* udpb, tcp_broker_t* tcpb)
{
    /* number of consecutive timeouts in order to clean used queue */
    uint32_t tmouts = 0;
    int max_fd = -1, max_pool, max_listen;
    pid_t pid;
    fd_set udp_rd_set, tcp_rd_set, merge_set, rd_set;
    int fd_cursor;    
   
    socklen_t len;
    ssize_t sent_bytes, rcv_bytes;
    // FIX ME Not a good idea here
    char req[MAX_DNS_UDP];
    char resp[MAX_DNS_UDP];

#ifdef DORAYAKI_DEBUG
    char straddr[INET_ADDRSTRLEN];
#endif

    struct timeval timeout;

    pid = getpid();

    /* This part is here to be used with select...if epool will be used..
    *   Probaly will be removed soon ;-)
    */
    FD_ZERO(&udp_rd_set);
    FD_ZERO(&tcp_rd_set);
    /* set read file descriptor set for select driven loop */
    udp_rd_set = udpb->pool->rd_set;
    tcp_rd_set = tcpb->pool->rd_set;
    /* Let's merge them all */
    FD_ZERO(&rd_set);
    FD_ZERO(&merge_set);
    for (fd_cursor = 0; fd_cursor < FD_SETSIZE; fd_cursor++) {
        if (FD_ISSET(fd_cursor, &udp_rd_set) || 
            FD_ISSET(fd_cursor, &tcp_rd_set)) {
                FD_SET(fd_cursor, &merge_set);
        }
    }
    
    rd_set = merge_set;
    FD_SET(udpb->listen_sock, &rd_set);
    FD_SET(tcpb->listen_sock, &rd_set);
    max_pool = (udpb->pool->max_fd > tcpb->pool->max_fd) ? udpb->pool->max_fd :
        tcpb->pool->max_fd;
    max_listen = (udpb->listen_sock > tcpb->listen_sock) ? udpb->listen_sock :
        tcpb->listen_sock;
    max_fd = (max_listen > max_pool) ? 
        max_listen + 1: max_pool + 1;

    while (keep_going)
    {
        int i;
        int ret = 0;
        
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        
        if ((ret = select(max_fd, &rd_set, NULL, NULL, &timeout)) == -1) {
            if (errno == EINTR) {
                DEBUG_MSG(stderr, "Pid: %d, select received a signal "
                    "while waiting for events, returning\n", pid);
                return EXIT_SUCCESS;
            }
            else {
                ERROR_MSG(stderr, "Select returned -1 %d(%s)\n", errno, 
                    strerror(errno));
                return EXIT_FAILURE;
            }
        } else if (!ret) { 
            /* timeout ;-) */
            INFO_MSG(stderr, "Timeout from child %d (consecutive timeouts %u)\n",
                 pid, tmouts);
            if (socket_pool_how_many_used(udpb->pool) == 
                (socket_pool_capacity(udpb->pool) - 
                (tmouts % socket_pool_capacity(udpb->pool)))) {
                tmouts++;
                DEBUG_MSG(stderr, "\t Child %d Delete least recently used UDP"
                    " used element\n", pid);
                socket_pool_release(udpb->pool, udpb->pool->used_tail);
                INFO_MSG(stderr, "\t Child %d Now %d UDP used element left\n",
                     pid, socket_pool_how_many_used(udpb->pool));
            }

            if (socket_pool_how_many_used(tcpb->pool) == 
                (socket_pool_capacity(tcpb->pool) - 
                (tmouts % socket_pool_capacity(tcpb->pool)))) {
                tmouts++;
                DEBUG_MSG(stderr, "\t Child %d Delete least recently used TCP"
                    " used element\n", pid);
                socket_pool_release(tcpb->pool, tcpb->pool->used_tail);
                INFO_MSG(stderr, "\t Child %d Now %d TCP used element left\n", 
                    pid, socket_pool_how_many_used(tcpb->pool));
            }
            /* set read file descriptor set for next iteration as we stop here*/
            rd_set = merge_set;    
            FD_SET(udpb->listen_sock, &rd_set);
            FD_SET(tcpb->listen_sock, &rd_set);
            // skip to next iteration
            continue;
        }

        tmouts = 0;
        /* Set for next iteration */
        rd_set = merge_set;    
        FD_SET(udpb->listen_sock, &rd_set);
        FD_SET(tcpb->listen_sock, &rd_set);

        // we have a new UDP request from outside guests >:)
        /* HANDLE UDP REQUEST */
        if (FD_ISSET(udpb->listen_sock, &rd_set)) {
            /* Set for next iteration */
            rd_set = udpb->pool->rd_set;    
            FD_SET(udpb->listen_sock, &rd_set);
            FD_SET(tcpb->listen_sock, &rd_set);

            conversation_t* proxy = socket_pool_acquire(udpb->pool);
            if (proxy) {
                //now relay traffic
                len = sizeof(proxy->enemy_addr);
                DEBUG_MSG(stderr, "Child %d wants to read UDP...\n", pid);

                /* Critical Section */ 
                /* Probably OS should take care of this critical section as
                * we are dealing here with UDP sockets
                */
                sem_wait(&read_sem);
                DEBUG_MSG(stderr, "\tChild %d entered read critical"
                    " section...\n", pid);
                rcv_bytes = recvfrom(udpb->listen_sock, req, MAX_DNS_UDP, 0, 
                    (struct sockaddr *)&(proxy->enemy_addr), &len);
                DEBUG_MSG(stderr, "\tChild %d finished reading critical"
                    " section...%d bytes read\n", pid, rcv_bytes);
                sem_post(&read_sem);
                /* End Critical Section */

                DEBUG_MSG(stderr, "Child %d exited read critical" 
                    " section...received data from %s\n", pid,
                    inet_ntop(AF_INET, &(proxy->enemy_addr.sin_addr), 
                    straddr, INET_ADDRSTRLEN));
                    
                /* FIX ME: REMOVE ALL THIS CRAP*/
                printf("---------------------------------------------------\n");
                //req[rcv_bytes] = '\0';
                for (i = 0; i < rcv_bytes; i++)
                    printf("%2X", (int)req[i]);
                printf("\n");
                printf("---------------------------------------------------\n");

                sent_bytes = sendto(proxy->rem_sock, req, rcv_bytes, 0, 
                    (struct sockaddr *)&(udpb->rem_srv), sizeof(udpb->rem_srv));
                if(sent_bytes == -1) {
                    ERROR_MSG(stderr, "Child %d: Error while relaying udp" 
                        " packet. Error %s (%d)", pid, strerror(errno), errno);
                    continue;
                } else if(sent_bytes != rcv_bytes) {
                    ERROR_MSG(stderr, "Child %d: An error occurred while "
                        "relaying udp packet Sent only %d/%d\n",
                        pid, sent_bytes, rcv_bytes);
                    continue;
                } 
                DEBUG_MSG(stderr, "Successfull relayed %d bytes to %s\n", \
                    sent_bytes, inet_ntop(AF_INET, &udpb->rem_srv.sin_addr, 
                    straddr, INET_ADDRSTRLEN));
            } else {
                INFO_MSG(stderr, "We don't have elements available or pool is"
                    " null discard message\n");
                /* At least skip one iteration with this file descriptor in 
                * in oder to prevent client requests to being discarded
                */
                FD_CLR(udpb->listen_sock, &rd_set);
            }
        /* END HANDLE UDP REQUEST */
        } else if (FD_ISSET(tcpb->listen_sock, &rd_set)) {
            conversation_t* proxy = socket_pool_acquire(tcpb->pool);
            if (proxy) {
                //now relay traffic
                len = sizeof(proxy->enemy_addr);
                DEBUG_MSG(stderr, "Child %d wants to read TCP...\n", pid);
                /* Critical Section */ 
            
            /* HANDLE_TCP_REQUEST */
            /* READ */
            continue;
            }
        }


        /* let's handle the comunication coming from the relay part
        *  starting from the tail (the oldest one)
        */
        conversation_t* elem;
        /* go from the tail to the head...tail element is the oldest... */
        for (elem = udpb->pool->used_tail; elem != NULL; elem = elem->prev) {
            if(FD_ISSET(elem->loc_sock, &rd_set)) {
                /* we got an answer so we will forward back to the original
                * client 
                */
                DEBUG_MSG(stderr, "Child %d Got a UDP message from fd = %d\n", pid, 
                    elem->loc_sock);
                /* FIX ME size of msg1 */
                rcv_bytes = recvfrom(elem->loc_sock, resp, MAX_DNS_UDP, 
                    0, NULL, NULL);
                // send back response and then release socket
                DEBUG_MSG(stderr, "Child %d wants to write UDP back ...\n", pid);
              
                /* Critical Section */
                //sem_wait(&write_sem);
                DEBUG_MSG(stderr, "\tChild %d entered write critical"
                    " section...\n", pid);
                sent_bytes = sendto(elem->rem_sock, resp, rcv_bytes, 0, 
                    (struct sockaddr *)&elem->enemy_addr, 
                        sizeof(elem->enemy_addr));
                DEBUG_MSG(stderr, "\tChild %d finished write critical"
                    " section...\n", pid);
                //sem_post(&write_sem);
                /* End of Critical Section */

                DEBUG_MSG(stderr, "Child %d exited write critical"
                    " section...\n", pid);

                if(sent_bytes == -1) {
                    ERROR_MSG(stderr, "Child %d: Error while relaying udp" 
                        " packet. Error %s (%d)", pid, strerror(errno), errno);
                } else if(sent_bytes != rcv_bytes) {
                    ERROR_MSG(stderr, "Child %d: An error occurred while "
                        "relaying udp packet Sent only %d/%d\n",
                        pid, sent_bytes, rcv_bytes);
                } 
                DEBUG_MSG(stderr, "Successfull relayed %d bytes to %s\n", 
                    sent_bytes, inet_ntop(AF_INET, &elem->enemy_addr.sin_addr, 
                    straddr, INET_ADDRSTRLEN));
               
                socket_pool_release(udpb->pool, elem);
                DEBUG_MSG(stderr, "Release element: Now we have %d used"
                    " element, %d free element\n", 
                    socket_pool_how_many_used(udpb->pool),
                    socket_pool_how_many_free(udpb->pool));
                /* Adding a break here will make the worker handle only one
                * request per "select" loop
                */
                //break;
            }
        }
    }
    close(udpb->listen_sock);
    close(tcpb->listen_sock);
    socket_pool_free(udpb->pool);
    socket_pool_free(tcpb->pool);
    return EXIT_SUCCESS;
}
