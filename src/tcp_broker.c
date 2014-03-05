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
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>


#include "debug.h"
#include "socket_pool.h"
#include "tcp_broker.h"

#define DNS_TCP_PKT_LEN 2
#define MAX_DNS_TCP 2 << 16

extern volatile sig_atomic_t keep_going;
pid_t pid;


static int handle_outside_requests(conversation_t* c);
static int forward_messages(tcp_broker_t* br, conversation_t* c);

/*! \brief Create a listening tcp socket on port 53
* 
* \param broker data structure to save all useful parameters
* \param sock share listening TCP socket
* \param listener IP where listen to
* \param target IP where we will proxy packet to
* \return EXIT_FAILURE in case of failure or EXIT_SUCCESS :)
*/
int tcp_broker_initialize(tcp_broker_t* br, int how_many, 
    char* listen_addr, char* target_addr)
{
    memset(br, 0, sizeof(tcp_broker_t));
    br->pool = socket_pool_create(how_many, true);
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
    br->listen_sock = socket(AF_INET, SOCK_STREAM, 0);
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
    
    /* FIX ME...choose apropriate size for backlog parameter */
    if (listen(br->listen_sock, 50) == -1) {
        ERROR_MSG(stderr, "Cannot complete listen...errno %d (%s)", errno, 
            strerror(errno));
        close(br->listen_sock);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/*! \brief Dispatch TCP packets coming from "evil" hackers and forward
* 
* \param br is the broker instance we would like to use :)
* \return EXIT_FAILURE in case of failure or EXIT_SUCCESS :)
*/
int tcp_fake_dns(tcp_broker_t* br)
{
    /* number of consecutive timeouts in order to clean used queue */
    uint32_t tmouts = 0;
    int max_fd = -1;
    fd_set rd_set;
    struct timeval timeout;   
    socklen_t len;
    ssize_t sent_bytes = 0, rcv_bytes = 0;

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

    while(keep_going) {
        int ret = 0;
        
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        if ((ret = select(max_fd, &rd_set, NULL, NULL, &timeout)) == -1) {
            if (errno == EINTR) {
                DEBUG_MSG(stderr, "TCP Pid: %d, select received a signal "
                    "while waiting for events, returning\n", pid);
                return EXIT_SUCCESS;
            } else {
                ERROR_MSG(stderr, "TCP Pid: %d, select returned -1 %d(%s)\n", 
                    pid, errno, strerror(errno));
                return EXIT_FAILURE;
            }
        } else if (!ret) {
            /* timeout ;-) */
            ERROR_MSG(stderr, "Timeout from TCP child %d (consecutive timeouts"
                " %u)\n", pid, tmouts);
            // FIX ME THERE IS A BUG HERE WITH TIMEOUTS (DON'T REMEMBER NOW)
            if (socket_pool_how_many_used(br->pool) == 
                (socket_pool_capacity(br->pool) - 
                (tmouts % socket_pool_capacity(br->pool)))) {
                tmouts++;
                DEBUG_MSG(stderr, "\t TCP Child %d Delete least recently used"
                    " used element\n", pid);
                socket_pool_release(br->pool, br->pool->used_tail);
                INFO_MSG(stderr, "\t TCP Child %d Now %d used element left\n",
                    pid, socket_pool_how_many_used(br->pool));
            }
            /* set read file descriptor set for next iteration as we stop here*/
            rd_set = br->pool->rd_set;    
            FD_SET(br->listen_sock, &rd_set);
            // skip to next iteration
            continue;
        }

        /* Let's do some work. We returned from select...who sent some data? */
        tmouts = 0;
        /* we have a new request from outside guests >:) */
        if (FD_ISSET(br->listen_sock, &rd_set)) {
            conversation_t* proxy = socket_pool_acquire(br->pool);
            if (proxy) {
                //now handle incoming connection traffic
                len = sizeof(proxy->enemy_addr);

                //fprintf(stderr, "TCP Child %d wants to read...\n", pid);
                //sem_wait(&read_sem);
                //fprintf(stderr, "\tTCP Child %d entered read critical section...\n", pid);
                proxy->loc_sock = accept(br->listen_sock, 
                    (struct sockaddr *)&(proxy->enemy_addr), &len);
                if (proxy->loc_sock == -1) {
                    ERROR_MSG(stderr, "TCP Child %d, error while accepting " 
                        "connection. Errorno is %d (%s)\n", pid, errno, 
                        strerror(errno));
                    /* release socket and go to next iteration */
                    socket_pool_release(br->pool, proxy);
                    continue;
                } 

                DEBUG_MSG(stderr, "TCP Child %d, got a connection from %s\n",
                    inet_ntop(AF_INET, (struct sockaddr_in*)
                    &(proxy->enemy_addr.sin_addr),straddr, INET_ADDRSTRLEN));
                //fprintf(stderr, "\tChild %d finished read critical section...\n", pid);
                //sem_post(&read_sem);
            } else {
                INFO_MSG(stderr, "We don't have elements available or pool is"
                    " null discard message\n");
                /* At least skip one iteration */
                FD_CLR(br->listen_sock, &rd_set);
                continue;
            }
        } else { /* Maybe we shoud do a check here with FD set */
            /* go from the tail to the head...tail element is the oldest...*/
            conversation_t* req = br->pool->used_tail;
            while (req) {
                if (FD_ISSET(req->loc_sock, &rd_set)) {
                    conversation_t* prev_req;
                    int ret = handle_outside_requests(req);
                    if (ret == -1) {
                        ERROR_MSG(stderr, "TCP Child %d error while handling"
                        "incoming request from sock %d\n", pid, req->loc_sock);
                        close(req->loc_sock);
                        prev_req = req->prev; 
                        socket_pool_release(br->pool, req);
                        req = prev_req;
                    } else {
                        /* forward data and update buffer offset for next read */
                        if(forward_messages(br, req) == EXIT_FAILURE) {
                            close(req->loc_sock);
                            FD_CLR(req->loc_sock, &rd_set);
                            prev_req = req->prev; 
                            socket_pool_release(br->pool, req);
                            req = prev_req;
                        } else {
                            if (req->expected_bytes == req->rcv_bytes)
                                FD_CLR(req->loc_sock, &rd_set);
                        }
                    }
                }
                req = req->prev;
            }        
        }

        /* 
        *  let's handle the comunication coming from the relay part
        *  starting from the tail (the oldest one)
        */
        conversation_t* elem = br->pool->used_tail;
        // go from the tail to the head...tail element is the oldest...
        while (elem) {
            if(FD_ISSET(elem->rem_sock, &rd_set)) {
                /* we got an answer so we will forward back to the original
                * client 
                */
                DEBUG_MSG(stderr, "TCP Child %d Got a message from fd = %d\n", 
                    pid, elem->rem_sock);
                /* Read response */
                rcv_bytes = recv(elem->rem_sock, elem->buff, elem->rcv_bytes, 0);
                if(rcv_bytes != -1 && rcv_bytes != elem->rcv_bytes)
                    continue;
                // send back response and then release socket
                DEBUG_MSG(stderr, "TCP Child %d wants to write back...\n", pid);
                sent_bytes = send(elem->loc_sock, elem->buff, rcv_bytes, 0);
                if(sent_bytes == -1) {
                    ERROR_MSG(stderr, "TCP Child %d: Error while relaying TCP" 
                        " packet. Error %s (%d)", pid, strerror(errno), errno);
                } else if(sent_bytes != rcv_bytes) {
                    ERROR_MSG(stderr, "TCP Child %d: An error occurred while "
                        "relaying TCP packet. Sent only %d/%d\n",
                        pid, sent_bytes, rcv_bytes);
                } 
                DEBUG_MSG(stderr, "TCP Child %d: Successfull relayed %d bytes "
                    "to %s\n", pid, sent_bytes, 
                    inet_ntop(AF_INET, &(br->enemy_addr.sin_addr), 
                    straddr, INET_ADDRSTRLEN))
                close(elem->loc_sock);
                close(elem->rem_sock);
                socket_pool_release(br->pool, elem);
                DEBUG_MSG(stderr, "TCP Child %d: Release element: Now we have "
                    "%d used element, %d free element\n", pid,
                    socket_pool_how_many_used(br->pool),
                    socket_pool_how_many_free(br->pool));
                /* Adding a break here will make the worker handle only one
                * request per "select" loop
                */
                break;
            }
        }
        // FIX ME...WE NEED TO HANLDE ALL THE *->loc_sock waiting for some more
        // data to come from the evil hacker...

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


int handle_outside_requests(conversation_t* c)
{
    int res = 0;
    if (!c)
        return EXIT_FAILURE;

    /* we will try to read as much as we can...*/
    res = recv(c->loc_sock, &c->buff[c->offset], MAX_DNS_TCP, MSG_DONTWAIT);
    if(res == -1) {
        /* something went wrong...shouldn't be actually */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            DEBUG_MSG(stderr, "TCP Child %d, no data available from socket %d"
                "...strange as we had been notified data were available", pid, 
                c->loc_sock);
            return EXIT_SUCCESS;
        } 
        DEBUG_MSG(stderr, "TCP Child %d, can't read any data from socket " 
            "%d we will remove shut down this stream\n", pid, c->loc_sock);
        return EXIT_FAILURE;
    } 

    c->rcv_bytes += res;
    /* let's check how many bytes we read if we have less than 2 bytes
    * or not complete DNS packet len */
    if (c->offset < DNS_TCP_PKT_LEN && res >= DNS_TCP_PKT_LEN) {
        c->expected_bytes = ntohs(*(uint16_t*)(c->buff));
        if (res < c->expected_bytes) {
            DEBUG_MSG(stderr, "TCP Child %d: need more data from %d\n", pid, 
                c->loc_sock);
            return EXIT_SUCCESS;
        }
    }
    return EXIT_SUCCESS;
}


int forward_messages(tcp_broker_t* br, conversation_t* c)
{
    int sent_bytes = 0;
    int limit;
    int len;
    char straddr[INET_ADDRSTRLEN];
    int partial = 0;

    int i;

    if(!br || !c) {
        ERROR_MSG(stderr, "TCP Child %d, can't forward message to remote server" 
            " (%s), as either broker (%p) or conversation (%p) is null\n ", pid, 
            inet_ntop(AF_INET, &(br->rem_srv.sin_addr), straddr, 
                INET_ADDRSTRLEN), br, c);
        return EXIT_FAILURE;
    }

    if (c->rcv_bytes == c->offset)
        limit = c->rcv_bytes;
    else 
        limit = c->rcv_bytes - c->offset;

    /* FIX ME: REMOVE ALL THIS CRAP*/
    printf("-------------------------------------------------------\n");
    for (i = 0; i < limit; i++)
        printf("%02X", c->buff[i] & 0xFF);
    printf("\n");
    printf("-------------------------------------------------------\n");

    // FIX ME...in pool we have a fwd_addr never used!!
    /* we can finally proxy our request */
    len = sizeof(br->rem_srv);
    if ((connect(c->rem_sock, (struct sockaddr*)&(br->rem_srv), len)) == -1) {
        ERROR_MSG(stderr, "TCP Child %d, can't connect to remote server (%s)."
            " Errno %d (%s)\n ", pid, inet_ntop(AF_INET, 
            &(br->rem_srv.sin_addr), straddr, INET_ADDRSTRLEN), 
            errno, strerror(errno));
        /* release resources */
        return EXIT_FAILURE;
        
    }
    DEBUG_MSG(stderr, "TCP Child %d, connected to %s\n",
        inet_ntop(AF_INET, (struct sockaddr_in*)&(proxy->rem_srv.sin_addr), 
        straddr, INET_ADDRSTRLEN));
    /* sending data */
    sent_bytes = write(c->rem_sock, &(c->buff[c->offset]), limit);
    DEBUG_MSG(stderr, "TCP Child %d, sent %d/%d bytes to %s\n", pid, sent_bytes, 
        proxy->read_bytes, inet_ntop(AF_INET, 
        (struct sockaddr_in*)&(proxy->rem_srv.sin_addr),
        straddr, INET_ADDRSTRLEN));

    if (sent_bytes && (sent_bytes == limit)) {
        c->offset += sent_bytes;
        return EXIT_SUCCESS;
    }

    /* For some reasons we couln't send all data with 1 write...let's finish */
    do {
        partial = write(c->rem_sock, &(c->buff[sent_bytes - 1]), 
            limit - sent_bytes);
        if(partial == -1) {
            ERROR_MSG(stderr, "TCP Child %d, error while sending partial. "
                "Errno %d (%s)\n", pid, errno, strerror(errno));
            return EXIT_FAILURE;
        }
        sent_bytes += partial;
        c->offset += partial;
    } while(sent_bytes != limit);

    return EXIT_SUCCESS;
}
