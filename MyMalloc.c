/*
 * CS252: MyMalloc Project
 *
 * The current implementation gets memory from the OS
 * every time memory is requested and never frees memory.
 *
 * You will implement the allocator as indicated in the handout,
 * as well as the deallocator.
 *
 * You will also need to add the necessary locking mechanisms to
 * support multi-threaded programs.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "MyMalloc.h"

static pthread_mutex_t mutex;

const int arenaSize = 2097152;

void increaseMallocCalls()  { _mallocCalls++; }

void increaseReallocCalls() { _reallocCalls++; }

void increaseCallocCalls()  { _callocCalls++; }

void increaseFreeCalls()    { _freeCalls++; }

extern void atExitHandlerInC()
{
    atExitHandler();
}
/* 
 * Initial setup of allocator. First chunk is retrieved from the OS,
 * and the fence posts and freeList are initialized.
 */
void initialize()
{
    // Environment var VERBOSE prints stats at end and turns on debugging
    // Default is on
    _verbose = 1;
    const char *envverbose = getenv("MALLOCVERBOSE");
    if (envverbose && !strcmp(envverbose, "NO")) {
        _verbose = 0;
    }

    pthread_mutex_init(&mutex, NULL);
    void *_mem = getMemoryFromOS(arenaSize);
    // In verbose mode register also printing statistics at exit
    atexit(atExitHandlerInC);

    // establish fence posts
    ObjectHeader * fencePostHead = (ObjectHeader *)_mem;
    fencePostHead->_allocated = 1;
    fencePostHead->_objectSize = 0;

    char *temp = (char *)_mem + arenaSize - sizeof(ObjectHeader);
    ObjectHeader * fencePostFoot = (ObjectHeader *)temp;
    fencePostFoot->_allocated = 1;
    fencePostFoot->_objectSize = 0;

    // Set up the sentinel as the start of the freeList
    _freeList = &_freeListSentinel;

    // Initialize the list to point to the _mem
    temp = (char *)_mem + sizeof(ObjectHeader);
    ObjectHeader *currentHeader = (ObjectHeader *)temp;
    currentHeader->_objectSize = arenaSize - (2*sizeof(ObjectHeader)); // ~2MB
    currentHeader->_leftObjectSize = 0;
    currentHeader->_allocated = 0;
    currentHeader->_listNext = _freeList;
    currentHeader->_listPrev = _freeList;
    _freeList->_listNext = currentHeader;
    _freeList->_listPrev = currentHeader;

    // Set the start of the allocated memory
    _memStart = (char *)currentHeader;

    _initialized = 1;
}

/* 
 * TODO: In allocateObject you will handle retrieving new memory for the malloc
 * request. The current implementation simply pulls from the OS for every
 * request.
 *
 * @param: amount of memory requested
 * @return: pointer to start of useable memory
 */
