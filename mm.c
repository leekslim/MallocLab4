#define DEBUGx
/*
 * mm.c - malloc using the segregated free list method
 * 
 * Notes:
 * 1. Every block has a header and a footer.
 * 2. The header contains size and allocation flag.  The header is actually at the end of the previous block.
 * 3. The footer contains size and allocation flag.
 * 4. Free list are linked to the segregated list.
 * 5. All free block contains pointer to the previous and next free block.
 * 6. The segregated list headers are organized by 2^k size, for example, k = 28

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


 2. Free block has:

 	 a) Header is 32 bits long
 	     size in bit 3 to 31
 	     allocated flag in bit 0 (1 for true, 0 for false)

     b) pointer to previous free block

     c) pointer to next free block

     d) Space (variable length)

     e) Footer is 32 bits long
 	     size is from bit 3 to 31
 	     bit 0 is for the allocated flag (1 for true, 0 for false)

            <-------- Bits 31 to 3 ------ > 0
            +================================+
ptr-WSIZE-> | Header: size of the block   | A|
   ptr ---> |--------------------------------|
            | ptr to its previous free block |
ptr+WSIZE-> |--------------------------------|
            | ptr to its next free block     |
            |--------------------------------|
            : additional space not used for  :
            : free block, size is variable   :
            : depending on size of free block:
            : ...                            :
            |--------------------------------|
            | Footer: size of the block   | A|
            +================================+
            <-------- Bits 31 to 3 ------ > 0


*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "26",
    /* First member's full name */
    "Evan Patrick Tang",
    /* First member's email address */
    "evantang2019@u.northwestern.edu",
    /* Second member's full name (leave blank if none) */
    "Li Keen Lim",
    /* Second member's email address (leave blank if none) */
    "likeenlim2019@u.northwestern.edu"
};

/* Basic constants and macros */
#define WSIZE                     4       /* Word and header/footer size (bytes) */
#define DSIZE                     8       /* Double word size (bytes) */
#define CHUNK_SIZE                (1<<12) /* Extend heap by this amount (4096 bytes) */
#define INITIALIZATION_CHUNK_SIZE (1<<6)  /* A smaller size (64 bytes) at the initialization time provides better utilization */

/* double word (8) alignment */
#define ALIGNMENT 8
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SEG_LIST_ARRAY_SIZE     28

#define MAX(x, y) ((x) > (y) ? (x) : (y)) // This is from example in textbook
#define MIN(x, y) ((x) < (y) ? (x) : (y)) // This is not from textbook but it is useful

// Pack a size and allocated bit into a word
#define PACK(size, alloc) ((size) | (alloc))

// Read and write a word at address p 
#define GET(p)            (*(unsigned int *)(p))
#define PUT(p, val)       (*(unsigned int *)(p) = (val))

// Store next and previous pointers for free blocks
#define SET_PTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))

// Read the size and allocation bit from address p 
#define GET_SIZE(p)     (GET(p) & ~0x7)
#define GET_ALLOC(p)    (GET(p) & 0x1)

// Address of block's header and footer 
#define HDRP(ptr) ((char *)(ptr) - WSIZE)
#define FTRP(ptr) ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - DSIZE)

// Address of (physically) next and previous blocks 
#define NEXT_BLKP(ptr) ((char *)(ptr) + GET_SIZE((char *)(ptr) - WSIZE))
#define PREV_BLKP(ptr) ((char *)(ptr) - GET_SIZE((char *)(ptr) - DSIZE))

// Pointer to pointer of free block's previous and next on the segregated list.
#define PREV_FREE_BLOCK(ptr) (*(char **)(ptr))
#define NEXT_FREE_BLOCK(ptr) (*(char **)(PTR_TO_NEXT_FREE_BLOCK(ptr)))

// Address of free block's next and previous entries
#define PTR_TO_PREV_FREE_BLOCK(ptr) ((char *)(ptr))
#define PTR_TO_NEXT_FREE_BLOCK(ptr) ((char *)(ptr) + WSIZE)

/* Global variables */
void *starting_addr_of_heap = 0;  /* Pointer to start of heap */
void *segregated_free_lists[SEG_LIST_ARRAY_SIZE];

