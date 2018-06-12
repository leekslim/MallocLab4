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

/* pack size bit and allocated bit into same word */
#define PACK(size, alloc)((size) | (alloc))

/* read or write to mem addr p */
#define GET(p)	(* (unsigned int *)(p))
#define PUT(p, val)		(*(unsigned int *)(p) = (val))

/* given addr p, read size bit or allocated bit */
#define GET_SIZE(p)	(GET(p) & ~0x7) // the block size encapsulated in the first WSIZE-3 bits includes the payload, header and footer
#define GET_ALL0C(p) (GET(p) & 0x1) // the alloc bit is the LSB of the header/footer

/* given block ptr bp, compute address of header or footer */
#define HDRP(bp)	((char *)(bp) - WSIZE) //HDRP means HeaderPointer
#define FTRP(bp)	((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) //FTRP means FooterPointer

/* given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)	((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)	((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* old given macros from assignment, can ignore and or delate later
* #define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
*
*
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
*/


/* 
 * mm_init - initialize the malloc package. A 'prologue' and 'epilogue' block is used to account for edge cases
 */
int mm_init(void)
{
	/* create the initial empty heap, mem_sbrk returns a generic pointer to the start of the heap, so heap_listp currently holds it */
	if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)–1) //L: not entirely sure what this is checking for
		return –1;
	PUT(heap_listp, 0);
	PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (3*WSIZE), PACK(0, 1));
	heap_listp += (2*WSIZE); //changes local variable holding heap pointer to the start of the actual heap
	/* extend the empty heap with a free block of CHUNKSIZE bytes */
	if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
		return –1;
    return 0;
}

/* 
 * mm_malloc - 
 *
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
	return NULL;
    else {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - 
 */
void mm_free(void *ptr)
{
}

/*
 * mm_realloc - 
 */
void *mm_realloc(void *ptr, size_t size)
{
    //ss
}

/*
* L: Heap Checker as per instructions, should be called at various points to check heap
*/

int mm_check(void) {
	int x=1; //initialize non-zero value, should return 0 if error, and print error messages before that
	//all checking code should go here, printing error message if heap not consistent, and changing x to 0
	return x;
}

/* PRIVATE STATIC FUNCTIONS */
/* extends the heap, checks if enough memory available, and whether size requested is aligned */
static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;
	
	/* allocate an even number of words to maintain alignment */
	size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
	if ((long)(bp = mem_sbrk(size)) == –1)
		return NULL;
	
	/* initialize free block header/footer and the epilogue header */
	PUT(HDRP(bp), PACK(size, 0));		/* Free block header */
	PUT(FTRP(bp), PACK(size, 0));		/* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));	/* New epilogue header */
	
	/* coalesce if the previous block was free */
	return coalesce(bp);
}

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
		PUT(HDRP(bp), PACK(size, 0));
		PUT (FTRP(bp), PACK(size,0));
	}

	else if (!prev_alloc && next_alloc) {		/* Case 3: coalesce with prev block */
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRPCbp), PACKCsize, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACKCsize, 0));
		bp = PREV_BLKP(bp);
	}

	else {						/* Case 4: coalesce with both prev and next block*/
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
			GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACKCsize, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACKCsize, 0));
		bp = PREV_BLKP(bp);
	}
	return bp;
}







