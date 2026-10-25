/*
    File : HmmAlloc.c
    Desc : Heap Memory Manager allocation function stubs
*/
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include "Hmm.h"

pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;
meta_ptr head = NULL;
void* heap_end = NULL;

void remove_from_free_list(meta_ptr block){
	if(!(is_free(block))){
		return;
	}
	if(head == NULL){
		clear_free(block);
	return;
	}
	if (block->prev != NULL) {
            block->prev->next = block->next;
        } else {
            head = block->next;
        }
    
        if (block->next != NULL) {
            block->next->prev = block->prev;
        }
		clear_free(block);
		block->next = NULL;
		block->prev = NULL; 
}

void insert_in_order(meta_ptr block){
	set_free(block);
	block->next = NULL;
	block->prev = NULL;

if(head == NULL){
	head = block;
	return;
}
if(real_size(head) >= real_size(block)){
	head->prev = block;
	block->next = head;
	head = block; 
	return;
}


 meta_ptr current = head;
	while ( (current->next) && real_size(current->next) < real_size(block) ) {
				current = current->next;
	}
		block->next = current->next;
        block->prev = current;
        if (current->next != NULL) {
            current->next->prev = block;
        }
        current->next = block;

}

static meta_ptr find_block(size_t size)
{
	meta_ptr current = head;
	while (current) {
		if (real_size(current) >= (size & SIZE_MASK )) {
			return current;
		}
		current = current->next;
	}
	return NULL;	
}

void split_block(meta_ptr block, size_t size)
{
	meta_ptr new_block = (meta_ptr)((char*)block + size);
	new_block->size = real_size(block) - size ;	//(initial size contains size + overhead of new block)
	
	set_free(new_block);
	update_footer(new_block);

	
	block->size = size | (block->size & PREV_FREE_MASK);
	insert_in_order(new_block);
	
}

meta_ptr expand_heap(size_t size)
{
	static int heap_init = 0; //heap initialized flag 
	if(heap_init == 0){
		heap_init = 1 ;
		if(((int)sbrk(0) + 8 ) % 16 != 0){
			sbrk(8);
		}
		heap_end = sbrk(0);
	}

	size_t inc = ((size & SIZE_MASK) < MIN_INCREMENT) ? MIN_INCREMENT : ((size & SIZE_MASK));
	meta_ptr new_block = (meta_ptr) sbrk(inc);
	if (new_block == (void *)-1)
		return NULL;

	heap_end += inc ;	

	new_block->size = inc;
	
	//last_block = new_block;
	return new_block;
	
}


void *HmmAlloc(size_t size)
{

	if (size == 0) {
		return NULL;
	}

 	size_t aligned_size = ALIGN(size + HEADER_SIZE);
	if (aligned_size >= size) {
		size = aligned_size;
	} else {
		return NULL;
	}
	if(size < 32){
		size = 32;

	} 

	pthread_mutex_lock(&heap_lock);

	meta_ptr block = find_block(size);

	if (block == NULL) {
			block = expand_heap(size);
		}
	if (block == NULL) {
	pthread_mutex_unlock(&heap_lock);
		return NULL;
		}

		remove_from_free_list(block);

		if (block->size >= size + 32 ) {
			split_block(block, size);
		}

		clear_free(block);
		if(!is_last(block)) clear_prev_free(phys_next(block));

	pthread_mutex_unlock(&heap_lock);
		return (void*)((char*)block + HEADER_SIZE);	//start of data
	}

void *hmmCalloc(size_t nmemb, size_t size)
{
  size_t total = nmemb * size;
        // Check for multiplication overflow
        if (nmemb != 0 && total / nmemb != size)
            return NULL;
    
        void *ptr = HmmAlloc(total);
        if (ptr != NULL) {
            memset(ptr, 0, total);
        }
        return ptr;
}

void *hmmReAlloc(void *ptr, size_t reqSize)
{
    if (ptr == NULL) {
            return HmmAlloc(reqSize);
        }
        if (reqSize == 0) {
            HmmFree(ptr);
            return NULL;
        }

        size_t current_size = hmmSize(ptr);
        if (reqSize <= current_size) {
            return ptr; // Existing block is already large enough
        }

        void *new_ptr = HmmAlloc(reqSize);
        if (new_ptr == NULL) {
            return NULL;
        }

        memcpy(new_ptr, ptr, current_size);
        HmmFree(ptr);
        return new_ptr;
}

size_t hmmSize(void *ptr)
{
  	if (ptr == NULL)
		return 0;
	meta_ptr block = (meta_ptr)((char*)ptr - 8);
	return (real_size(block) - HEADER_SIZE );
}