// Additional functions
void *extend_heap(size_t size);
void *coalesce(void *ptr);
void *place(void *ptr, size_t adjusted_size);
void insert_node(void *ptr, size_t size);
void delete_node(void *ptr);
#ifdef DEBUG
void *top_of_heap = 0;
void mm_checkheap(int lineno);
#endif

#if defined(__i386__) /* 32 bit detected, gcc -m32 option is probably used */
	#define GCC_M32_OPTION // comment this line out this if this will not be compiled for x86 with gcc -m32 option
#endif

// C language code is also written in case the asm function cannot be used.  To use the C code, comment out the line which defines GCC_M32_OPTION
// asm() is documented in the C++ standard and supported by gcc.
// Below is from the ISO C++ official standards documentation at http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/n4727.pdf:
//      10.4 The asm declaration [dcl.asm]
//           1 An asm declaration has the form
//                asm-definition:
//                     attribute-specifier-seqopt asm ( string-literal ) ;
#ifdef GCC_M32_OPTION
	#define FIND_POSITION_OF_MOST_SIGNIFICANT_BIT_IN_A_WORD(index_to_array, temp_size) {\
	     asm ("bsrl %0, %0" : "=r" (index_to_array) : "0" (temp_size));\
	 	 index_to_array = MIN(index_to_array, SEG_LIST_ARRAY_SIZE-1); /* Make sure the array index is not bigger than the number of array allocated */ \
	}
#else  // If it is not compile with -m32, the following C language code will be used
    // It is possible to implement a shorter version with a while loop, but the performance is not good.
	#define FIND_POSITION_OF_MOST_SIGNIFICANT_BIT_IN_A_WORD(index_to_array, temp_size) {\
		if (temp_size & 0xFF000000) {\
			index_to_array = 24;\
		}\
		else if (temp_size & 0x00FF0000) {\
			index_to_array = 16;\
		}\
		else if (temp_size & 0x0000FF00) {\
			index_to_array = 8;\
		}\
		else if (temp_size & 0x000000FF) {\
			index_to_array = 0;\
		}\
		temp_size >>= index_to_array;\
		if (temp_size & 0x00000080) {\
			index_to_array += 7;\
		}\
		else if (temp_size & 0x00000040) {\
			index_to_array += 6;\
		}\
		else if (temp_size & 0x00000020) {\
			index_to_array += 5;\
		}\
		else if (temp_size & 0x00000010) {\
			index_to_array += 4;\
		}\
		else if (temp_size & 0x00000008) {\
			index_to_array += 3;\
		}\
		else if (temp_size & 0x00000004) {\
			index_to_array += 2;\
		}\
		else if (temp_size & 0x00000002) {\
			index_to_array += 1;\
		}\
		index_to_array = MIN(index_to_array, SEG_LIST_ARRAY_SIZE-1);\ // Make sure the array index is not bigger than the number of array allocated
	}
#endif

void *coalesce(void *ptr)
{
	void *next_block_ptr = NEXT_BLKP(ptr);
	void *prev_block_ptr = PREV_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next_block_ptr));
	size_t prev_alloc = GET_ALLOC(HDRP(prev_block_ptr));
    size_t size = GET_SIZE(HDRP(ptr));

    if (prev_alloc && next_alloc) {         // Case 1 - Nothing to coalesce
        return ptr;
    } else if (prev_alloc && !next_alloc) { // Case 2 - next block is free
        delete_node(ptr);                   // must delete the nodes before changing size of the node
        delete_node(next_block_ptr);        // must delete the nodes before changing size of the node
        size += GET_SIZE(HDRP(next_block_ptr));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));      // Once the size of the header is changed, the footer is the new (coalesced) footer
    } else if (!prev_alloc && next_alloc) { // Case 3 - previous block is free
        delete_node(prev_block_ptr);        // must delete the nodes before changing size of the node
        delete_node(ptr);                   // must delete the nodes before changing size of the node
        size += GET_SIZE(HDRP(prev_block_ptr));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(prev_block_ptr), PACK(size, 0));
        ptr = prev_block_ptr;
    } else {                               // Case 4 - both previous and next are free
        delete_node(prev_block_ptr);       // must delete the nodes before changing size of the node
        delete_node(ptr);                  // must delete the nodes before changing size of the node
        delete_node(next_block_ptr);
        size += GET_SIZE(HDRP(prev_block_ptr)) + GET_SIZE(HDRP(next_block_ptr));
        PUT(HDRP(prev_block_ptr), PACK(size, 0));
        PUT(FTRP(next_block_ptr), PACK(size, 0));
        ptr = prev_block_ptr;
    }
    insert_node(ptr, size);
    return ptr;
}

