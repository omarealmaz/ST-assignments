/*
    File : Hmm.h
    Desc : Heap Memory Manager include file
*/

#ifndef HMM_H
#define HMM_H

#include <stddef.h>

#define HEADER_SIZE 8
#define FOOTER_SIZE 8
#define OVERHEAD 16
#define FREE_HEADER_SIZE 24
#define FREE_MASK  ((size_t)1)       // ...00000001
#define SIZE_MASK  (~((size_t)3))    // ...11111100
#define PREV_FREE_MASK ((size_t)2)   // ...00000010
#define ALIGN(size) (((size) + (16 - 1)) & ~(16 - 1))
#define MIN_INCREMENT 128 * 1024 //128KB in real malloc
#define MIN_DECREMENT 128 * 1024 //128KB in real malloc

typedef struct meta_block *meta_ptr;
    
    struct meta_block {
        size_t size;
        meta_ptr next;
        meta_ptr prev;
    };

extern meta_ptr head;
extern meta_ptr last_block;

static inline void set_free(meta_ptr block){
block->size |= FREE_MASK ;
}

static inline void clear_free(meta_ptr block){
block->size &= ~(FREE_MASK) ;
}

static inline void set_prev_free(meta_ptr block){
block->size |= PREV_FREE_MASK ;
}

static inline void clear_prev_free(meta_ptr block){
block->size &= ~(PREV_FREE_MASK) ; 
}

static inline size_t real_size(meta_ptr block){
return ((block->size) & (SIZE_MASK));
}

static inline int is_free(meta_ptr block){
return ((block->size) & (FREE_MASK));
}

static inline int prev_is_free(meta_ptr block){
return ((block->size) & (PREV_FREE_MASK));
}

static inline meta_ptr phys_next(meta_ptr block){
	return (meta_ptr)((char*)block + real_size(block));
}

static inline size_t prev_size(meta_ptr block){
	return *(size_t*)((char*)block - FOOTER_SIZE) & SIZE_MASK;
}

static inline meta_ptr phys_prev(meta_ptr block){
	return (meta_ptr)((char*)block - prev_size(block));
}

static inline void update_footer(meta_ptr block){
 *((size_t*)((char*)block + real_size(block) - FOOTER_SIZE )) = real_size(block);
}



void remove_from_free_list(meta_ptr block);

void insert_in_order(meta_ptr block);

void HmmFree(void *ptr);

void *HmmAlloc(size_t size);

void *hmmCalloc(size_t nmemb, size_t size);

void *hmmReAlloc(void *ptr, size_t reqSize);

size_t hmmSize(void *ptr);

#endif /* HMM_H */