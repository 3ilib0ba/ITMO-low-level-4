#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mem.h"
#include "mem_internals.h"
#include "util.h"

//#define _DEFAULT_SOURCE

void debug_block(struct block_header* b, const char* fmt, ... );
void debug(const char* fmt, ... );

extern inline block_size size_from_capacity( block_capacity cap );
extern inline block_capacity capacity_from_size( block_size sz );

static bool            block_is_big_enough( size_t query, struct block_header* block ) { return block->capacity.bytes >= query; }
static size_t          pages_count   ( size_t mem )                      { return mem / getpagesize() + ((mem % getpagesize()) > 0); }
static size_t          round_pages   ( size_t mem )                      { return getpagesize() * pages_count( mem ) ; }

static void block_init( void* restrict addr, block_size block_sz, void* restrict next ) {
  *((struct block_header*)addr) = (struct block_header) {
    .next = next,
    .capacity = capacity_from_size(block_sz),
    .is_free = true
  };
}

static size_t region_actual_size( size_t query ) { return size_max( round_pages( query ), REGION_MIN_SIZE ); }

extern inline bool region_is_invalid( const struct region* r );

static void* map_pages(void const* addr, size_t length, int additional_flags) {
  return mmap( (void*) addr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | additional_flags , 0, 0 );
}


/*  аллоцировать регион памяти и инициализировать его блоком */
static struct region alloc_region  ( void const * addr, size_t query ) {
    const size_t size = region_actual_size(query);
    void *new_block_address = map_pages(addr, size, MAP_FIXED_NOREPLACE);

    if (new_block_address == MAP_FAILED) {
        new_block_address = map_pages(addr, size, 0);
        if (new_block_address == MAP_FAILED) {
            new_block_address = NULL;
        }
    }

    struct region new_block = (struct region) { .addr = new_block_address, .size = size, .extends = true };
    block_init(new_block_address, (block_size) { .bytes = region_actual_size(query) }, NULL);

    return new_block;

}

static void* block_after( struct block_header const* block )         ;

void* heap_init( size_t initial ) {
  const struct region region = alloc_region( HEAP_START, initial );
  if ( region_is_invalid(&region) ) return NULL;

  return region.addr;
}

#define BLOCK_MIN_CAPACITY 24

/*  --- Разделение блоков (если найденный свободный блок слишком большой )--- */

static bool block_splittable( struct block_header* restrict block, size_t query) {
  return block-> is_free && query + offsetof( struct block_header, contents ) + BLOCK_MIN_CAPACITY <= block->capacity.bytes;
}

static bool split_if_too_big( struct block_header* block, size_t query ) {
    if (!block_splittable(block, query)) {
        return false;
    }

    void *second_block = (void*)((uint8_t*) block + offsetof(struct block_header, contents) + query);
    block_size size_second_block = (block_size) {.bytes = block->capacity.bytes - query};
    block_init(second_block, size_second_block, NULL);

    block->capacity.bytes = query;
    block->next = second_block;
    
    return true;
}


/*  --- Слияние соседних свободных блоков --- */

static void* block_after( struct block_header const* block )              {
  return  (void*) (block->contents + block->capacity.bytes);
}
static bool blocks_continuous (
                               struct block_header const* fst,
                               struct block_header const* snd ) {
  return (void*)snd == block_after(fst);
}

static bool mergeable(struct block_header const* restrict fst, struct block_header const* restrict snd) {
  return fst->is_free && snd->is_free && blocks_continuous( fst, snd ) ;
}

static bool try_merge_with_next( struct block_header* block ) {
    if (!block->next || !mergeable(block, block->next)) {
        return false;
    }

    block->capacity.bytes += block->next->capacity.bytes;
    block->next = block->next->next;
    return true;
}

/*  --- ... ecли размера кучи хватает --- */
struct block_search_result {
  enum {BSR_FOUND_GOOD_BLOCK, BSR_REACHED_END_NOT_FOUND, BSR_CORRUPTED} type;
  struct block_header* block;
};


static struct block_search_result find_good_or_last  ( struct block_header* restrict block, size_t sz )    {
    struct block_header *current_block = block;
    struct block_header *next_block = block;

    while (current_block) {
        if (current_block->is_free) {
            if (block_is_big_enough(sz, current_block)) {
                split_if_too_big(current_block, sz);
                return (struct block_search_result) {
                        .type = BSR_FOUND_GOOD_BLOCK,
                        .block = current_block
                };
            } else if (!try_merge_with_next(current_block)) {
                next_block = current_block;
                current_block = current_block->next;
            }
        } else {
            next_block = current_block;
            current_block = current_block->next;
        }
    }

    return (struct block_search_result) {
            .type = BSR_REACHED_END_NOT_FOUND,
            .block = next_block
    };

}

/*  Попробовать выделить память в куче начиная с блока `block` не пытаясь расширить кучу
 Можно переиспользовать как только кучу расширили. */
static struct block_search_result try_memalloc_existing ( size_t query, struct block_header* block ) {
    return find_good_or_last(block, query);
}



static struct block_header* grow_heap( struct block_header* restrict last, size_t query ) {
    block_size size = size_from_capacity(last->capacity);
    void* new_heap_addr = (void*)((uint8_t*) last + size.bytes);
    struct region additional_region = alloc_region(new_heap_addr, query);
    split_if_too_big(additional_region.addr, query);
    last->next = additional_region.addr;
    if(try_merge_with_next(last)) {
        return last;
    }
    else return last->next;
}

/*  Реализует основную логику malloc и возвращает заголовок выделенного блока */
static struct block_header* memalloc( size_t query, struct block_header* heap_start) {
    struct block_header *new_header;
    struct block_search_result result = try_memalloc_existing(query, heap_start);


    if (result.type == BSR_CORRUPTED) {
        result.block = heap_init(query);
        split_if_too_big(result.block, query);
    } else if (result.type == BSR_REACHED_END_NOT_FOUND) {
        new_header = grow_heap(result.block, query);
        split_if_too_big(new_header, query);
        return new_header;
    }
    result.block->is_free=false;
    return result.block;
}

void* _malloc( size_t query ) {
  struct block_header* const addr = memalloc( query, (struct block_header*) HEAP_START );
  if (addr) return addr->contents;
  else return NULL;
}

static struct block_header* block_get_header(void* contents) {
  return (struct block_header*) (((uint8_t*)contents)-offsetof(struct block_header, contents));
}

void _free( void* mem ) {
  if (!mem) return ;
  struct block_header* header = block_get_header( mem );
  header->is_free = true;
  // merge free blocks
  while(try_merge_with_next(header));
}