void delete_node(void *ptr) {
    unsigned int index_to_array = 0;
    size_t size = GET_SIZE(HDRP(ptr));

    // Select segregated free list based on the size
    // C language code is also written in case the asm function cannot be used.  To use the C code, comment out the line which defines GCC_M32_OPTION
    // asm () is documented in the C++ standard and supported by gcc.  Additional comments are documented around line 145, where GCC_M32_OPTION is defined.
    unsigned long temp_size = (unsigned long) size;
    FIND_POSITION_OF_MOST_SIGNIFICANT_BIT_IN_A_WORD(index_to_array, temp_size);
    if (NEXT_FREE_BLOCK(ptr) == NULL) { // There are no nodes after this node which will be deleted
        if (PREV_FREE_BLOCK(ptr) == NULL) { // The node being deleted is the only node on this segregated free list
            segregated_free_lists[index_to_array] = NULL;
        } else { // Set the previous node's next ptr to NULL
            SET_PTR(PTR_TO_NEXT_FREE_BLOCK(PREV_FREE_BLOCK(ptr)), NULL);
        }
    } else { // There are nodes after the node to be deleted
        if (PREV_FREE_BLOCK(ptr) == NULL) { // The node to be deleted is at the head of the segregated free list
            segregated_free_lists[index_to_array] = NEXT_FREE_BLOCK(ptr);
            SET_PTR(PTR_TO_PREV_FREE_BLOCK(NEXT_FREE_BLOCK(ptr)), NULL); // There is no PTR_TO_PREV_FREE_BLOCK for the node at the head of the segregated free list
        } else {
        	SET_PTR(PTR_TO_PREV_FREE_BLOCK(NEXT_FREE_BLOCK(ptr)), PREV_FREE_BLOCK(ptr));
            SET_PTR(PTR_TO_NEXT_FREE_BLOCK(PREV_FREE_BLOCK(ptr)), NEXT_FREE_BLOCK(ptr));
        }
    }
    return;
}

void *extend_heap(size_t requested_size)
{
    void *ptr;                   
    size_t adjusted_size;                // Adjusted size
    
    adjusted_size = ALIGN(requested_size);
    
    if ((ptr = mem_sbrk(adjusted_size)) == (void *)-1)
        return NULL;
#ifdef DEBUG
    top_of_heap = ptr + adjusted_size;
    /* printf("%s\n", __func__); */ mm_checkheap(__LINE__);
#endif
    // Set headers and footer 
    PUT(HDRP(ptr), PACK(adjusted_size, 0));
    PUT(FTRP(ptr), PACK(adjusted_size, 0));
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1));
    insert_node(ptr, adjusted_size);

    return coalesce(ptr);
}

