#ifndef __DORAYAKI_SOCKET_POOL__
#define __DORAYAKI_SOCKET_POOL__

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

/* That's a bit ugly...we will use same data structure for TCP or UDP
* For UDP rem_sock and enemy_addr are the only used while for TCP
* conversation we will use both...it's a bit waste of memory but now
* there is no point in trying to do sofisticated thing in order not to
* waste such a few kbytes
*/
typedef struct conversation_t {
    int loc_sock; /*<! Used for UDP and TCP conversation */
    int rem_sock; /*<! Used only for TCP conversation */
    struct sockaddr_in enemy_addr; /*<! Used for UDP and TCP conversation */
    struct sockaddr_in fwd_addr; /*<! Used only for TCP conversation */
    uint16_t rcv_bytes; /*<! See what to do here as used only in TCP */
    uint16_t offset; /*<! See what to do here as used only in TCP */
    uint16_t expected_bytes; /*<! See what to do here as used only in TCP */
    uint8_t* buff; /*<! See what to do here as used only in TCP */
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

socket_pool_t* socket_pool_create(uint32_t, bool istcp);
conversation_t* socket_pool_acquire(socket_pool_t *);
bool socket_pool_release(socket_pool_t*, conversation_t*);
conversation_t* socket_pool_find(conversation_t*, int);
int socket_pool_how_many_used(socket_pool_t *);
int socket_pool_how_many_free(socket_pool_t* );
int socket_pool_capacity(socket_pool_t *);
int socket_pool_max_fd_used(socket_pool_t *);
void socket_pool_free(socket_pool_t*);

#endif 

