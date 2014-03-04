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

/*! \brief Socket pool creation routine
* 
* A socket pool consisting of a linked list of capacity is allocated in this
* procedure. For each element a SOCK_DGRAM or pair of SOCK_STREAM sockets are 
* created in order to be used.
* Catastrophic problems (i.e. failed allocation, socket call failed)
* will lead to program termination).
*
* \param capacity Maximum number of pre-allocated element
* \param istcp boolean to decide wheater create STREAM or DGRAM sockets
* \return socket_pool_t pointer
*/
socket_pool_t* socket_pool_create(uint32_t capacity, bool istcp)
{
    socket_pool_t* pool = NULL;
    int i = 0;
    int tmp_sock;

    if (!capacity)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Can't allocate a socket_pool"
        " with %d elem\n", capacity);

    pool = (socket_pool_t*) malloc(sizeof(socket_pool_t));
    if (!pool)
        FATAL_ERROR(stderr, EXIT_FAILURE, "Can't allocate socket_pool structure\n");
    memset(pool, 0, sizeof(socket_pool_t));

    FD_ZERO(&(pool->rd_set));

    // Allocating socket element
    for (i = 0; i < capacity; i++) {
        conversation_t* dummy;
        dummy = (conversation_t*)malloc(sizeof(conversation_t));

        if (!dummy)
            FATAL_ERROR(stderr, EXIT_FAILURE, "Can't allocate a pool_conversation element\n");

        /* zero's all structure fields, so no need to set to NULL unused ptrs */
        memset(dummy, 0, sizeof(conversation_t));
        // here's our udp socket
        if (!istcp) {
            dummy->loc_sock = socket(AF_INET, SOCK_DGRAM, 0);
            dummy->rem_sock = socket(AF_INET, SOCK_DGRAM, 0);
            if ((dummy->loc_sock == -1) || (dummy->rem_sock == -1))
                FATAL_ERROR(stderr, EXIT_FAILURE, "Can't create UDP socket."
                " (loc_sock =%d, rem_sock= %d). Errorno is %d %s\n", 
                dummy->loc_sock, dummy->rem_sock, errno, strerror(errno));
        } else {
            // accept will create it...
            //dummy->loc_sock = socket(AF_INET, SOCK_STREAM, 0);
            dummy->rem_sock = socket(AF_INET, SOCK_STREAM, 0);
            if ((dummy->loc_sock == -1) || (dummy->rem_sock == - 1))
                FATAL_ERROR(stderr, EXIT_FAILURE, "Can't create TCP sockets" 
                " (loc_sock =%d, rem_sock= %d). Errorno is %d %s\n", 
                dummy->loc_sock, dummy->rem_sock, errno, strerror(errno));
            /* we need also to allocate buffer for data in TCP...
             * we pre-allocate it to be 2^16 bytes...waste of memory but we will
             * do optimization later
            */
            dummy->buff = (uint8_t*)malloc((2 << 16) * sizeof(uint8_t));
            if (!dummy->buff)
                FATAL_ERROR(stderr, EXIT_FAILURE, "Can't allocate buffer for" 
                "TCP data");
        }
      
        tmp_sock = dummy->loc_sock > dummy->rem_sock ? 
            dummy->loc_sock : dummy->rem_sock;
        if (pool->max_fd < tmp_sock) {
            pool->max_fd = tmp_sock;
            DEBUG_MSG(stderr, "New max file descriptor used is %d\n", 
                pool->max_fd);
        }

        list_insert_head(&pool->free_head, dummy);
        pool->free_count++;
    }
    
    pool->capacity = capacity;

    DEBUG_MSG(stderr, "Socket pool (%s) creation finished, free_head (%p), "
        "used_head (%p), capacity (%d), free_count (%d), " 
        "used_count (%d), max_fd (%d)\n", (istcp == 1) ? "TCP" : "UDP",
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
        DEBUG_MSG(stderr, "Can't assign element from a %p pool\n", p);
        return NULL;
    }
    if (p->used_count < p->capacity && p->free_count && p->free_head) {
            DEBUG_MSG(stderr, "Ready to assign element from pool %p\n", p);
            conversation_t* node;
            if((node = list_remove_head(&p->free_head))) {
                p->free_count--;
                DEBUG_MSG(stderr, "Node %p removed from free list, now "
                    "free_list head %p (free_count = %d)\n", 
                    node, p->free_head, p->free_count);
                DEBUG_MSG(stderr, "Ready to insert node %p in used_list %p\n", 
                    node, p->used_head);
                if(!list_insert_head(&p->used_head, node))
                    p->used_tail = node;
                p->used_count++;
                DEBUG_MSG(stderr, "Node %p node inserted in used list, now"
                    " used list head %p (used_count = %d)\n", node, 
                    p->used_head, p->used_count);
                if(p->used_head->loc_sock)
                    FD_SET(p->used_head->loc_sock, &(p->rd_set));
                if(p->used_head->rem_sock)
                    FD_SET(p->used_head->rem_sock, &(p->rd_set));
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
        DEBUG_MSG(stderr, "Cannot release element if pool is %p\n", p);
        return false;
    }
    if (!c) {
        DEBUG_MSG(stderr, "Cannot released element if it's %p\n", c);
        return false;
    }
    
    
    if(c->prev) /* forward link of the predecessor if exists */
        c->prev->next = c->next;
    else /* c is the head of the list so change it */ 
        p->used_head = c->next;
    
    if(c->next) /* backward link of the successor if exists */
        c->next->prev = c->prev;
    else 
        p->used_tail = c->prev;

    p->used_count--;

    /* unleash current node */
    c->prev = NULL;
    c->next = NULL;

    list_insert_head(&p->free_head, c);
    p->free_count++;
    
    assert(p->capacity == p->used_count + p->free_count);
    if(c->loc_sock)
        FD_CLR(c->loc_sock, &(p->rd_set));
    if(c->rem_sock)
        FD_CLR(c->rem_sock, &(p->rd_set));
    if(c->rcv_bytes) /* TCP case only */
        c->rcv_bytes = 0;
    DEBUG_MSG(stderr, "Released element %p from %p, max_count (%d == %d) "
        "used_count + free_count \n", c, p->used_head, p->capacity,
        p->used_count + p->free_count);
    return true;
}