void insert_node(void *ptr, size_t size_of_node) {
    unsigned long index_to_array = 0;
    void *search_ptr = ptr;
    void *ptr_to_node_to_take_insertion = NULL;
    
    // Select segregated free list. Each element in the segregated_free_lists points to a linked list.  Each element in the array is in the power of 2.
    unsigned long temp_size = (unsigned long) size_of_node;
	FIND_POSITION_OF_MOST_SIGNIFICANT_BIT_IN_A_WORD(index_to_array, temp_size);
    
    // Keep nodes in the ascending order of the size so that the search will find the best fit first
    search_ptr = segregated_free_lists[index_to_array];
    while ((search_ptr != NULL) && (size_of_node > GET_SIZE(HDRP(search_ptr)))) { // keep on looping to the next node until a node is greater than or equal to the size of the new node to insert
    	ptr_to_node_to_take_insertion = search_ptr;
        search_ptr = NEXT_FREE_BLOCK(search_ptr);
    }
    
    // Set the next and previous pointers to insert new node
    if (search_ptr == NULL) {  // reached the end of the segregated free list
        if (ptr_to_node_to_take_insertion == NULL) { // Empty segregated free list, add new node to segregated free list as the only node
            segregated_free_lists[index_to_array] = ptr;
            SET_PTR(PTR_TO_NEXT_FREE_BLOCK(ptr), NULL);
            SET_PTR(PTR_TO_PREV_FREE_BLOCK(ptr), NULL);
        } else {  // There is something in the segregated free list, but reached the end, add new node to the end of the list
            SET_PTR(PTR_TO_NEXT_FREE_BLOCK(ptr_to_node_to_take_insertion), ptr);
            SET_PTR(PTR_TO_NEXT_FREE_BLOCK(ptr), NULL);
            SET_PTR(PTR_TO_PREV_FREE_BLOCK(ptr), ptr_to_node_to_take_insertion);
        }
    } else { // Did not reach the end of the segregated free list
        if (ptr_to_node_to_take_insertion == NULL) {  // Insert new node to the head of segregated free list
        	segregated_free_lists[index_to_array] = ptr;
            SET_PTR(PTR_TO_NEXT_FREE_BLOCK(ptr), search_ptr);
            SET_PTR(PTR_TO_PREV_FREE_BLOCK(ptr), NULL); // The head of the segregated free list's PTR_TO_PREV_FREE_BLOCK is NULL
            SET_PTR(PTR_TO_PREV_FREE_BLOCK(search_ptr), ptr);
        } else {  // Insert new node to an non empty segregated free list, there is also a node before and after
        	SET_PTR(PTR_TO_NEXT_FREE_BLOCK(ptr), search_ptr);
            SET_PTR(PTR_TO_PREV_FREE_BLOCK(ptr), ptr_to_node_to_take_insertion);
            SET_PTR(PTR_TO_PREV_FREE_BLOCK(search_ptr), ptr);
            SET_PTR(PTR_TO_NEXT_FREE_BLOCK(ptr_to_node_to_take_insertion), ptr);
        }
    }
    return;
}

/*
 * mm_init - initialize the malloc package.
 * This is called before calling mm_malloc, mm_realloc, or mm_free,
 * such as allocating the initial heap area.
 * All global values should be initialized in this function.
 *
 * Return value : -1 if there was a problem, 0 otherwise.
 */
int mm_init(void)
{
    int index_to_array;
    
    // Initialize segregated list for free blocks
    for (index_to_array = 0; index_to_array < SEG_LIST_ARRAY_SIZE; index_to_array++) {
        segregated_free_lists[index_to_array] = NULL;
    }
    
    // Allocate memory for the initial empty heap 
    if ((long)(starting_addr_of_heap = mem_sbrk(4 * WSIZE)) == -1)
        return -1;
    
    PUT(starting_addr_of_heap, 0);                            /* Alignment padding to get the payload start at boundary of 8 */
    starting_addr_of_heap += WSIZE;
    PUT(starting_addr_of_heap, PACK(DSIZE, 1));            /* Prologue header */
    PUT(starting_addr_of_heap + WSIZE, PACK(DSIZE, 1));    /* Prologue footer */
    PUT(starting_addr_of_heap + (2 * WSIZE), PACK(0, 1));  /* Epilogue header for next block */
    
    if (extend_heap(INITIALIZATION_CHUNK_SIZE) == NULL)
        return -1;
    
    return 0;
}

/*
 * mm_free - The mm_free routine frees the block pointed to by ptr
 *
 * Return value : returns nothing
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    insert_node(ptr, size);
    coalesce(ptr);

    return;
}

/*
 * mm_malloc - Allocate a block. Incrementing the brk pointer if necessary
 *     Always allocate a block whose size is a multiple of the alignment.
 *
 * The mm_malloc routine returns a pointer to an allocated block payload.
 * 
 * Return value : Return the payload pointers that are aligned to 8 bytes.
 * 				  Return NULL if the requested size is 0 or if there are problem with extend heap.
 */
