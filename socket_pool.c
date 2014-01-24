#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "debug.h"
#include "socket_pool.h"


static bool list_insert_head(conversation_t** dst, conversation_t* node);
static conversation_t* list_remove_head(conversation_t** lst);
//static conversation_t* list_element_unleash(conversation_t** c);

/*! \brief Socket pool creation routine
* 
* A socket pool consisting of a linked list of capacity is allocated in this
* procedure. For each element a datagram socket is created in order to be used.
* In case of catastrophic problems (i.e. failed allocation, socket call failed
* will lead to program termination).
*
* \param capacity Maximum number of pre-allocated element
* \return socket_pool_t pointer
*/
socket_pool_t* socket_pool_create(uint32_t capacity)
{
    socket_pool_t* pool = NULL;
    int i = 0;

    if (!capacity)
        FATAL_ERROR("Can't allocate a socket_pool with %d elem\n", capacity);

    pool = (socket_pool_t*) malloc(sizeof(socket_pool_t));
    if (!pool)
        FATAL_ERROR("Can't allocate socket_pool structure\n");
    memset(pool, 0, sizeof(socket_pool_t));

    // Allocating socket element
    for (i = 0; i < capacity; i++) {
        conversation_t* dummy;
        dummy = (conversation_t*)malloc(sizeof(conversation_t));

        if (!dummy)
            FATAL_ERROR("Can't allocate a pool_conversation element\n");

        /* zero's all structure fields, so no need to set to NULL unused ptrs */
        memset(dummy, 0, sizeof(conversation_t));
        // here's our udp socket
        dummy->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (dummy->sock_fd == -1)
            FATAL_ERROR("Can't create socket. Errorno is %d %s\n", errno, 
                strerror(errno));
      
        if (pool->max_fd < dummy->sock_fd) {
            pool->max_fd = dummy->sock_fd;
            DEBUG_MSG("New max file descriptor used is %d\n", dummy->sock_fd);
        }

        list_insert_head(&pool->free_head, dummy);
        pool->free_count++;

    }
    
    pool->capacity = capacity;

    DEBUG_MSG("Socket pool creation finished, free_head (%p), "
        "used_head (%p), capacity (%d), free_count (%d), " 
        "used_count (%d), max_fd (%d)\n", 
        pool->free_head, pool->used_head, pool->capacity, pool->free_count, 
        pool->used_count, pool->max_fd);

    return pool;
}

/*! \brief Request an empty element of the pool
* Used to request an empty element from the pool. If no element are left NULL
* is returned. The element is then inserted into used_list until it's released
* with socket_pool_release function. Counters are also updated.
*
* \param pool, pool which request the element from.
* \return pool_conversation_t pointer or NULL in case of not available element
*/

conversation_t* socket_pool_acquire(socket_pool_t* p)
{
    if(!p) {
        DEBUG_MSG("Can't assign element from a %p pool\n", p);
        return NULL;
    }
    if (p->used_count < p->capacity && p->free_count && p->free_head) {
            DEBUG_MSG("Ready to assign element from pool %p\n", p);
            conversation_t* node;
            if((node = list_remove_head(&p->free_head))) {
                p->free_count--;
                DEBUG_MSG("Node %p removed from free list, now free_list head "
                    "%p (free_count = %d)\n", node, p->free_head, p->free_count);
                DEBUG_MSG("Ready to insert node %p in used_list %p\n", 
                    node, p->used_head);
                list_insert_head(&p->used_head, node);
                p->used_count++;
                DEBUG_MSG("Node %p node inserted in used list, now used list "
                    "head %p (used_count = %d)\n", node, p->used_head, 
                    p->used_count);
                return p->used_head;
            }
    }
    return NULL;
}

/*! \brief Released the conversation_t element passed
* Used to release an previously used element of the pool. 
* The element is then inserted into free_list until it's required again
* with socket_pool_acquire function. Counters are also updated.
*
* \param pool, pool which release the element from.
* \param c, conversation to be released.
* \return true in case of success, false otherwise.
*/
bool socket_pool_release(socket_pool_t* p, conversation_t* c)
{
    if(!p) {
        DEBUG_MSG("Cannot release element if pool is %p\n", p);
        return false;
    }
    if (!c) {
        DEBUG_MSG("Cannot released element if it's %p\n", c);
        return false;
    }
    
    
    if(c->prev) /* forward link of the predecessor if exists */
        c->prev->next = c->next;
    else /* c is the head of the list so change it */ 
        p->used_head = c->next;
    
    if(c->next) /* backward link of the successor if exists */
        c->next->prev = c->prev;

    p->used_count--;

    /* unleash current node */
    c->prev = NULL;
    c->next = NULL;

    list_insert_head(&p->free_head, c);
    p->free_count++;
    
    assert(p->capacity == p->used_count + p->free_count);
    
    DEBUG_MSG("Released element %p from %p, max_count (%d == %d) "
        "used_count + free_count \n", c, p->used_head, p->capacity,
        p->used_count + p->free_count);
    return true;
}


