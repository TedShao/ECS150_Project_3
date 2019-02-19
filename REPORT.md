# Project 3 Report

## Intro

In this project we designed the implementation of a semaphore and thread
protected storage. These implementations are found in sem.c and tps.c

## Semaphore

The Semaphore data structure had a queue to hold blocked threads and a count to
keep track of the avaible number of resources.

    struct semaphore {
        queue_t wait_queue;
        size_t count;
    };

### Create and Destroy

Upon calling *sem_create(size)*, the passed semaphore will be initialized to
have a count of size and the queue will be created. When *sem_destroy()* is called,
it will check to make sure the are no threads currently being blocked and then
destroy the queue and free the passed semaphore pointer.

### Up and Down

When *sem_down()* is called, it will attempt to grab a resource. If the
semaphore count is 0, the calling thread will be blocked and enqueued into the
wait_queue. The thread can be unblocked and dequeued by another thread calling
*sem_up()*. 

## TPS

Thread protected storage was implemented by using two structs, and a queue
global to the library.

The first structure is a page_t that holds the address of the protected memory
and a ref_count to know how many threads are using this storage space. This
ref_count is important for the copy on write functionality.

    typedef struct page {
	    char *ptr;
	    unsigned int ref_count;
    } *page_t;

The second structure is a tps_t that holds a thread ID and a pointer to the
previously described page struct. This allows multiple threads to point to the
same memory after a clone.

    typedef struct tps {
	    pthread_t tid;
	    page_t page;
    } *tps_t;

The global queue held all the TPSs created.

    queue_t tps_queue





