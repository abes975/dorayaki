#ifndef __DORAYAKI_SOCKET_POOL__
#define __DORAYAKI_SOCKET_POOL__

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>


typedef struct conversation_t {
    int sock_fd;
    struct sockaddr_in clientaddr;
    struct conversation_t* prev;
    struct conversation_t* next;
} conversation_t; 


typedef struct {
    conversation_t* free_head;
    conversation_t* used_head;
    conversation_t* used_tail;
    int capacity; /*!< Number of element pre-allocated inside the list */
    int free_count; /*!< Number of valid element inside free list */
    int used_count; /*!< How many are used...pending request */
    int max_fd; /*!< max value socket descriptor, useful when using select */
    fd_set rd_set; /* !< we want to read from those.... */
} socket_pool_t;

socket_pool_t* socket_pool_create(uint32_t);
conversation_t* socket_pool_acquire(socket_pool_t *);
bool socket_pool_release(socket_pool_t*, conversation_t*);
conversation_t* socket_pool_find(conversation_t*, int);
int socket_pool_how_many_used(socket_pool_t *);
int socket_pool_how_many_free(socket_pool_t* );
int socket_pool_capacity(socket_pool_t *);
int socket_pool_max_fd_used(socket_pool_t *);
void socket_pool_free(socket_pool_t*);

#endif 