conversation_t* socket_pool_find(conversation_t* list, int sock_fd)
{
    conversation_t* dummy = list;
    while(dummy) {
        if(dummy->sock_fd == sock_fd) {
            DEBUG_MSG("Found element at %p (%d == %d)\n", 
                dummy, dummy->sock_fd, sock_fd);
            return dummy;
        }
        dummy = dummy->next;
    }
    DEBUG_MSG("No element found\n");
    return NULL;
}

int socket_pool_how_many_used(socket_pool_t* p)
{
    if (!p) {
        DEBUG_MSG("Pool is not valid (%p)\n", p);
        return -1;
    }
    else
        return p->used_count;
}

int socket_pool_how_many_free(socket_pool_t* p)
{
    if (!p) {
        DEBUG_MSG("Pool is not valid (%p)\n", p);
        return -1;
    }
    else
        return p->free_count;
}

int socket_pool_capacity(socket_pool_t* p)
{
    if (!p) {
        DEBUG_MSG("Pool is not valid (%p)\n", p);
        return -1;
    }
    else
        return p->capacity;
}


void socket_pool_free(socket_pool_t* p){
    int cnt = 0;    
    if (!p) {
        DEBUG_MSG("Free invoked on a %p pool\n", p); 
        return;
    }
    
    conversation_t* tmp = p->free_head;
    while(cnt < p->free_count && tmp) {
        DEBUG_MSG("Deallocating elem %d of free list\n", cnt + 1);
        tmp = p->free_head->next;
        free(p->free_head);
        p->free_head = tmp;
        cnt++;
    }

    tmp = p->used_head;
    cnt = 0;
    while(cnt < p->used_count && tmp) {
        DEBUG_MSG("Deallocating elem %d of used list\n", cnt + 1);
        tmp = p->used_head->next;
        free(p->used_head);
        p->used_head = tmp;
        cnt++;
    }

    free(p);
    DEBUG_MSG("Deallocated pool structure\n");
    p = NULL;
    return;
}


/*
*
* Insert element as head of the list passed as parameter.
* return false if the head was null before insertion
*
*/
bool list_insert_head(conversation_t** dst, conversation_t* node)
{
    if(!*dst) {
        DEBUG_MSG("Head is %p\n", *dst);
       *dst = node;
        DEBUG_MSG("Inserted %p as first element...now head is %p"
        " prev is %p next is %p\n", node, *dst, 
        (*dst)->prev, (*dst)->next);
        return false;
    }

    node->next = (*dst);
    (*dst)->prev = node;
    (*dst) = node;
    DEBUG_MSG("Inserted %p as inside pre-existent list...head is %p"
        " prev is %p next is %p\n", node, *dst, 
        (*dst)->prev, (*dst)->next);

    return true;
}

/*
*
* Remove element from head of the list passed as parameter.
* return null if the list was empty, otherwise removed element is returned
*
*/

conversation_t* list_remove_head(conversation_t** lst)
{
    conversation_t* node;
    if(!*lst) {
        DEBUG_MSG("Can't remove element from %p list\n", *lst);
        return NULL;
    }

    assert((*lst)->prev == NULL);
    
    node = *lst;
    *lst = node->next;
    /* after assigning node->next *lst can be null if only 1 elem is present */
    if(*lst) 
        (*lst)->prev = NULL;
   
    node->next = NULL;
    node->prev = NULL;
    DEBUG_MSG("Successfull removed first element: now list head is %p\n", *lst);
    return node;
}

/*
*
* Remove element from whatever (not head) position of the list passed as 
* parameter. The element c passed MUST belong to the list...:) no check is done
* Return null if the list was empty, otherwise removed element is returned
*
*/
#if 0
conversation_t* list_element_unleash(conversation_t** c)
{
    conversation_t* tmp = *c;
    /* setting the forward link of the predecessor if exists */
    if(tmp->prev) {
        tmp->prev->next = tmp->next;
    } else {
        /* *c is the head of the list so I need to advance it */
        *c = tmp->next;
    }
    /* setting backward link of the successor if exists */
    if(tmp->next)
        tmp->next->prev = tmp->prev;

    /* unleash current node */
    tmp->prev = NULL;
    tmp->next = NULL;
    return tmp;
}
#endif
