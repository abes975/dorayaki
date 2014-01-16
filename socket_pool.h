#ifndef __DORAYAKI_SOCK_POOL__
#define __DORAYAKI_SOCK_POOL__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int sock_fd;
    struct sockaddr_in clientaddr;
    struct conversation_t* prev;
    struct conversation_t* next;
} pool_conversation_t; 


typedef struct {
    conversation_t* free_head;
    conversation_t* used_head;
    uint32_t max_elem; /*!< Number of element pre-allocated inside the list */
    uint32_t free_count; /*!< Number of valid element inside free list */
    uint32_t used_count; /*!< How many are used...pending request */
    uint32_t max_fd; /*!< max value socket descriptor, useful when using select */
} socket_pool_t;

socket_pool_t* socket_pool_create(uint32_t);
pool_conversation_t* socket_pool_acquire(socket_pool_t *);
pool_conversation_t* socket_pool_release(socket_pool_t*, pool_conversation_t*);
pool_conversation_t* socket_pool_find(socket_pool_t*, int);
uint32_t circ_buff_how_many_used(socket_pool_t *);
uint32_t circ_buff_capacity(socket_pool_t *);
void circ_buff_free(socket_pool_t*);

#endif 

