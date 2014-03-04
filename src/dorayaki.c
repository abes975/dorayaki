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

#define WORKER_UDP 0
#define WORKER_TCP 1

#define PER_WORKER_UDP_CAPACITY 10
#define PER_WORKER_TCP_CAPACITY 2

volatile sig_atomic_t keep_going = 1;
static int mitosis(int worker_udp, int worker_tcp, udp_broker_t* udpb, 
    tcp_broker_t* tcpb);
void break_loop(int signum);

sem_t read_sem;
sem_t write_sem;

int main(int argc, char** argv)
{
    udp_broker_t udpb;
    tcp_broker_t tcpb;
    int res;
    /* FIX ME!!! Use getopt!!! */
    if (argc < 3) {
        FATAL_ERROR(stderr, EXIT_FAILURE, "\nPlease run with %s "
            "<listen udp address> <forward udp address>\n", argv[0]);
    }
    /* Create listening sockets before calling fork */
    /* this is the listening UDP server (local) socket */
    res = udp_broker_initialize(&udpb, PER_WORKER_UDP_CAPACITY, argv[1], argv[2]);
    if (res == EXIT_FAILURE) 
        FATAL_ERROR(stderr, EXIT_FAILURE, "Error while initializing udp_broker\n");
    res = tcp_broker_initialize(&tcpb, PER_WORKER_TCP_CAPACITY, argv[1], argv[2]);
    if (res == EXIT_FAILURE)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Error while initializing tcp_broker\n");

    /* do some fish and bread multiplication ;- */
    mitosis(WORKER_UDP, WORKER_TCP, &udpb, &tcpb);
    close(udpb.listen_sock);
    close(tcpb.listen_sock);
    return EXIT_SUCCESS;
    
}

void break_loop(int sig_num)
{
    keep_going = 0;
    printf("Break loop chiamata\n");
    signal(SIGINT, break_loop);
}

int mitosis(int worker_udp, int worker_tcp, udp_broker_t* udpb, tcp_broker_t* tcpb)
{
    /* Need to be changed if we want to support this parameter to be configured */
    /* Don't like this kind of dynamic array ;-) */
    pid_t udp_cpid[WORKER_UDP];
    pid_t tcp_cpid[WORKER_TCP];
    pid_t wpid;
    int status = -1;
    int i;

    /* very nice signal handler to break endless loop */
    signal(SIGINT, break_loop);

    /* Probably they will be used only with TCP */
    if(sem_init(&read_sem, 1, 1) < 0)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Read semaphore initilization");

    if(sem_init(&write_sem, 1, 1) < 0)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Write semaphore initilization");
    /* END SEMAPHORE */

    for (i = 0; i < worker_udp ; i++) {
        DEBUG_MSG(stderr, "Ready to create UDP worker #%d/%d\n", i, WORKER_UDP);
        if ((udp_cpid[i] = fork()) == - 1) {
            FATAL_ERROR(stderr, EXIT_FAILURE, "Cannot complete fork"
                " for UDP child (%d). Exiting\n", i);
        } else if (udp_cpid[i] == 0) {
            INFO_MSG(stderr, "I'm the UDP son %d...Faithful and ready to serve "
                "you...:(\n", getpid());
            udp_fake_dns(udpb);
            return EXIT_SUCCESS;
        } 
    }
  
    for (i = 0; i < worker_tcp; i++) {
        if ((tcp_cpid[i] = fork()) == - 1) {
            FATAL_ERROR(stderr, EXIT_FAILURE, "Cannot complete fork"
                " for TCP child (%d). Exiting\n", i);
        } else if (tcp_cpid[i] == 0) {
            INFO_MSG(stderr, "I'm the TCP son %d...Faithful and ready to serve "
                "you...:(\n", getpid());
            tcp_fake_dns(tcpb);
            return EXIT_SUCCESS;
        } 
    }

    // FIX ME...hereafter will be substituted by father "manager" ;-))
    while(keep_going) {
        INFO_MSG(stderr, "I'm the father...so...I will sleep for a while\n");                  
        sleep(15);
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
