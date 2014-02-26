#ifndef __DORAYAKI_UDP_BROKER__
#define __DORAYAKI_UDP_BROKER__

#include "socket_pool.h"
#define MAX_DNS_UDP 2 >> 12

typedef struct udp_broker {
    int listen_sock; /*!< Listening upd socket fd... */
    socket_pool_t* pool; /*!< Forwarded upd socket pool... */
    struct sockaddr_in loc_srv; /*!< Where are we are listening to...*/
    struct sockaddr_in rem_srv; /*!< Where are we sending our packets...*/
} udp_broker_t;

int udp_broker_listener(udp_broker_t* broker, char* listener, char* target);
int udp_broker_create(udp_broker_t* broker, int how_many);

#endif
