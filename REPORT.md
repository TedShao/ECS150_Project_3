# Project 3 Report

In this project we designed the implementation of a semaphore and thread
protected storage. These implementations are found in sem.c and tps.c. These
libraries are tested with the given test programs provided and also with an
additional test program called testsuite.x located in the test folder.

# Semaphore

The Semaphore data structure had a queue to hold blocked threads and a count to
keep track of the avaible number of resources.

    struct semaphore {
        queue_t wait_queue;
        size_t count;
    };

## Create and Destroy

Upon calling *sem_create(size)*, the passed semaphore will be initialized to
have a count of size and the queue will be created. When *sem_destroy()* is called,
it will check to make sure the are no threads currently being blocked and then
destroy the queue and free the passed semaphore pointer.

## Up and Down

When *sem_down()* is called, it will attempt to grab a resource. If the
semaphore count is 0, the calling thread will be blocked and enqueued into the
wait_queue. The thread can be unblocked and dequeued by another thread calling
*sem_up()*. 

# TPS

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

Since we are provided a queue with and an API that allows us to search for
specific items, we used a queue to store all of our TPS data. This global queue
holds all the TPSs created.

    queue_t tps_queue

## Tps Initialization
When the user calls tps_init for the first time, a static variable to the
function is set to detect if the function is called again. In that case the
function will return -1. In addition if a value greater than 0 is passed to
tps_init(), a signal handler is registered to the SIGSEGV and SIGBUS signals.
This signal handler will analyze what caused the error. If the error is due to
an attmpeted outside access of a tps, a custom error message will be printed to
std error.

## TPS Create and Destroy
When the user calls tps_create, the tps_queue is checked to make sure a tps
isn't already created for the current thread. If one already exists, the function
returns -1, otherwise a new **tps_t** is created containing...
- a page holding the allocated memory of size TPS_SIZE and ref_count set to 1.
  The memory allocated here is protected and cannot be read or written to. In
  tps_read and write the access writes are temporarily modified.
- a tid holding the current pthread tid
    
This new tps is then enqueued into the tps_queue. 

When the user calls tps_destroy, tps_queue is checked to make sure the thread
actually has a tps. If the thread has a tps, the function then checks the
ref_count to the tps page. If only one tps is using this page, the page memory
is freed, otherwise, the ref_count is decremented. In both cases, the tps stuct
is freed and deleted from the tps_queue.

## TPS Read and Write
In both tps_read and tps_write, an initial check is performed to make sure the
buffer is not null, the offset and length are in the bounds of the TPS, and the
current thread has a tps in tps_queue. After these checks, the tps is found in
the queue. The ptr in referenced page has the address of the allocated memory
and is then temporarily given read rights. The proper data is then stored into
the buffer using memcpy. If this is a cloned page that has yet to write to the
tps, this function will not copy the page.

The implementation for tps_write is more complicated since we need the copy on
write functionality. Once the function performs the initial checks, the ref
count of the page is checked. If ref_count is 1, the function will simply use
mcpy to write buffer to the page. If ref_count is greater than 1, a new page is
created and the current page of the tps is copied into the new page. The current
page ref_count is decremented and now the tps references the new copied page.
Then this new page is written to with what is contained in buffer using memcpy.

## TPS Clone
When the user clones a tps, a new tps is created, but a new page is not. The new
tps simply points to the same page as the cloned tps and increments ref_count.
Only upon calling tps_write from any of the threads referencing this page will a new
page be created and the data copied.

# Testing

In addition to using the provided tests, we created our own *testsuite* to
extensively test our implementation of TPS. The testing is broken into 3
different types.
- Protection
- Error
- Default

## Protection

    ./testsuite.x protection

This tests the protection of the tps and the proper calling of the custom
segvhandler. This test intentionally causes a segmentation fault by attempting
to dereference memory inside a tps outside the library. This function will stop
the program and therefore is separate from the other tests.

## Error

    ./testsuite.x error

This tests all the possible scenarios when the API functions should return with
-1. This is done by intentionly passing bad parameters or recalling functions
like tps_init, tps_create, etc.

## Default

    ./testsuite.x

This tests the proper functionality of the API, ensuring reading, writing, and
cloning work.

### read_write_test

This tests reading and writing to a tps. This includes reading and writing using
offsets and lengths different than 0 and TPS_SIZE.

### clone_test

This test is more involved than the others and checks multiple features of
cloning.

- Proper cloning of a tps
- New memory is not allocated on tps_create or tps_read
- Writing to a cloned tps does not modify the original or vice versa
- New memory is allocated only on a call to write

This test is performed by using semaphores to precisely switch between two
threads and performing cloning, reading and writing in each one and checking for
the expected values. In addition the mmap wrapper is used to check for when new
memory is allocated.








