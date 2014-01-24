#include <check.h>
#include <stdlib.h>
#include <stdio.h>

#include "../socket_pool.h"

START_TEST (socket_pool_create_1_elem_test)
{
    socket_pool_t* p = NULL;
    int size = 1;
    p = socket_pool_create(size);
    ck_assert(p != NULL);
    ck_assert_int_eq(socket_pool_capacity(p), size);
    ck_assert_int_eq(socket_pool_how_many_free(p), size);
    ck_assert_int_eq(socket_pool_how_many_free(p), socket_pool_capacity(p) - 
        socket_pool_how_many_used(p));
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) - 
        socket_pool_how_many_free(p));
    socket_pool_free(p);
}
END_TEST

START_TEST (socket_pool_create_3_elem_test)
{
    socket_pool_t* p = NULL;
    int size = 3;
    p = socket_pool_create(size);
    ck_assert(p != NULL);
    ck_assert_int_eq(socket_pool_capacity(p), size);
    ck_assert_int_eq(socket_pool_how_many_free(p), size);
    ck_assert_int_eq(socket_pool_how_many_free(p), socket_pool_capacity(p) - 
        socket_pool_how_many_used(p));
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) - 
        socket_pool_how_many_free(p));
    socket_pool_free(p);
}
END_TEST

START_TEST (socket_pool_acquire_test)
{
    socket_pool_t* p = NULL;
    conversation_t* c1, *c2, *c3;
    int size = 3;    
    ck_assert_int_eq(socket_pool_how_many_used(p), -1);
    p = socket_pool_create(size);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    c1 = socket_pool_acquire(p);
    ck_assert(c1 != NULL);
    ck_assert(c1 == p->used_head);
    ck_assert(c1->next == NULL);
    ck_assert(c1->prev == NULL);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));

    c2 = socket_pool_acquire(p);
    ck_assert(c2 != NULL);
    ck_assert(c2 == p->used_head);
    ck_assert(c2->next == c1);
    ck_assert(c2->prev == NULL);    
    ck_assert(c1->prev == c2);
    ck_assert(c1->next == NULL);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));

    c3 = socket_pool_acquire(p);
    ck_assert(c3 != NULL);
    ck_assert(c3 == p->used_head);
    ck_assert(c3->next == c2);
    ck_assert(c3->prev == NULL);
    ck_assert(c3->next->next == c1);
    ck_assert(c3->next->prev == c3);
    ck_assert(c3->next->next->next == NULL);
    ck_assert(c3->next->next->prev == c2);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));

    socket_pool_free(p);
}
END_TEST

