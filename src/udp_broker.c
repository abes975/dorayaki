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

extern volatile sig_atomic_t keep_going;

/*! \brief initialize udp broker
* 
* \param broker data structure to save all useful parameters
* \param how_many is the capacity for the socket pool
* \param listen_addr IP where listen to
* \param target_addr IP where we will forward packets to
* \return EXIT_FAILURE in case of failure or EXIT_SUCCESS :)
*/
int udp_broker_initialize(udp_broker_t* br, int how_many, 
    char* listen_addr, char* target_addr)
{
    memset(br, 0, sizeof(udp_broker_t));
    if(!br->pool)
        br->pool = socket_pool_create(how_many, false);
    if(!br->pool) {
        ERROR_MSG(stderr, "Cannot allocate udp socket pool\n");
        return EXIT_FAILURE;
    }

    /* FIX ME...maybe we will change hereafter...now listening port is fixed */
    int port = 53;

    /* listening udp socket */
    br->loc_srv.sin_family = AF_INET;
    br->loc_srv.sin_port=htons(port);
  
    if(!listen_addr) {
        ERROR_MSG(stderr, "Listener is null (%p)", listen_addr);
        return EXIT_FAILURE;
    }

    if (inet_pton(AF_INET, listen_addr, &(br->loc_srv.sin_addr.s_addr)) != 1) {
        ERROR_MSG(stderr, "Cannot convert listener (%s) into a valid IPv4 "
            "address\n", listen_addr);
        return EXIT_FAILURE;
    }

    /* save forward remote server address*/
    br->rem_srv.sin_family = AF_INET;
    br->rem_srv.sin_port=htons(port);

    if (!target_addr) {
        ERROR_MSG(stderr, "Forward target is null (%p)", target_addr);
        return EXIT_FAILURE;
    }

    if (inet_pton(AF_INET, target_addr, &br->rem_srv.sin_addr.s_addr) != 1) {
        ERROR_MSG(stderr, "Cannot convert forwarder (%s) into a valid IPv4 "
            "address\n", target_addr);
    }

    /* this is the listening server (local) socket */
    br->listen_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (br->listen_sock == -1) {
        ERROR_MSG(stderr, "Cannot allocate listening socket\n");
        return EXIT_FAILURE;
    }

    if (bind(br->listen_sock, (struct sockaddr *)&(br->loc_srv), 
        sizeof(br->loc_srv)) == -1) {
            ERROR_MSG(stderr, "Cannot complete bind...errno %d (%s)", errno, 
                strerror(errno));
            close(br->listen_sock);
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*! \brief Dispatch UDP packets coming from "evil" hackers and forward
*
* \param br is the udb broker instance we would like to use :)
* \return EXIT_FAILURE in case of failure or EXIT_SUCCESS :)
*/
int udp_fake_dns(udp_broker_t* br)
{
    pid_t pid;

    /* number of consecutive timeouts in order to clean used queue */
    uint32_t tmouts = 0;
    int max_fd = -1;
    fd_set rd_set;
    struct timeval timeout;   
    socklen_t len;
    ssize_t sent_bytes, rcv_bytes;

    // FIX ME Not a good idea here
    char req[MAX_DNS_UDP];
    char resp[MAX_DNS_UDP];
    memset(req, 0, MAX_DNS_UDP);
    memset(resp, 0, MAX_DNS_UDP);
#ifdef DORAYAKI_DEBUG
    char straddr[INET_ADDRSTRLEN];
#endif
    pid = getpid();
    
    /* This part is here to be used with select...what if epool is used..
    *   Probaly will be removed soon ;-)
    */
    FD_ZERO(&rd_set);
    /* set read file descriptor set for select driven loop */
    rd_set = br->pool->rd_set;
    FD_SET(br->listen_sock, &rd_set);

    max_fd = (br->listen_sock > socket_pool_max_fd_used(br->pool)) ?
         br->listen_sock + 1: socket_pool_max_fd_used(br->pool) + 1;

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
            } else {
                ERROR_MSG(stderr, "Pid: %d, select returned -1 %d(%s)\n", pid, 
                    errno, strerror(errno));
                return EXIT_FAILURE;
            }
        } else if (!ret) {
            /* timeout ;-) */
            ERROR_MSG(stderr, "Timeout from child %d consecutive timeouts %u\n",
                 pid, tmouts);
            // FIX ME THERE IS A BUG HERE WITH TIMEOUTS (BUT I DON'T REMEMEBER IT NOW)
            if (socket_pool_how_many_used(br->pool) == 
                (socket_pool_capacity(br->pool) - 
                (tmouts % socket_pool_capacity(br->pool)))) {
                tmouts++;
                DEBUG_MSG(stderr, "\t Child %d Delete least recently used"
                    " used element\n", pid);
                socket_pool_release(br->pool, br->pool->used_tail);
                INFO_MSG(stderr, "\t Child %d Now %d used element left\n", pid, 
                    socket_pool_how_many_used(br->pool));
            }
            /* set read file descriptor set for next iteration as we stop here*/
            rd_set = br->pool->rd_set;    
            FD_SET(br->listen_sock, &rd_set);
            // skip to next iteration
            continue;
        }
        
        /* Let's do some work..as we returned from select..who sent some data? */
        tmouts = 0;
        /* we have a new request from outside guests >:) */
        if (FD_ISSET(br->listen_sock, &rd_set)) {
            conversation_t* proxy = socket_pool_acquire(br->pool);
            if (proxy) {
                //now relay traffic
                len = sizeof(proxy->enemy_addr);

                //fprintf(stderr, "Child %d wants to read...\n", pid);
                //sem_wait(&read_sem);
                //fprintf(stderr, "\tChild %d entered read critical section...\n", pid);
                rcv_bytes = recvfrom(br->listen_sock, req, MAX_DNS_UDP, 0, 
                    (struct sockaddr *)&(proxy->enemy_addr), &len);
                //fprintf(stderr, "\tChild %d finished read critical section...\n", pid);
                //sem_post(&read_sem);

                DEBUG_MSG(stderr, "Child %d finished read section" 
                    "...received data from %s\n", pid,
                    inet_ntop(AF_INET, &(proxy->enemy_addr.sin_addr), 
                    straddr, INET_ADDRSTRLEN));

                /* FIX ME: REMOVE ALL THIS CRAP*/
                printf("-------------------------------------------------------\n");
                //req[rcv_bytes] = '\0';
                for (i = 0; i < rcv_bytes; i++)
                    printf("%2X", (int)req[i]);
                printf("\n");
                printf("-------------------------------------------------------\n");

                sent_bytes = sendto(proxy->loc_sock, req, rcv_bytes, 0, 
                    (struct sockaddr *)&(br->rem_srv), sizeof(br->rem_srv));
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
                DEBUG_MSG(stderr, "Child %d: Successfull relayed %d bytes to %s\n", 
                    pid, sent_bytes, inet_ntop(AF_INET, &br->rem_srv.sin_addr, 
                    straddr, INET_ADDRSTRLEN));
            } else {
                INFO_MSG(stderr, "We don't have elements available or pool is"
                    " null discard message\n");
                /* At least skip one iteration with this file descriptor in 
                * in oder to prevent client requests to being discarded
                */
                FD_CLR(br->listen_sock, &rd_set);
            }
        }

        /* 
        *  let's handle the comunication coming from the relay part
        *  starting from the tail (the oldest one)
        */
        conversation_t* elem;
        // go from the tail to the head...tail element is the oldest...
        for (elem = br->pool->used_tail; elem != NULL; elem = elem->prev) {
            //printf("Child %d checking for element %d\n", pid, elem->sock_fd);
            if(FD_ISSET(elem->loc_sock, &rd_set)) {
                /* we got an answer so we will forward back to the original
                * client 
                */
                DEBUG_MSG(stderr, "Child %d Got a message from fd = %d\n", pid, 
                    elem->loc_sock);
                /* Read response */
                rcv_bytes = recvfrom(elem->loc_sock, resp, MAX_DNS_UDP, 
                    0, NULL, NULL);
                // send back response and then release socket
                DEBUG_MSG(stderr, "Child %d wants to write back...\n", pid);
              
                /* 
                 * I don't think lock is needed as op system should be take 
                 * care of this synchronization. Probably br->listen_sock could
                 * be safely used....
                */
                //sem_wait(&write_sem);
                //DEBUG_MSG(stderr, "\tChild %d entered write critical"
                //    " section...\n", pid);
                sent_bytes = sendto(br->listen_sock, resp, rcv_bytes, 0, 
                    (struct sockaddr *)&elem->enemy_addr, 
                    sizeof(elem->enemy_addr));
                //DEBUG_MSG(stderr, "\tChild %d finished write critical"
                //    " section...\n", pid);
                //sem_post(&write_sem);
                /* End of Critical Section */
                //DEBUG_MSG(stderr, "Child %d exited write critical"
                //    " section...\n", pid);

                if(sent_bytes == -1) {
                    ERROR_MSG(stderr, "Child %d: Error while relaying udp" 
                        " packet. Error %s (%d)", pid, strerror(errno), errno);
                } else if(sent_bytes != rcv_bytes) {
                    ERROR_MSG(stderr, "Child %d: An error occurred while "
                        "relaying UDP packet. Sent only %d/%d\n",
                        pid, sent_bytes, rcv_bytes);
                } 
                DEBUG_MSG(stderr, "Child %d: Successfull relayed %d bytes "
                    "to %s\n", pid, sent_bytes, 
                    inet_ntop(AF_INET, &(elem->enemy_addr.sin_addr), 
                    straddr, INET_ADDRSTRLEN))
               
                socket_pool_release(br->pool, elem);
                DEBUG_MSG(stderr, "Child %d: Release element: Now we have "
                    "%d used element, %d free element\n", pid,
                    socket_pool_how_many_used(br->pool),
                    socket_pool_how_many_free(br->pool));
                /* Adding a break here will make the worker handle only one
                * request per "select" loop
                */
                break;
            }
        }
        /* Set for next iteration */
        rd_set = br->pool->rd_set;    
        FD_SET(br->listen_sock, &rd_set);

        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

    }
    close(br->listen_sock);
    socket_pool_free(br->pool);
    return EXIT_SUCCESS;
}