void *mm_malloc(size_t requested_size)
{
    size_t adjusted_size;      /* Adjusted block size */
    size_t extend_size;        /* Amount to extend heap if more memory is required */
    void *ptr = NULL;          /* Pointer */
    
    // Ignore size 0 cases
    if (requested_size == 0)
        return NULL;
    
    // Align block size
    if (requested_size <= DSIZE) {
        adjusted_size = 2 * DSIZE; // Minimum size, space for header, footer, next and previous free block
    } else {
        adjusted_size = ALIGN(requested_size+DSIZE);
    }
    
    int index_to_array = 0;
    unsigned long temp_size = (unsigned long) adjusted_size;
	FIND_POSITION_OF_MOST_SIGNIFICANT_BIT_IN_A_WORD(index_to_array, temp_size);

    while (index_to_array < SEG_LIST_ARRAY_SIZE) {
        if ((index_to_array == SEG_LIST_ARRAY_SIZE - 1) || ((segregated_free_lists[index_to_array] != NULL))) {
            ptr = segregated_free_lists[index_to_array];
            // Ignore blocks that are too small
            while ((ptr != NULL) && ((adjusted_size > GET_SIZE(HDRP(ptr))))) {
                ptr = NEXT_FREE_BLOCK(ptr);
            }
            if (ptr != NULL) break; // Found a block that fits
        }
        index_to_array++;
    }
    
    // if free block is not found, extend the heap
    if (ptr == NULL) {
        extend_size = MAX(adjusted_size, CHUNK_SIZE);
        if ((ptr = extend_heap(extend_size)) == NULL)
            return NULL;
    }
    
    // Place and divide block
    ptr = place(ptr, adjusted_size);
    
#ifdef DEBUG
    /* printf("%s\n", __func__); */ mm_checkheap(__LINE__);
#endif
    // Return pointer to newly allocated block 
    return ptr;
}

/*
 *
 * mm_realloc function returns a pointer to an allocated
 * memory of at least size bytes requested.  If necessary
 * copy data from old to new block.
 *
 */
void *mm_realloc(void *ptr, size_t requested_size)
{
    void *new_ptr = ptr;    // Pointer to be returned
    size_t original_block_size = GET_SIZE(HDRP(ptr));
    size_t new_size = requested_size; // Size of new block
    int extend_size;        // Size of the required heap extension, if it is required
    
    if (requested_size == 0)      // Ignore size 0 cases
        return NULL;
    
    // Align block size
    if (new_size <= DSIZE) {
        new_size = 2 * DSIZE; // Need space for the next and previous pointers of free block, footer and header for next block.  Each of them is Word size
    } else {
        new_size = ALIGN(requested_size + DSIZE);
    }
    
    /* Add overhead requirements (8) to block size.  A bigger size (e.g. 128) would work and would allocate more space to accommodate future reallocation request  */
    // Testing of the realloc trace files provide the same performance regardless of a small (8) or a larger (128) reallocation buffer size
    new_size += 8; // Add overhead. A bigger size does not seem to provide a performance or utilization improvement
    
    /* Allocate more space if the existing block is not large enough.  If there is enough space, just return the same block */
    if ((int)(original_block_size - new_size) < 0) { // Need to cast to int to have both negative and positive numbers from the subtraction
    	int need_to_malloc = 1; // flag to determine if malloc is required.
        /* Check if next block is a free block or the epilogue block */
        if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) { // Next block is free
        	size_t size_of_original_and_next_block = original_block_size + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
            if (new_size - size_of_original_and_next_block < 0) { // There is enough space in the next block.  No need to malloc and no need to extend heap. If there is not enough space, the need_to_malloc flag will remain as 1
                delete_node(NEXT_BLKP(ptr));
                // Do not split block.  It might be reallocated in the future.  It might help to split block if the requested size is small, but will improve utilization and performance for the trace files available for this assignment
                PUT(HDRP(ptr), PACK(size_of_original_and_next_block, 1)); // use all space available on the two blocks
                PUT(FTRP(ptr), PACK(size_of_original_and_next_block, 1));
            	need_to_malloc = 0;
#ifdef DEBUG
                printf("realloc with enough space %d in existing block\n", new_size);
                /* printf("%s\n", __func__); */ mm_checkheap(__LINE__);
#endif
            }
        }
        else if (!GET_SIZE(HDRP(NEXT_BLKP(ptr)))) {
    		// Next block is the epilogue block (last block at the top of the heap) so extend the heap.
            extend_size = MAX((new_size - original_block_size), CHUNK_SIZE);
#ifdef DEBUG
                printf("extending heap by %d\n", extend_size);
                /* printf("%s\n", __func__); */ mm_checkheap(__LINE__);
                printf("original %lu, requested %lu, new size %lu, extend %d\n", original_block_size, requested_size, new_size, extend_size);
#endif
            if (extend_heap(extend_size) == NULL)
                return NULL;
            need_to_malloc = 0;
            delete_node(NEXT_BLKP(ptr)); // delete the new node provided by the extend_heap because it is used now
            
            // Do not split block.  It might be reallocated in the future.  It might help to split block if the requested size is small, but will improve utilization and performance for the trace files available for this assignment
            PUT(HDRP(ptr), PACK(original_block_size + extend_size, 1));
            PUT(FTRP(ptr), PACK(original_block_size + extend_size, 1));

        }
    	if (need_to_malloc) {
            new_ptr = mm_malloc(new_size - DSIZE);
            memcpy(new_ptr, ptr, original_block_size); // MIN(size, new_size));
            mm_free(ptr);
#ifdef DEBUG
            printf("realloc with new malloc %d %d\n", new_size, original_block_size);
            /* printf("%s\n", __func__); */ mm_checkheap(__LINE__);
#endif
        }
    }
    return new_ptr;  // Return the pointer to the reallocated block
} // end of mm_realloc

