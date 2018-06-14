/*
 * mm.c - malloc using the segregated free list method
 * 
 * Notes:
 * 1. Every block has a header and a footer.
 * 2. The header contains size and allocation flag. 
 * 3. The footer contains size and allocation flag.

 Block information:

 1. Allocated block has:
 	 a) Header is 32 bits long.  Header is actually located 4 bytes before the block pointer
 	     size in bit 3 to 31
 	     allocated flag in bit 0 (1 for true, 0 for false)

     b) Payload and padding: variable size in 8 bytes increment.

     c) Footer is 32 bits long
 	     size in bit 3 to 31
 	     allocated flag in bit 0 (1 for true, 0 for false)

            <-------- Bits 31 to 3 ------ > 0
            +================================+
ptr-WSIZE-> | Header: size of the block   | A|
   ptr ---> |--------------------------------|
            : Payload, size is variable      :
            : depending on size of block     :
            : ...                            :
            |--------------------------------|
            | Footer: size of the block   | A|
            +================================+
            <-------- Bits 31 to 3 ------>  0


*/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Evan and Leeks",
    /* First member's full name */
    "Li Keen 'Leeks' Lim",
    /* First member's email address */
    "lilim2019@u.northwestern.edu",
    /* Second member's full name (leave blank if none) */
    "Evan Patrick Tang",
    /* Second member's email address (leave blank if none) */
    "eptang@gmail.com"
};

/* Constants and Macros
* Various constant sizes and functions to traverse the implicit list
* are defined, the minimum block size used is 8 bytes
*/

/*  based on minimum block size 8 */
#define WSIZE 4	// size of the header and footer
#define DSIZE 8 // regular or default block size
#define CHUNKSIZE 512 // when extending heap
#define MAX(x, y) ((x) > (y)? (x) : (y))

/* pack size bit and allocated bit into same word */
#define PACK(size, alloc)((size) | (alloc))

/* read or write to mem addr p */
#define GET(p)	(* (unsigned int *)(p))
#define PUT(p, val)	(*(unsigned int *)(p) = (val))

/* given addr p, read size bit or allocated bit */
#define GET_SIZE(p)	(GET(p) & ~0x7) // the block size encapsulated in the first WSIZE-3 bits includes the payload, header and footer
#define GET_ALLOC(p) (GET(p) & 0x1) // the alloc bit is the LSB of the header/footer

/* given block ptr bp, compute address of header or footer */
#define HDRP(bp)	((char *)(bp) - WSIZE) //HDRP means HeaderPointer
#define FTRP(bp)	((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) //FTRP means FooterPointer

/* given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)	((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)	((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* static global scalars */
static void *firstbp = NULL; /* points to the first block past the prologue after initializing*/

/* PRIVATE STATIC FUNCTIONS */
/* checks for all cases when a block is freed and performs correct coalesce */
static void *coalesce(void *bp) 
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) {			/* Case 1: no coalesce required */
		return bp;
	}

	else if (prev_alloc && !next_alloc) {		/* Case 2: coalesce with next block */
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT (HDRP(bp), PACK(size,0));
		PUT (FTRP(bp), PACK(size,0));
	}

	else if (!prev_alloc && next_alloc) {		/* Case 3: coalesce with prev block */
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}

	else {						/* Case 4: coalesce with both prev and next block*/
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
				GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	return bp;
}

/* extends the heap, checks if enough memory available, and whether size requested is aligned */
static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;
	
	/* allocate an even number of words to maintain alignment */
	size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
	if ((long)(bp = mem_sbrk(size)) == (-1)){return NULL;}
	
	/* initialize free block header/footer and the epilogue header */
	PUT(HDRP(bp), PACK(size, 0));		/* free block header */
	PUT(FTRP(bp), PACK(size, 0));		/* free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));	/* new epilogue header */
	
	/* coalesce if the previous block was free */
	return coalesce(bp);
}

/* used by mm_malloc to find fit*/
static void *find_fit(size_t asize)
{
	/* first-fit search */
	void *bp = firstbp; /* returns pointer to start of prologue, adds prologue to go to first byte to actual heap start */
	for (; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
			return bp;
		}
	}
	return NULL; /* No fit */
}