void * allocateObject(size_t size)
{
    // Make sure that allocator is initialized
    if (!_initialized)
        initialize();

    /* Add the ObjectHeader to the size and round the total size up to a 
     * multiple of 8 bytes for alignment.
     */
    size_t roundedSize = (size + sizeof(ObjectHeader) + 7) & ~7;
    if (!_freeList->_listNext)
        _freeList->_listNext = (ObjectHeader*)getMemoryFromOS(arenaSize);
    int real_size=roundedSize ; 
	int extra_space =8+sizeof(ObjectHeader);//This is to check if we can split the memory chunk into two pieces.
    ObjectHeader* curHead = _freeList->_listNext;
    //printf("%d real size\n",real_size); 
	int isReplaced =0;//If this is 0 then we couln't find a free space in freeList
	ObjectHeader *ptr;
	//Repeat until we find a block to place our object.
	while(isReplaced==0){
    	ptr = _freeList->_listNext; //Skip dummy header, pointer points to first header now.
    	while (ptr != _freeList) {
		//		printf("%zu size \n",ptr->_objectSize);
				int empty_space = ptr->_objectSize;//Available space.
				//First check if we have enough space for both real_size and extra size also check if this block is allocated.
 				if(empty_space >= real_size+extra_space && ptr->_allocated ==0){
					isReplaced =1;//We found a place to replace our object.
					long base=(long) ptr+(long)ptr->_objectSize-real_size; //The baseoff for the start of higher memory.
         			ObjectHeader *new_chunk =  (ObjectHeader *) (base); //Initialize our chunk using the baseoff.
					new_chunk->_objectSize =real_size; 
 					new_chunk->_allocated=1;//Since we are replacing our object to this chunk.
					new_chunk->_leftObjectSize = ptr->_objectSize-real_size; //leftObject is ptr and we are using real_size of its memory so we need to subtract it.
					new_chunk->_listNext=ptr->_listNext; //Adjust pointers.
          			new_chunk->_listNext->_listPrev=new_chunk;//Adjust pointers.
					new_chunk->_listPrev=ptr;//Adjust pointers.
          			ptr->_objectSize = ptr->_objectSize-real_size; //Adjust size
					ptr->_listNext=new_chunk; //Adjust pointers
					curHead=ptr->_listNext;//This is to be returned.
					break;
				}
				//Second check if we can fit our object exactly to current block and it shouldn't be allocated.
				else if(empty_space >=real_size && ptr->_allocated==0){
					isReplaced= 1;
					ptr->_allocated =1; //We will use this part, so make allocated 1.
  					curHead= ptr;// This is to be returned.
					break;
      	}
        ptr = ptr->_listNext;//At each step go to the next header.
    }
    //If we couldn't find any block large enough then request 2MB from OS.
		if(isReplaced ==0){
			ptr= ptr->_listPrev;// ptr was pointing to dummy header so go 1 back.
			ObjectHeader *currentHeader = getMemoryFromOS(arenaSize) ; //request memory
    	// establish fence posts
		    ObjectHeader * fencePostHead = (ObjectHeader *)currentHeader; //Fenceposts
		    fencePostHead->_allocated = 1;
		    fencePostHead->_objectSize = 0;
		
		    char *temp = (char *)currentHeader+ arenaSize - sizeof(ObjectHeader); 
		    ObjectHeader * fencePostFoot = (ObjectHeader *)temp; //Fenceposts
		    fencePostFoot->_allocated = 1;
		    fencePostFoot->_objectSize = 0;

    		temp = (char *)currentHeader+ sizeof(ObjectHeader); //Start address of our actual header after dummy header.
			currentHeader =(ObjectHeader *) temp; //initialize.
    		currentHeader->_objectSize = arenaSize - (2*sizeof(ObjectHeader)); // ~2MB
    		currentHeader->_leftObjectSize = ptr->_objectSize; //ptr is the previous/left object.
 		  	currentHeader->_allocated = 0; //Isnt allocated yet.
    		currentHeader->_listNext = _freeList; //adjust pointers.
   	  		currentHeader->_listPrev = ptr; //adjust pointers
			ptr->_listNext =fencePostHead; //adjust pointers.
			fencePostHead->_listNext=currentHeader; //adjust pointers.
		}
		//Assumpton: After adding 2MB to free list it is for sure that we can find a place.
		}
	
    // Naively get memory from the OS every time
   // void *_mem = getMemoryFromOS(arenaSize); 

    // Store the size in the header
    ObjectHeader *o = curHead; //curHead holds the start address of header.
    o->_objectSize = roundedSize;

    pthread_mutex_unlock(&mutex);

    // Return a pointer to useable memory
    return (void *)((char *)o + sizeof(ObjectHeader)); //Go one header forward so that usable memory is returned.
}

/* 
 * TODO: In freeObject you will implement returning memory back to the free
 * list, and coalescing the block with surrounding free blocks if possible.
 *
 * @param: pointer to the beginning of the block to be returned
 * Note: ptr points to beginning of useable memory, not the block's header
 */