void *place(void *ptr, size_t adjusted_size)
{
    size_t size_of_current_block = GET_SIZE(HDRP(ptr));
    size_t remainder = size_of_current_block - adjusted_size;
    void* next_block_ptr;

    delete_node(ptr);

    if (DSIZE * 4 > remainder) { // Too small to split block
        PUT(HDRP(ptr), PACK(size_of_current_block, 1));
        PUT(FTRP(ptr), PACK(size_of_current_block, 1));
    } else {
    	if (adjusted_size > 96) { // Split block. If size of block is not small, don't leave the block after it extendible.
    		PUT(HDRP(ptr), PACK(remainder, 0));
            PUT(FTRP(ptr), PACK(remainder, 0));
        	next_block_ptr = NEXT_BLKP(ptr);
        	PUT(HDRP(next_block_ptr), PACK(adjusted_size, 1));
            PUT(FTRP(next_block_ptr), PACK(adjusted_size, 1));
            insert_node(ptr, remainder);
            ptr = next_block_ptr;
    	} else {  // Split block.  If the size of the block is small, leave the space after it extendable.
    		PUT(HDRP(ptr), PACK(adjusted_size, 1));
    		PUT(FTRP(ptr), PACK(adjusted_size, 1));
    		next_block_ptr = NEXT_BLKP(ptr);
    		PUT(HDRP(next_block_ptr), PACK(remainder, 0));
    		PUT(FTRP(next_block_ptr), PACK(remainder, 0));
    		insert_node(next_block_ptr, remainder);
    	}
    }
    return ptr;
}