/* used by mm_malloc to do actual allocation by placing size and allocated bits, memory pointer in*/ 
static void place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) >= (2*DSIZE)) {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize-asize, 0));
		PUT(FTRP(bp), PACK(csize-asize, 0));
	}
	else {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
}

/* used by mm_realloc in case new ptr within heap needed */
static void copy_block(void *src, void *dest)
{
	size_t payload_size = GET_SIZE(HDRP(src)) - DSIZE; /* remove total size of header and footer */
	char *curr_src = (char *)src; 
	char *curr_dest = (char *)dest;
	for(; curr_src < ((char *)src + payload_size); curr_src++) /* traverse byte by byte */
	{
		*curr_dest = *curr_src;
		curr_dest ++;
	}
} 

/* 
 *
 *
 *mm_init - initialize the malloc package. A 'prologue' and 'epilogue' block is used to account for edge cases
 *
 *
 */
int mm_init(void)
{
	/* create the initial empty heap, mem_sbrk returns a generic pointer to the start of the heap, so heap_listp currently holds it */
	void *heap_listp = mem_sbrk(4*WSIZE);
	if (heap_listp == (void *)(-1)){ return -1;}/* L: not entirely sure what this is checking for */
	PUT(heap_listp, 0); /* for alignment */
	heap_listp += WSIZE;
	PUT(heap_listp, PACK(DSIZE, 1)); /* prologue header */
	heap_listp += WSIZE;
	PUT(heap_listp, PACK(DSIZE, 1)); /* prologue footer */
	heap_listp += WSIZE;
	PUT(heap_listp, PACK(0, 1)); /* epiprologue header */
	/* extend the empty heap with a free block of CHUNKSIZE bytes */
	if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {return -1;}
	firstbp = heap_listp - WSIZE; /* changes global variable pointing to first block after prologue*/
    return 0;
}

/* 
 * mm_malloc - attemps to find a free block and then returns that pointer
 *
 */
void *mm_malloc(size_t size)
{
	/* ignore spurious requests */
	if (size == 0)
		return NULL;
	
	size_t asize;	/* adjusted block size */
	size_t extendsize;	/* amount to extend heap if no fit */
	char *bp;
	
	/* adjust block size to include overhead and alignment reqs. */
	if (size <= DSIZE) /* smaller than regular block size of payload */
	{
		asize = 2*DSIZE;
	}
	/* if larger than regular block size, first DSIZE is for footer and header, second DSIZE-1 is for extra space if unaligned 
	 * its -1 because size is at least 1 above DSIZE, division mods away unaligned 'remainder', then multiply back
	*/
	else 
	{
		asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
	}
	
	/* search the free list for a fit */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	/* no fit found. get more memory and place the block */
	extendsize = MAX(asize,CHUNKSIZE);
	if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
		return NULL;
	place(bp, asize);
	return bp;
}

/*
 * mm_free - frees, and then coalesces, L: I don't think it checks whether that block is free or not but I guess it's not necessary
 */
