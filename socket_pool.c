#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <errno.h>

#include "socket_pool.h"


static _socket_pool_list_add(conversation_t** head, conversation_t* elem);
static _socket_pool_list_remove(conversation_t** head, conversation_t elem);
static _socket_pool_list_migrate(conversation_t** l1, conversation_t** l2);

/*! \brief Socket pool creation routine
* 
* A socket pool consisting of a linked list of max_element is allocated in this
* procedure. For each element a datagram socket is created in order to be used.
* In case of catastrophic problems (i.e. failed allocation, socket call failed
* will lead to program termination).
*
* \param max_element Maximum number of pre-allocated element
* \return socket_pool_t pointer
*/
socket_pool_t* socket_pool_create(uint32_t max_element)
{
    socket_pool_t* pool = NULL;
    if (!max_elem)
        FATAL_ERROR("Can't allocate a socket_pool with %d elem\n", max_element);

    pool = (socket_pool_t*) malloc(sizeof(socket_pool_t));
    if (!pool)
        FATAL_ERROR("Can't allocate socket_pool structure\n");
    memset(pool, 0, sizeof(socket_pool_t);

    // Allocating socket element
    for (i = 0; i < max_elem; i++) {
        pool_conversation_t* dummy;
        dummy = (pool_conversation_t*)malloc(sizeof(pool_conversation_t));

        if (!dummy)
            FATAL_ERROR("Can't allocate a pool_conversation element\n");

        /* zero's all structure fields, so no need to set to NULL unused ptrs */
        memset(dummy, 0, sizeof(pool_conversation_t));
        // here's our udp socket
        dummy->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (dummy->sock_fd == -1)
            FATAL_ERROR("Can't create socket. Errorno is %d %s\n", errno, 
                strerror(errno));
      
        if (pool->max_fd < dummy->sock_fd) {
            pool->max_fd = dummy->sock_fd;
            DEBUG_MSG("New max file descriptor used is %d\n", dummy->sock_fd);
        }

        if(!pool->free_head) {
            pool->free_head = dummy;
            DEBUG_MSG("First element: head is %p \n", pool->free_head);
        }　else {
            dummy->next = pool->free_head;
            pool->free_head->prev = dummy;
            pool->free_head = dummy;
            pool->free_count++;
            DEBUG_MSG("Inserted %p as first element...head is %p"
                " prev is %p next is %p\n", dummy, pool->free_head, 
                pool->free_head->prev, pool->free_head->next);
        }
    }
    
    pool->max_elem = max_elem;

    DEBUG_MSG("Socket pool creation finished, free_head (%p), "
        " used_head (%p), max_elem (%d), free_count (%d), " 
        " used_count (%d), max_fd (%d)\n", 
        pool->free_head, pool->used_head, pool->max_elem, pool->free_count, 
        pool->used_count, pool->max_fd);
    
}

/*! \brief Request an empty element of the pool
* Used to request an empty element from the pool. If no element are left NULL
* is returned. The element is then inserted into used_list until it's released
* with socket_pool_release function. Counter are also updated.
*
* \param pool, pool which request the element from.
* \return pool_conversation_t pointer or NULL in case of not available element
*/

pool_conversation_t* socket_pool_acquire(socket_pool_t* p)
{
    if(!p) {
        DEBUG_MSG("Can't assign element from a %p pool\n", p);
        return NULL;
    }
    if (p->used_count < p->max_elem && p->free_count && p->free_head) {
            if(list_migrate_first(&p->used_head, p->free_head)) {
                p->used_count++;
                p->free_count--;
                return p->used_head;
            }
    }
    return NULL;
}


bool socket_pool_release(socket_pool_t* p, pool_conversation_t* c)
{
    if(!p) {
        DEBUG_MSG("Cannot release element if pool is %p\n", p);
        return false;
    }
    if (!c) {
        DEBUG_MSG("Cannot released element if it's %p\n", c);
        return false;
    }

    if (!socket_pool_find(p->used_head, c->sock_fd)) {
        DEBUG_MSG("Element %d is not inside used pool, can't relsease it\n", c->sock_fd);
        return false;
    }
    
}


pool_conversation_t* socket_pool_find(pool_conversation_t* head, int sock_fd)
{
    while(head) {
        if(head->sock_fd == sock_fd) {
            DEBUG_MSG("Found element at %p (%d == %d)\n", sock_fd, head, head->sock_fd, sock_fd);
            return head;
        }
        head = head->next;
    }
    DEBUG_MSG("No element found\n");
    return NULL;
}

uint32_t circ_buff_how_many_used(socket_pool_t *);
uint32_t circ_buff_capacity(socket_pool_t *);
void circ_buff_free(socket_pool_t*);

/*
*
*   Migrate element from l_src, to l_dst, and return pointer to the element migrated
*   FIX_ME: This function can be called only if l_src and l_dst points to the
*   head of the source and destination lists. otherwise a big mess will occur.!!
*
*/
bool list_migrate_first(pool_conversation_t** l_dst, 
    pool_conversation_t* node)
{
    if(!*l_src || *l_src) {
        DEBUG_MSG("Cannot migrate element if the src list is empty %p or if "
            "%p is not the head of the list (%p->prev (%p) != NULL)\n", 
            *l_src, *l_src, *l_src->prev);        
        return false;
    }
   
    pool_conversation_t* node = *l_src;
    if(!*l_dst) {
        /* first element of used list */
        *l_dst = node;
        *l_dst->next = NULL;
        *l_src = node->next;
        *l_src->prev = NULL;
        DEBUG_MSG("Inserted first elem %p in %p list\n", node, *l_dst);
    } else {
        *l_dst->prev = node;
        *l_src = node->next;
        node->next = *l_dst;
        *l_dst = node;
        *l_dst->prev = NULL;
        DEBUG_MSG("Inserted elem %p in dst list (%p), src list now is %p\n", 
            node, *l_dst, *l_src);
    }
    return true;        
}