/*! \brief Conversation find routine
* 
* Given a socket descriptor it returns the conversation currently associated
* with that socket. Null in case nothing is found!
* The search process takes O(n) is linear in the number of the element inside
* the list
*
* \param list Where to look for the element (from there to the end)
* \param sock_fd socket descriptor we are looking for
* \return conversation_t pointer or NULL
*/
conversation_t* socket_pool_find(conversation_t* list, int sock_fd)
{
    conversation_t* dummy = list;
    while(dummy) {
        if(dummy->loc_sock == sock_fd) {
            DEBUG_MSG(stderr, "Found element at %p (%d == %d)\n", 
                dummy, dummy->loc_sock, sock_fd);
            return dummy;
        }
        dummy = dummy->next;
    }
    DEBUG_MSG(stderr, "No element found\n");
    return NULL;
}

/*! \brief get back how many element inside the pool are used
* 
* Given the pool pointer return the number of elements inside used_list
*
* \param p Pool where to look for
* \return int numer of elements used or -1 if the pool is invalid
*/
int socket_pool_how_many_used(socket_pool_t* p)
{
    if (!p) {
        ERROR_MSG(stderr, "Pool is not valid (%p)\n", p);
        return -1;
    }
    else
        return p->used_count;
}

/*! \brief get back how many element inside the pool are available
* 
* Given the pool pointer return the number of elements inside free_list
*
* \param p Pool where to look for
* \return int numer of elements available or -1 if the pool is invalid
*/
int socket_pool_how_many_free(socket_pool_t* p)
{
    if (!p) {
        ERROR_MSG(stderr, "Pool is not valid (%p)\n", p);
        return -1;
    }
    else
        return p->free_count;
}

/*! \brief get back how many conversation we can "simoultaneously" handle
* 
* Given the pool pointer return the maximum number of element available
*
* \param p Pool where to look for
* \return the numer of maximum elements (conversation) allowed inside the pool
* or -1 if the pool is not valid
*/

int socket_pool_capacity(socket_pool_t* p)
{
    if (!p) {
        ERROR_MSG(stderr, "Pool is not valid (%p)\n", p);
        return -1;
    }
    else
        return p->capacity;
}

/*! \brief get back the maximum file descriptor used inside the pool
* 
* Very useful when using select call :)
*
* \param p Pool where to look for
* \return the maximum file descriptor used or -1 if the pool is not valid
*/
int socket_pool_max_fd_used(socket_pool_t *p)
{
    if (!p) {
        ERROR_MSG(stderr, "Pool is not valid (%p)\n", p);
        return -1;
    }
    else
        return p->max_fd;
}


/*! \brief Free memory used by pool
* 
* Given the pool pointer just release memory used ;)
*
* \param p Pool where to look for
* \return nothing
*/
void socket_pool_free(socket_pool_t* p){
    int cnt = 0;    
    if (!p) {
        ERROR_MSG(stderr, "Free invoked on a %p pool\n", p); 
        return;
    }
    
    conversation_t* tmp = p->free_head;
    while(cnt < p->free_count && tmp) {
        DEBUG_MSG(stderr, "Deallocating elem %d of free list\n", cnt + 1);
        tmp = p->free_head->next;
        close(p->free_head->loc_sock);
        close(p->free_head->rem_sock);
        if (p->free_head->buff)
            free(p->free_head->buff);
        free(p->free_head);
        p->free_head = tmp;
        cnt++;
    }

    tmp = p->used_head;
    cnt = 0;
    while(cnt < p->used_count && tmp) {
        DEBUG_MSG(stderr, "Deallocating elem %d of used list\n", cnt + 1);
        tmp = p->used_head->next;
        close(p->used_head->loc_sock);
        close(p->used_head->rem_sock);
        if (p->used_head->buff)
            free(p->used_head->buff);
        free(p->used_head);
        p->used_head = tmp;
        cnt++;
    }

    free(p);
    DEBUG_MSG(stderr, "Deallocated pool structure\n");
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
        DEBUG_MSG(stderr, "Head is %p\n", *dst);
       *dst = node;
        DEBUG_MSG(stderr, "Inserted %p as first element...now head is %p"
        " prev is %p next is %p\n", node, *dst, 
        (*dst)->prev, (*dst)->next);
        return false;
    }

    node->next = (*dst);
    (*dst)->prev = node;
    (*dst) = node;
    DEBUG_MSG(stderr, "Inserted %p as inside pre-existent list...head is %p"
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
        DEBUG_MSG(stderr, "Can't remove element from %p list\n", *lst);
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
    DEBUG_MSG(stderr, "Successfull removed first element: now list head is %p\n", *lst);
    return node;
}