void mm_free(void *bp)
{
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

/*
 * mm_realloc - L: work in progress, doesn't preserve data for some reason
 */
void *mm_realloc(void *ptr, size_t size)
{
	/* check if not allocated */
	if(ptr == NULL)
	{
		return mm_malloc(size);
	}
	/* check if just want to free */
	else if(size == 0)
	{
		mm_free(ptr);
		return NULL;
	}
	/* compare sizes */
	else
	{
		void* new_ptr = ptr; /* for possible relocation */
		size_t old_size = GET_SIZE(HDRP(ptr));
		size_t old_next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		size_t new_size; /* following operations make new size include overhead */
		/* adjust block size to include overhead and alignment reqs. */
		if (size <= DSIZE) /* smaller than regular block size of payload */
		{
			new_size = 2*DSIZE;
		}
		/* if larger than regular block size, first DSIZE is for footer and header, second DSIZE-1 is for extra space if unaligned 
		 * its -1 because size is at least 1 above DSIZE, division mods away unaligned 'remainder', then multiply back
		*/
		else 
		{
			new_size = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
		}
		
		/* new size is greater, find space for new block size while preserving data */
		if(new_size > old_size)
		{
			/* check if possible to coalesce with next block to make enough space */
			size_t merge_size = old_next_size + old_size; /* hypothetical combined block size */
			if(!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && (merge_size >= new_size))
			{
				PUT(HDRP(ptr), PACK(merge_size, 0)); /* trick place into thinking it is one contiguous block */
				place(ptr, new_size); /* place will split the next block if necessary */
				return ptr;
			}
			/* since not possible, find another place to reallocate to, note that this leaves out possibility of merging backwards */
			else if ((new_ptr = find_fit(new_size)) != NULL) 
			{
				place(new_ptr, new_size);
				copy_block(ptr, new_ptr);
				mm_free(ptr);
				return new_ptr;
			}
			else /* if no space, extend heap */
			{
				size_t extendsize = MAX(new_size,CHUNKSIZE);
				if ((new_ptr = extend_heap(extendsize/WSIZE)) == NULL){return NULL;}
				else {
					place(new_ptr, new_size);
					copy_block(ptr, new_ptr);
					mm_free(ptr);
					return new_ptr;
				}
			}
		}
			
		/* new size is smaller or equal */	
		else 
		{
			size_t extra_space = old_size - new_size; /* because old_size and new_size are both DWORD-Aligned, extra-space is a multiple of DWORD */
			size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
			size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
			if (extra_space < DSIZE) {return ptr;} /* reduction in size too little to make any changes */
			else if (prev_alloc && next_alloc && (extra_space < 2*DSIZE)) {return ptr;} /* no free neighbours and extra space less than minimum block size */
			else if (!next_alloc) /* give extra space to next block */
			{
				PUT(HDRP(ptr), PACK(new_size, 1));
				PUT((ptr + new_size - DSIZE), PACK(new_size, 1));
				PUT((ptr + new_size - WSIZE), PACK(old_next_size + extra_space, 0));
				PUT(FTRP(NEXT_BLKP(ptr)), PACK(old_next_size + extra_space, 0));
				return ptr;
			}
			else if (!prev_alloc) /* copy data into free previous block so that data is not lost before being copied, free block is now moved forward */
			{
				new_ptr = PREV_BLKP(ptr);
				size_t old_prev_size = GET_SIZE(HDRP(new_ptr));
				PUT(FTRP(ptr), PACK(old_prev_size + extra_space, 0)); /* before memory location of header is erased */
				copy_block(ptr, new_ptr);
				PUT(HDRP(new_ptr), PACK(new_size, 1));
				PUT(FTRP(new_ptr), PACK(new_size, 1));
				PUT(HDRP(NEXT_BLKP(new_ptr)), PACK(old_prev_size + extra_space, 0));
				return new_ptr;
			}
			else { /* no free neighbours but extra space enough to split block in 2 */
				PUT(HDRP(ptr), PACK(new_size, 1));
				PUT(FTRP(ptr), PACK(new_size, 1));
				PUT(HDRP(NEXT_BLKP(ptr)), PACK(extra_space, 0));
				PUT(FTRP(NEXT_BLKP(ptr)), PACK(extra_space, 0));
				return ptr;
			}
		}
	}
}

/*
* This heap checker has extra checking mechanisms for our later experimented with segregated free list, but it didn't work, so we didn't submit
* that solution but we still have the heap checker.
*/
/*
int mm_check(void) {
	int x=1; // initialize non-zero value, should return 0 if error, and print error messages before that 
	void *ptr;
    int number_of_free_blocks = 0;
    int number_of_free_blocks_in_seg_list = 0;
	void *seg_list_traverse = NULL; 

    // Verify prologue 
    ptr = firstbp - 3 * WSIZE;      // pointer to the start of the heap link list 
    if ((GET_SIZE(ptr) != DSIZE) || (GET_ALLOC(ptr) != 1)) {
        printf("Addr: %p - Prologue header error** \n", ptr);
		x=0;
    }
    ptr += WSIZE;
    if ((GET_SIZE(ptr) != DSIZE) || (GET_ALLOC(ptr) != 1)) {
        printf("Addr: %p - Prologue footer error** \n", ptr);
		x=0;
    }
    ptr = firstbp; // set pointer to first block

    // Iterating through entire heap. Convoluted code checks that
    // we are not at the epilogue. Loops thr and checks epilogue block! 
    while (GET_SIZE(HDRP(ptr)) > 0) {
    	if (GET_SIZE(HDRP(ptr)) != GET_SIZE(FTRP(ptr))) {
	        printf("Addr: %p - Header and footer size do not match\n", ptr);
			x=0;
	    }
    	// Check each block's address alignment 
    	if (ALIGN((size_t) ptr) != (size_t)ptr) {
    		printf("Addr: %p - Block Alignment Error** \n", ptr);
			x=0;
    	}
    	// Each block's bounds check 
    	if ((ptr > lastbp) || (ptr < firstbp)) {
    		printf("Addr: %p - Not within heap, top: %p, start: %p\n", ptr, lastbp, firstbp);
			x=0;
    	}
	    // Check if minimum block size met 
        if (GET_SIZE(HDRP(ptr)) < (2*DSIZE)) {
            printf("Addr: %p - ** Min Size Error ** \n", ptr);
			x=0;
        }
	    if (GET_ALLOC(HDRP(ptr)) != GET_ALLOC(FTRP(ptr))) {
    		printf("Addr: %p - ** Header and footer allocation flag do not match.\n", ptr);
			x=0;
    	}
    	// Check coalescing: If alloc bit of current and next block is 0 
        if (!GET_ALLOC(HDRP(ptr)) && (!GET_ALLOC(HDRP(NEXT_BLKP(ptr))))) {
            printf("Addr: %p - ** Coalescing Error** \n", ptr);
			x=0;
        }
        // Count total number of free blocks 
        if (!(GET_ALLOC(HDRP(ptr))))
		{number_of_free_blocks ++;}
        ptr = NEXT_BLKP(ptr); // go to next pointer
    }
	printf("The total number of free blocks is: %d \n", number_of_free_blocks);
	// Count total number of free blocks within seg lists
	for(seg_list_traverse = free0; seg_list_traverse != NULL; seg_list_traverse = NEXT_FREE_BLOCK(seg_list_traverse))
	{
		number_of_free_blocks_in_seg_list++;
	}
	printf("The total number of free blocks in free0 is: %d \n", number_of_free_blocks_in_seg_list);
	number_of_free_blocks_in_seg_list = 0;
	for(seg_list_traverse = free1; seg_list_traverse != NULL; seg_list_traverse = NEXT_FREE_BLOCK(seg_list_traverse))
	{
		number_of_free_blocks_in_seg_list++;
	}
	printf("The total number of free blocks in free1 is: %d \n", number_of_free_blocks_in_seg_list);
	number_of_free_blocks_in_seg_list = 0;
	for(seg_list_traverse = free2; seg_list_traverse != NULL; seg_list_traverse = NEXT_FREE_BLOCK(seg_list_traverse))
	{
		number_of_free_blocks_in_seg_list++;
	}
	printf("The total number of free blocks in free2 is: %d \n", number_of_free_blocks_in_seg_list);
	number_of_free_blocks_in_seg_list = 0;
	for(seg_list_traverse = free3; seg_list_traverse != NULL; seg_list_traverse = NEXT_FREE_BLOCK(seg_list_traverse))
	{
		number_of_free_blocks_in_seg_list++;
	}
	printf("The total number of free blocks in free3 is: %d \n", number_of_free_blocks_in_seg_list);
	number_of_free_blocks_in_seg_list = 0;
	for(seg_list_traverse = free4; seg_list_traverse != NULL; seg_list_traverse = NEXT_FREE_BLOCK(seg_list_traverse))
	{
		number_of_free_blocks_in_seg_list++;
	}
	printf("The total number of free blocks in free4 is: %d \n", number_of_free_blocks_in_seg_list);
	return x;
}*/