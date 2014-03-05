#ifndef __DORAYAKI_TCP_BROKER__
#define __DORAYAKI_TCP_BROKER__

#include "socket_pool.h"

#define REQ_NEED_MORE_DATA 0
#define REQ_CAN_FORWARD 1
#define REQ_ERROR   2

typedef struct tcp_broker {
    int listen_sock; /*!< Listening tcp socket fd... */
    socket_pool_t* pool; /*!< Forwarded upd socket pool... */
    struct sockaddr_in loc_srv; /*!< Where are we are listening to...*/
    struct sockaddr_in rem_srv; /*!< Where are we sending our packets...*/
} tcp_broker_t;

//int tcp_fake_dns(int sock, char* listen_addr, char* target_addr, int how_many);
int tcp_fake_dns(tcp_broker_t* br);
int tcp_broker_initialize(tcp_broker_t* broker, int how_many, 
    char* listen_addr, char* target_addr);
#endif