void freeObject(void *ptr)
{
    // Add your implementation here
    	ObjectHeader* head = (ObjectHeader*)(ptr - sizeof(ObjectHeader)); //head holds the start address of header.
		ObjectHeader *left=head->_listPrev; //previous header
		ObjectHeader *right=head->_listNext;// next header
		ObjectHeader *toReturn; //to be returned
		int is_left_free= 0; //if the left neighbor is free then this is 1.
		if(left->_allocated==0 && left->_objectSize !=0) is_left_free=1;
		int is_right_free= 0; //If the right neighbor is free then this is 1.
		if(right->_allocated==0 && right->_objectSize != 0) is_right_free=1;
		//Both neighbors are free combine them.
		if(is_right_free==1 && is_left_free==1){
			left->_objectSize = left->_objectSize+head->_objectSize+right->_objectSize; //size will be the total of three.
			left->_listNext=right->_listNext; //Adjust pointers..
			right->_listNext->_listPrev=left; //Adjust pointers..
			toReturn =left; 
		}//Right neighbor is free.
		else if(is_right_free==1){
			head->_objectSize= head->_objectSize+right->_objectSize; //size will be the total of two
			head->_listNext=right->_listNext;//adjust headers
			right->_listNext->_listPrev=head;//adjust headers
			toReturn =head;
		}//Left neighbor is free.
		else if(is_left_free== 1){ 
      		left->_objectSize =left->_objectSize+head->_objectSize; //Size will be the total of two.
			left->_listNext=right; //adjust headers.
			right->_listPrev=left; //adjust headers.
     		toReturn=left; 
		}//No neighbor is free simpply deallocate it.
		else {
			head->_allocated=0;
			toReturn =head;
		}
    return (void )((char *)toReturn + sizeof(ObjectHeader)); //Add one header size so that the returned address is usable memory.
}

/* 
 * Prints the current state of the heap.
 */
void print()
{
    printf("\n-------------------\n");

    printf("HeapSize:\t%zd bytes\n", _heapSize );
    printf("# mallocs:\t%d\n", _mallocCalls );
    printf("# reallocs:\t%d\n", _reallocCalls );
    printf("# callocs:\t%d\n", _callocCalls );
    printf("# frees:\t%d\n", _freeCalls );

    printf("\n-------------------\n");
}

/* 
 * Prints the current state of the freeList
 */
void print_list() {
    printf("FreeList: ");
    if (!_initialized) 
        initialize();

    ObjectHeader * ptr = _freeList->_listNext;
    while (ptr != _freeList) {
				if(ptr->_allocated==1){
					ptr=ptr->_listNext;
					continue;
					} 
        long offset = (long)ptr - (long)_memStart;
        printf("[offset:%ld,size:%zd]", offset, ptr->_objectSize);
        ptr = ptr->_listNext;
        if (ptr != NULL)
            printf("->");
    }
    printf("\n");
}

/* 
 * This function employs the actual system call, sbrk, that retrieves memory
 * from the OS.
 *
 * @param: the chunk size that is requested from the OS
 * @return: pointer to the beginning of the chunk retrieved from the OS
 */
void * getMemoryFromOS(size_t size)
{
    _heapSize += size;

    // Use sbrk() to get memory from OS
    void *_mem = sbrk(size);

    // if the list hasn't been initialized, initialize memStart to mem
    if (!_initialized)
        _memStart = _mem;

    return _mem;
}

void atExitHandler()
{
    // Print statistics when exit
    if (_verbose)
        print();
}

/*
 * C interface
 */

extern void * malloc(size_t size)
{
    pthread_mutex_lock(&mutex);
    increaseMallocCalls();

    return allocateObject(size);
}

extern void free(void *ptr)
{
    pthread_mutex_lock(&mutex);
    increaseFreeCalls();

    if (ptr == 0) {
        // No object to free
        pthread_mutex_unlock(&mutex);
        return;
    }

    freeObject(ptr);
}

extern void * realloc(void *ptr, size_t size)
{
    pthread_mutex_lock(&mutex);
    increaseReallocCalls();

    // Allocate new object
    void *newptr = allocateObject(size);

    // Copy old object only if ptr != 0
    if (ptr != 0) {

        // copy only the minimum number of bytes
        ObjectHeader* hdr = (ObjectHeader *)((char *) ptr - sizeof(ObjectHeader));
        size_t sizeToCopy =  hdr->_objectSize;
        if (sizeToCopy > size)
            sizeToCopy = size;

        memcpy(newptr, ptr, sizeToCopy);

        //Free old object
        freeObject(ptr);
    }

    return newptr;
}

extern void * calloc(size_t nelem, size_t elsize)
{
    pthread_mutex_lock(&mutex);
    increaseCallocCalls();

    // calloc allocates and initializes
    size_t size = nelem *elsize;

    void *ptr = allocateObject(size);

    if (ptr) {
        // No error; initialize chunk with 0s
        memset(ptr, 0, size);
    }

    return ptr;
}

