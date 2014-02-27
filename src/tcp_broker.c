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

extern volatile sig_atomic_t keep_going;

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

    return EXIT_SUCCESS;
}

/*! \brief Dispatch TCP packets coming from "evil" hackers and forward
* 
* \param sock share listening TCP socket
* \param listen_addr is the listening ip address
* \param target_addr is where we forward received packets
* \param how_many how many request can we have pending simultaneously...
* \return EXIT_FAILURE in case of failure or EXIT_SUCCESS :)
*/
int tcp_fake_dns(tcp_broker_t* br)
{
    pid_t pid;

    pid = getpid();
    while(keep_going) {
        INFO_MSG(stderr, "Child %d:%p I'the TCP child running\n", pid, br)
        sleep(10);
    }
    return EXIT_SUCCESS;
}