START_TEST (socket_pool_release_test)
{
    socket_pool_t* p = NULL;
    conversation_t* acq1, *acq2, *acq3, *acq4;
    conversation_t* rel1, *rel2, *rel3, *rel4;
    bool res;
    int size = 4;    
    p = socket_pool_create(size);
    /* 
    *   This is not a proper test...but it's the only way to see what static
    *   function does unless I removed static with some conditional compilation
    *   tricks...
    *   so let's unroll free_list by hand...4 elements are enough to cover all
    *   cases...
    *   free list is rel1->rel2->rel3->rel4
    */
    rel1 = p->free_head;
    rel2 = rel1->next;
    rel3 = rel2->next;
    rel4 = rel3->next;
    ck_assert(rel1->prev == NULL);
    ck_assert(rel2->prev == rel1);
    ck_assert(rel3->prev == rel2);
    ck_assert(rel4->prev == rel3);
    ck_assert(rel1->next->next->next == rel4);
    ck_assert(rel1->next->next == rel3);
    ck_assert(rel1->next == rel2);
    ck_assert(rel2->next->next == rel4);
    ck_assert(rel2->next == rel3);
    ck_assert(rel3->next == rel4);
    ck_assert(rel4->next == NULL);

    /* Acquire first element (rel1) */
    acq1 = socket_pool_acquire(p);
    ck_assert(acq1 == rel1);
    ck_assert(p->used_head == acq1);
    ck_assert(p->used_head->next == NULL);
    ck_assert(p->used_head->prev == NULL);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));

    /*
    *   Check what happened to other pointers....
    *   All other pointers rel2...rel4 should have been logically shifted 
    *   so rel2 is new free_list head..and so on...
    *   free list rel2->rel3->rel4
    *   used list acq1
    */
    ck_assert(p->free_head == rel2);
    ck_assert(p->free_head->next == rel3);
    ck_assert(p->free_head->next->next == rel4);
    ck_assert(p->free_head->prev == NULL);
    ck_assert(p->free_head->next->prev == rel2);
    ck_assert(p->free_head->next->next->prev == rel3);

    /* 
    *    acquire another element
    *    used list will be acq2->acq1
    *    free list rel3->rel4
    */   
    acq2 = socket_pool_acquire(p);
    ck_assert(acq2 == rel2);
    ck_assert(p->used_head == acq2);
    ck_assert(p->used_head->next == acq1);
    ck_assert(p->used_head->next->next == NULL);
    ck_assert(p->used_head->next->prev == acq2);  
    ck_assert(p->used_head->prev == NULL);    
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));

    /* 
    *   Check what happened to other pointers....
    *   All other pointers rel3...rel4 should have been logically shifted 
    *   so rel3 is new free_list head..and so on...
    */
    ck_assert(p->free_head == rel3);
    ck_assert(p->free_head->next == rel4);
    ck_assert(p->free_head->next->next == NULL);
    ck_assert(p->free_head->prev == NULL);
    ck_assert(p->free_head->next->prev == rel3);

    /*
    *   Acquire third element
    *   Used list acq3->acq2->acq1; 
    */
    acq3 = socket_pool_acquire(p);
    ck_assert(acq3 == rel3);
    ck_assert(p->used_head == acq3);
    ck_assert(p->used_head->next == acq2);
    ck_assert(p->used_head->prev == NULL);
    ck_assert(p->used_head->next->next == acq1);
    ck_assert(p->used_head->next->prev == acq3);
    ck_assert(p->used_head->next->next->next == NULL);
    ck_assert(p->used_head->next->next->prev == acq2);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));

    /* 
    *   Check what happened to other pointers....
    *   so rel4 is new free_list head..and so on...
    */
    ck_assert(p->free_head == rel4);
    ck_assert(p->free_head->next == NULL);
    ck_assert(p->free_head->prev == NULL);


    /*  
    *   Acquire last element 
    *   Used list acq4->acq3->acq2->acq1
    *   free list NULL    
    */
    acq4 = socket_pool_acquire(p);
    ck_assert(acq4 == rel4);
    ck_assert(p->used_head == acq4);
    ck_assert(p->used_head->next == acq3);
    ck_assert(p->used_head->prev == NULL);
    ck_assert(p->used_head->next->next == acq2);
    ck_assert(p->used_head->next->prev == acq4);
    ck_assert(p->used_head->next->next->next == acq1);
    ck_assert(p->used_head->next->next->prev == acq3);
    ck_assert(p->used_head->next->next->next->next == NULL);
    ck_assert(p->used_head->next->next->next->prev == acq2);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));

    /*
    *   Check what happened to other pointers....
    *   free_list head should be empty now
    */
    ck_assert(p->free_head == NULL);

    /*
    *   Now is time to release element
    *   used_list is now acq4->acq3->acq2->acq1
    *   Release first acq2 and check
    *   used list should be now acq4->acq3->acq1
    *   and free_list should be acq2
    */
    res = socket_pool_release(p, acq2);
    ck_assert(res == true);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));

    ck_assert(p->used_head == acq4);
    ck_assert(p->used_head->next == acq3);
    ck_assert(p->used_head->prev == NULL);
    ck_assert(p->used_head->next->next == acq1);
    ck_assert(p->used_head->next->prev == acq4); 
    ck_assert(p->used_head->next->next->next == NULL);
    ck_assert(p->used_head->next->next->prev == acq3);

    /* released element becomes new free list head */
    ck_assert(p->free_head == acq2);
    ck_assert(p->free_head->next == NULL);
    /* head has no predecessor ever */
    ck_assert(p->free_head->prev == NULL);


    /*  
    *   now used_list is  acq4->acq3->acq1
    *   Release again middle element so free list will finally become 
    *   Free list : acq3->acq2
    *   Used list : acq4->acq1
    */
    res = socket_pool_release(p, acq3);
    ck_assert(res == true);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));

    ck_assert(p->used_head == acq4);
    ck_assert(p->used_head->next == acq1);
    ck_assert(p->used_head->prev == NULL);
    ck_assert(p->used_head->next->next == NULL);
    ck_assert(p->used_head->next->prev == acq4);
    /* released element becomes new free list head */
    ck_assert(p->free_head == acq3);
    /* head has no predecessor ever */
    ck_assert(p->free_head->prev == NULL);
    ck_assert(p->free_head->next == acq2);
    ck_assert(p->free_head->next->next == NULL);
    ck_assert(p->free_head->next->prev == acq3);


    /* 
    *   Now get again acq3 so it will be the head of used list 
    *   Used List: acq3->acq4->acq1 
    *   then release head element element acq3 so after release operation 
    *   free list : acq3->acq2
    *
    */
    acq3 = NULL; /* clear pointer and re-aquire it */
    acq3 = socket_pool_acquire(p);
    ck_assert(acq3 != NULL);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));
    ck_assert(p->used_head == acq3);
    ck_assert(p->used_head->prev == NULL);
    ck_assert(p->used_head->next == acq4);
    ck_assert(p->used_head->next->next == acq1);
    ck_assert(p->used_head->next->prev == acq3);
    ck_assert(p->used_head->next->next->next == NULL);
    ck_assert(p->used_head->next->next->prev == acq4);

    ck_assert(p->free_head == acq2);
    ck_assert(p->free_head->next == NULL);
    ck_assert(p->free_head->prev == NULL);

    /* relase so used list = acq4->acq1 and free list: acq3->acq2 */    
    res = socket_pool_release(p, acq3);
    ck_assert(res == true);
    ck_assert(p->used_head == acq4);
    ck_assert(p->used_head->prev == NULL);
    ck_assert(p->used_head->next == acq1);
    ck_assert(p->used_head->next->next == NULL);
    ck_assert(p->used_head->next->prev == acq4);


    ck_assert(p->free_head == acq3);
    ck_assert(p->free_head->prev == NULL);
    ck_assert(p->free_head->next == acq2);
    ck_assert(p->free_head->next->next == NULL);
    ck_assert(p->free_head->next->prev == acq3);

    /* 
    *   Used list is now acq4->acq1
    *   let's release tail element acq1
    *   Free list will be : acq1->acq3->acq2 and final state for 
    *   used list: acq4
    */
    res = socket_pool_release(p, acq1);
    ck_assert(res == true);
    ck_assert(acq4->next == NULL);
    ck_assert(acq4->prev == NULL);
    ck_assert(p->free_head == acq1);
    ck_assert(p->used_head == acq4);
    ck_assert_int_eq(socket_pool_how_many_used(p), socket_pool_capacity(p) -
        socket_pool_how_many_free(p));
    ck_assert_int_eq(socket_pool_how_many_free(p), 
            socket_pool_capacity(p) - socket_pool_how_many_used(p));
    socket_pool_free(p);
}
END_TEST

