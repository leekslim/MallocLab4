/*
 * An implicit free list solution, every block includes a header and footer, and there are simple
 * functions to coalesce, split and free. Realloc does not do anything.
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
    "Evan and Leeks", /* L: didn't know what else to put */
    /* First member's full name */
    "Li Keen 'Leeks' Lim",
    /* First member's email address */
    "lilim2019@u.northwestern.edu",
    /* Second member's full name (leave blank if none) */
    "Evan Tang",
    /* Second member's email address (leave blank if none) */
    "eptang@gmail.com"
};

/* Constants and Macros
* Various constant sizes and functions to traverse the implicit list
* are defined, the minimum block size used is 8 bytes
*/

/*  based on minimum block size 8 */
#define WSIZE 4 // size of the header and footer
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

/* used by mm_realloc in case new ptr within heap needed 
static void copy_block(void *src, void *dest)
{
	
} */


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
 * mm_realloc - L: simple mm_realloc that mostly uses mm_alloc and mm_free
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
			merge_size = old_next_size + old_size; /* hypothetical combined block size */
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
				return new_ptr;
			}
			else
			{
				size_t extendsize = MAX(asize,CHUNKSIZE);
				if ((new_ptr = extend_heap(extendsize/WSIZE)) == NULL){return NULL;}
				else {
					place(new_ptr, new_size);
					return new_ptr;
				}
			}
		}
			
		/* new size is smaller or equal */	
		else 
		{
			size_t extra_space = old_size - new_size; /* because old_size and new_size are both DWORD-Aligned, extra-space is a multiple of DWORD */
			size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
			size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
			if (extra_space < DWORD) {return ptr;} /* reduction in size too little to make any changes */
			else if (prev_alloc && next_alloc && (extra_space < 2*DWORD)) {return ptr;} /* no free neighbours and extra space less than minimum block size */
			else if (!next_alloc) /* give extra space to next block */
			{
				PUT(HDRP(ptr), PACK(new_size, 1));
				PUT((ptr + new_size - DSIZE), PACK(new_size, 1));
				PUT((ptr + new_size - WSIZE), PACK(old_next_size + extra_space, 0));
				PUT((FTRP(NEXT_BLKP(ptr)), PACK(old_next_size + extra_space, 0));
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
* L: Heap Checker as per instructions, should be called at various points to check heap
*/

int mm_check(void) {
	int x=1; /*initialize non-zero value, should return 0 if error, and print error messages before that */
	/* traverses headers and footers  to check size and alloc bits */
	/* traverse to check for external fragmentation, i.e. missed coalesce */
	/* checks for overlapping allocated blocks */
	return x;
}
