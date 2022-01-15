//
// Created by Evgenii Ivanov on 07.01.2022.
//

#include "testing.h"
#include "mem.h"
#include "mem_internals.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x10000
#endif

// test with malloc one block
void test_1() {
    printf("test 1 start");

    void *heap = heap_init(7777);
    debug_heap(stdout, heap);
    printf("\nвыделяю 555: \n");
    _malloc(555);
    debug_heap(stdout, heap);
}

// test with free one block
void test_2() {
    printf("test 2 start\n");

    void *heap = heap_init(8080);
    void *block_to_free = _malloc(2048);
    debug_heap(stdout, heap);
    printf("\nосвобождаю 2048: \n");
    _free(block_to_free);
    debug_heap(stdout, heap);

}

// test with free of few blocks
void test_3() {
    printf("test 3 start\n");

    void *heap = heap_init(4444);
    void *block_to_free_1 = _malloc(100);
    void *block_to_free_2 = _malloc(200);
    void *block_to_free_3 = _malloc(300);
    void *block_to_free_4 = _malloc(400);

    debug_heap(stdout, heap);
    printf("\nосвобождение блока 3 [300]: ");
    _free(block_to_free_3);
    debug_heap(stdout, heap);
    printf("\nосвобождение блока 1 [100]: ");
    _free(block_to_free_1);
    debug_heap(stdout, heap);
    printf("\nосвобождение блока 2 [200]: ");
    _free(block_to_free_2);
    debug_heap(stdout, heap);
    printf("\nосвобождение блока 4 [400]: ");
    _free(block_to_free_4);
    debug_heap(stdout, heap);
}

// test with 'overmalloc'
void test_4() {
    printf("test 4 start\n");

    void *heap = heap_init(12000);
    _malloc(10000);
    debug_heap(stdout, heap);
    _malloc(4000);
    debug_heap(stdout, heap);
}

// test with new memory and moving
void test_5() {
    printf("test 5 start\n");

    void *heap1 = heap_init(5000);
    debug_heap(stdout, heap1);
    _malloc(5000);
    debug_heap(stdout, heap1);

    struct block_header* tmp = heap1;
    while (tmp->next != NULL) {
        tmp = tmp->next;
    }
    mmap((uint8_t*) tmp + size_from_capacity(tmp->capacity).bytes, 10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED_NOREPLACE, 0, 0);

    printf("%s\n", "result: ");
    _malloc(9000);
    debug_heap(stdout, heap1);
}
