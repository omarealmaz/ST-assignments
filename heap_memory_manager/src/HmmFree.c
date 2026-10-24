/*
    File : HmmFree.c
    Desc : Heap Memory Manager free function stub
*/
#include<unistd.h>
#include "Hmm.h"

meta_ptr merge_free_blocks(meta_ptr block)
{
	if ((block != last_block) && is_free(phys_next(block))) {

		meta_ptr next_block = phys_next(block);
		remove_from_free_list(next_block);
		
		block->size = (real_size(block) + real_size(next_block))|(prev_is_free(block) ? PREV_FREE_MASK : 0);
		update_footer(block);

		if (next_block == last_block) {
			last_block = block;
		}
	}
	if (prev_is_free(block)) {
		meta_ptr prev_block = phys_prev(block);
		remove_from_free_list(prev_block);
		//prev_block->next = block->next;
		prev_block->size = (real_size(prev_block) + real_size(block));

		if (block == last_block) {
			last_block = prev_block;
		}
		block = prev_block;
		update_footer(block);
	}
	return block;
}


void HmmFree(void *ptr)
{
  //ptr is start of data
	if (ptr == NULL) 
		return;

	meta_ptr block = (meta_ptr)( (char*)ptr - 8);

	if(is_free(block)) return;	

	block = merge_free_blocks(block);
	set_free(block);
	update_footer(block);

	if (block != last_block) {
            set_prev_free(phys_next(block));
        }

	
if (block == last_block && real_size(block) >= MIN_DECREMENT) {
	        sbrk(-(real_size(block)));
            last_block = NULL;
        } else {
            insert_in_order(block);
        }
}