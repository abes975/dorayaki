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
#include "tcp_broker.h"

/*! \brief Create a listening tcp socket and return file descriptor
* 
* \param broker data structure to save all useful parameters
* \param listener IP where listen to
* \param target IP where we will proxy packet to
* \return the listening socket's file descriptor or -1 if something was wrong
*/
int tcp_broker_listener(tcp_broker_t* br, char* listener, char* target)
{
    br->listen_sock = -1;
    /* FIX ME...maybe we will change hereafter...now listening port is fixed */
    int port = 53;

    /* listening udp socket */
    br->loc_srv.sin_family = AF_INET;
    br->loc_srv.sin_port=htons(port);
  
    if(!listener) {
        ERROR_MSG(stderr, "Listener is null (%p)", listener);
        return -1;
    }

    if (inet_pton(AF_INET, listener, &(br->loc_srv.sin_addr.s_addr)) != 1) {
        ERROR_MSG(stderr, "Cannot convert listener (%s) into a valid IPv4 "
            "address\n", listener);
        return -1;
    }

    /* save forward remote server address*/
    br->rem_srv.sin_family = AF_INET;
    br->rem_srv.sin_port=htons(port);

    if (!target) {
        ERROR_MSG(stderr, "Forward target is null (%p)", target);
        return -1;
    }

    if (inet_pton(AF_INET, target, &br->rem_srv.sin_addr.s_addr) != 1) {
        ERROR_MSG(stderr, "Cannot convert forwarder (%s) into a valid IPv4 "
            "address\n", target);
    }

    /* this is the listening server (local) socket */
    br->listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (br->listen_sock == -1) {
        ERROR_MSG(stderr, "Cannot allocate listening socket\n");
        return -1;
    }

    if (bind(br->listen_sock, (struct sockaddr *)&(br->loc_srv), 
        sizeof(br->loc_srv)) == -1) {
            ERROR_MSG(stderr, "Cannot complete bind...errno %d (%s)", errno, 
                strerror(errno));
            close(br->listen_sock);
            return -1;
    }

    return br->listen_sock;
}


int tcp_broker_create(tcp_broker_t* br, int how_many)
{
    memset(br, 0, sizeof(tcp_broker_t));
    if(!br->pool)
        br->pool = socket_pool_create(how_many, true);
    if(!br->pool) {
        ERROR_MSG(stderr, "Cannot allocate tcp socket pool\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