START_TEST (socket_pool_how_many_free_test)
{
    socket_pool_t* p = NULL;
    int size = 1;    
    ck_assert_int_eq(socket_pool_how_many_free(p), -1);
    p = socket_pool_create(size);
    ck_assert_int_eq(socket_pool_how_many_free(p), size);
    socket_pool_free(p);
}
END_TEST

START_TEST (socket_pool_how_many_used_test)
{
    socket_pool_t* p = NULL;
    int size = 1;    
    ck_assert_int_eq(socket_pool_how_many_used(p), -1);
    p = socket_pool_create(size);
    ck_assert_int_eq(socket_pool_how_many_used(p), 0);
    socket_pool_acquire(p);
    ck_assert_int_eq(socket_pool_how_many_used(p), 1);
    socket_pool_free(p);
}
END_TEST

Suite *socket_pool_suite (void)
{
  Suite *s = suite_create ("Socket Pool");

  /* Core test case */
  TCase *tc_core = tcase_create("Core");
  tcase_add_test(tc_core, socket_pool_create_1_elem_test);
  tcase_add_test(tc_core, socket_pool_create_3_elem_test);
  tcase_add_test(tc_core, socket_pool_acquire_test);
  tcase_add_test(tc_core, socket_pool_release_test);
  tcase_add_test(tc_core, socket_pool_how_many_free_test);
  tcase_add_test(tc_core, socket_pool_how_many_used_test);
  suite_add_tcase(s, tc_core);

  return s;
}

int main (void)
{
  int number_failed;
  Suite *s = socket_pool_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
