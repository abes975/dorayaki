#ifndef __DORAYAKI_TCP_BROKER__
#define __DORAYAKI_TCP_BROKER__

#include "socket_pool.h"

typedef struct tcp_broker {
    int listen_sock; /*!< Listening tcp socket fd... */
    socket_pool_t* pool; /*!< Forwarded upd socket pool... */
    struct sockaddr_in loc_srv; /*!< Where are we are listening to...*/
    struct sockaddr_in rem_srv; /*!< Where are we sending our packets...*/
} tcp_broker_t;

int tcp_broker_listener(tcp_broker_t* broker, char* listener, char* target);
int tcp_broker_create(tcp_broker_t* broker, int how_many);
#endif