#ifdef DEBUG
void mm_checkheap(int lineno) {
    void *ptr;
    int number_of_free_blocks = 0;
    int number_of_free_blocks_in_seg_list = 0;

    /* Verify prologue */
    ptr = starting_addr_of_heap;      /* pointer to the start of the heap link list */
    if ((GET_SIZE(ptr) != DSIZE) || (GET_ALLOC(ptr) != 1)) {
        printf("Addr: %p - Prologue header error** \n", ptr);
    }
    ptr += WSIZE;
    if ((GET_SIZE(ptr) != DSIZE) || (GET_ALLOC(ptr) != 1)) {
        printf("Addr: %p - Prologue footer error** \n", ptr);
    }
    ptr += 2 * WSIZE; // set pointer to the next block

    /* Iterating through entire heap. Convoluted code checks that
     * we are not at the epilogue. Loops thr and checks epilogue block! */
    while (GET_SIZE(HDRP(ptr)) > 0) {
    	if (GET_SIZE(HDRP(ptr)) != GET_SIZE(FTRP(ptr))) {
	        printf("Addr: %p - Header and footer size do not match\n", ptr);
	    }
    	/* Check each block's address alignment */
    	if (ALIGN((size_t) ptr) != (size_t)ptr) {
    		printf("Addr: %p - Block Alignment Error** \n", ptr);
    	}
    	/* Each block's bounds check */
    	if ((ptr > top_of_heap) || (ptr < starting_addr_of_heap)) {
    		printf("Addr: %p - Not within heap, top: %p, start: %p\n", ptr, top_of_heap, starting_addr_of_heap);
    	}
	    /* Check Minimum size */
        if (GET_SIZE(HDRP(ptr)) < (2*DSIZE)) {
            printf("Addr: %p - ** Min Size Error ** \n", ptr);
        }
	    if (GET_ALLOC(HDRP(ptr)) != GET_ALLOC(FTRP(ptr))) {
    		printf("Addr: %p - ** Header and footer allocation flag do not match.\n", ptr);
    	}
    	/* Check coalescing: If alloc bit of current and next block is 0 */
        if (!(GET_ALLOC(HDRP(ptr)) && (!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))))) {
            printf("Addr: %p - ** Coalescing Error** \n", ptr);
        }
        /* Count number of free blocks */
        if (!(GET_ALLOC(HDRP(ptr))))
        	number_of_free_blocks ++;
        ptr = NEXT_BLKP(ptr);
    }

    /* Iterating through segregated free list */

    int index_to_array = 0;
    while (index_to_array < SEG_LIST_ARRAY_SIZE - 1) {
        void *seg_list_ptr = segregated_free_lists[index_to_array];
        int last_size = 0;
        int current_size = 0;
        while (seg_list_ptr != NULL) {
        	if ((seg_list_ptr > top_of_heap) || (seg_list_ptr < starting_addr_of_heap)) {
        		printf("Addr of free list: %p - Not within heap** \n", seg_list_ptr);
        	}
    	    if (GET_ALLOC(HDRP(seg_list_ptr))) {
        		printf("Addr of free list: %p - ** Header allocation flag is not free.\n", seg_list_ptr);
        	}
        	if (GET_ALLOC(FTRP(seg_list_ptr))) {
    			printf("Addr of free list: %p - ** footer allocation flag is not free.\n", seg_list_ptr);
    		}
    		current_size = GET_SIZE(HDRP(seg_list_ptr));
    		if (last_size > current_size ) {
    			printf("Addr of free list: %p - ** segregated free list is not sorted in assending order.\n", seg_list_ptr);
			}
			number_of_free_blocks_in_seg_list ++;
			seg_list_ptr = NEXT_FREE_BLOCK(seg_list_ptr);
        }
        index_to_array++;
    }
    if (number_of_free_blocks != number_of_free_blocks_in_seg_list) {
		printf("Number of free blocks (%d) and number of pointers in segregated free list (%d) are different at line %d.\n", number_of_free_blocks, number_of_free_blocks_in_seg_list, lineno);
		index_to_array = 0;
		while (index_to_array < SEG_LIST_ARRAY_SIZE - 1) {
	        void *seg_list_ptr = segregated_free_lists[index_to_array];
	        while (seg_list_ptr != NULL) {
	        	printf("seg list %d PTR: %p size: %d\n", index_to_array, seg_list_ptr, GET_SIZE(HDRP(seg_list_ptr)));
				seg_list_ptr = NEXT_FREE_BLOCK(seg_list_ptr);
	        }
	        index_to_array++;
	    }
	    ptr = starting_addr_of_heap + (3 * WSIZE);      /* pointer to the start of the heap link list */
	    while (GET_SIZE(HDRP(ptr)) > 0) {
	        if (!(GET_ALLOC(HDRP(ptr)))) {
	        	number_of_free_blocks ++;
	        	printf("free block PTR: %p size: %d\n", ptr, GET_SIZE(HDRP(ptr)));
	        }
	        ptr = NEXT_BLKP(ptr);
	    }
	}
//    else {
//    	printf("Number of free blocks (%d) and number of pointers in segregated free list (%d) are the same at line %d.\n", number_of_free_blocks, number_of_free_blocks_in_seg_list, lineno);
//	}
}
#endif
