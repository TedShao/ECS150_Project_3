#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stddef.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"

/***** Data Structures *****/
typedef struct page {
	char *ptr;
	unsigned int ref_count;
} *page_t;

typedef struct tps {
	pthread_t tid;
	page_t page;
} *tps_t;

/***** Global Variables *****/
static queue_t tps_queue = NULL;

/***** Internal Functions *****/
/* Enqueue item, if queue is null, create queue */
static void tps_queue_enqueue_check(queue_t *queue, void *data)
{
	/* If the queue is null, allocate a new queue */
	if (*queue == NULL){
		*queue = queue_create();
	} 
	
	queue_enqueue(*queue, data);

	return;
}

/* Delete item, if queue is empty, deallocate and set to NULL */
static void tps_queue_delete_check(queue_t *queue, void *data)
{
	/* delete item */
	queue_delete(*queue, data); 

	/* If the queue is empty, destroy and set to NULL */
	if (queue_length(*queue) == 0){
		queue_destroy(*queue);
		*queue = NULL;
	}

	return;
}

/* Find tps for associated TID */
static int find_tid(void *data, void *arg)
{
    tps_t tps = (tps_t)data;
    pthread_t tid = (pthread_t)arg;

    if (tps->tid == tid) {
        return 1;
    }

    return 0;
}
/* Find tps for associated pointer */
static int find_ptr(void *data, void *arg)
{
    tps_t tps = (tps_t)data;

    if ((void*)tps->page->ptr == arg) {
        return 1;
    }

    return 0;
}
/* Handler for seg fault on tps access */
static void segv_handler(int sig, siginfo_t *si, void *context)
{
    /*
     * Get the address corresponding to the beginning of the page where the
     * fault occurred
     */
    void *p_fault = (void*)((uintptr_t)si->si_addr & ~(TPS_SIZE - 1));
	void *void_ptr = NULL;

    /*
     * Iterate through all the TPS areas and find if p_fault matches one of them
     */
	queue_iterate(tps_queue, find_ptr, p_fault, &void_ptr);
    if (void_ptr != NULL) {
        /* Printf the following error message */
        fprintf(stderr, "TPS protection error!\n");
	}

    /* In any case, restore the default signal handlers */
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    /* And transmit the signal again in order to cause the program to crash */
    raise(sig);
}


/***** API Definitions *****/
int tps_init(int segv)
{
	static int initialized = 0;
	
	if (initialized) {
		return -1;
	}

	initialized = 1;

	if (segv) {
		struct sigaction sa;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO;
		sa.sa_sigaction = segv_handler;
		sigaction(SIGBUS, &sa, NULL);
		sigaction(SIGSEGV, &sa, NULL);
    }

	return 0;
}

int tps_create(void)
{	
	tps_t new_tps = NULL;
	void *void_ptr = NULL;
	pthread_t tid;

	tid = pthread_self();

	enter_critical_section();

	/* Check if tps already created */
	queue_iterate(tps_queue, find_tid, (void*)tid, &void_ptr);
	if (void_ptr != NULL) {
		exit_critical_section();
		return -1;
	}

	/* Allocate memory and check for proper allocation */
	void_ptr = mmap(NULL, TPS_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (void_ptr == MAP_FAILED) {
		exit_critical_section();
		return -1;
	}
	
	/* Create new tps struct */
	new_tps       = (tps_t)  malloc(sizeof(struct tps));
	new_tps->page = (page_t) malloc(sizeof(struct page));

	new_tps->page->ptr       = (char*) void_ptr;
	new_tps->page->ref_count = 1;
	new_tps->tid             = tid;

	tps_queue_enqueue_check(&tps_queue, (void*) new_tps);

	exit_critical_section();
	return 0;
}

int tps_destroy(void)
{
	tps_t del_tps = NULL;
	void *void_ptr = NULL;
	pthread_t tid;

	tid = pthread_self();

	enter_critical_section();

	/* Check if tid has allocated tps */
	queue_iterate(tps_queue, find_tid, (void*)tid, (void**)&void_ptr);
	if (void_ptr == NULL) {
		exit_critical_section();
		return -1;
	}

	del_tps = (tps_t)void_ptr;

	/* 
	 * Check page reference count
	 * -If 1: Unmap memory, free struct data, and dequeue
	 * -else: decrement ref_count
	 */
	if (del_tps->page->ref_count == 1) {
		mprotect(del_tps->page->ptr, TPS_SIZE, PROT_READ | PROT_WRITE);
		munmap(del_tps->page->ptr, TPS_SIZE);
		free(del_tps->page);
		free(del_tps);
		tps_queue_delete_check(&tps_queue, (void*)del_tps);
	} else {
		del_tps->page->ref_count -= 1;
	}

	exit_critical_section();
	return 0;
}

int tps_read(size_t offset, size_t length, char *buffer)
{
	tps_t access_tps = NULL;
	void *void_ptr = NULL;
	pthread_t tid;

	tid = pthread_self();

	enter_critical_section();

	/* 
	 * Check for:
	 * -buffer is NULL
	 * -out of bounds
	 */
	if (buffer == NULL) {
		exit_critical_section();
		return -1;
	} else if (offset >= TPS_SIZE) {
		exit_critical_section();
		return -1;
	} else if ((offset + length) > TPS_SIZE) {
		exit_critical_section();
		return -1;
	} 

	/* Check for tps for current tid */
	queue_iterate(tps_queue, find_tid, (void*)tid, &void_ptr);
	if ( void_ptr == NULL) {
		exit_critical_section();
		return -1;
	}

	access_tps = (tps_t) void_ptr;

	/* Allow temporary read access */
	mprotect(access_tps->page->ptr, TPS_SIZE, PROT_READ);
	memcpy(buffer, (void*)(access_tps->page->ptr + offset), length);
	mprotect(access_tps->page->ptr, TPS_SIZE, PROT_NONE);

	exit_critical_section();
	return 0;
}

int tps_write(size_t offset, size_t length, char *buffer)
{
	tps_t access_tps = NULL;
	void *void_ptr = NULL;
	pthread_t tid;

	tid = pthread_self();

	enter_critical_section();

	/* 
	 * Check for:
	 * -buffer is NULL
	 * -out of bounds
	 */
	if (buffer == NULL) {
		exit_critical_section();
		return -1;
	} else if (offset >= TPS_SIZE) {
		exit_critical_section();
		return -1;
	} else if ((offset + length) > TPS_SIZE) {
		exit_critical_section();
		return -1;
	} 

	/* Check for tps for current tid */
	queue_iterate(tps_queue, find_tid, (void*)tid, (void**)&void_ptr);
	if (void_ptr == NULL) {
		exit_critical_section();
		return -1;
	}

	access_tps = (tps_t) void_ptr;

	/* Copy on Write if necessary */
	if (access_tps->page->ref_count == 1) {
		/* Allow temporary write access */
		mprotect(access_tps->page->ptr, TPS_SIZE, PROT_READ | PROT_WRITE);
		memcpy((void*)(access_tps->page->ptr + offset), buffer, length);
		mprotect(access_tps->page->ptr, TPS_SIZE, PROT_NONE);
	} else {
		page_t old_page;

		/* Save shared page into variable and decrement ref_count */
		old_page = access_tps->page;
		old_page->ref_count -= 1;

		/* Allocate memory and check for proper allocation */
		void_ptr = mmap(NULL, TPS_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (void_ptr == MAP_FAILED) {
			exit_critical_section();
			return -1;
		}

		/* Create new page */
		access_tps->page = (page_t) malloc(sizeof(struct page));
		access_tps->page->ref_count = 1;
		access_tps->page->ptr       = (char*) void_ptr;
		access_tps->tid             = tid;

		/* Unprotect memory */
		mprotect(access_tps->page->ptr, TPS_SIZE, PROT_READ | PROT_WRITE);
		mprotect(old_page->ptr, TPS_SIZE, PROT_READ);

		/* Copy memory and write buffer */
		memcpy((access_tps->page->ptr), (old_page->ptr), TPS_SIZE);
		memcpy((void*)(access_tps->page->ptr + offset), buffer, length);

		/* Protect memory */
		mprotect(access_tps->page->ptr, TPS_SIZE, PROT_NONE);
		mprotect(old_page->ptr, TPS_SIZE, PROT_NONE);
	}

	exit_critical_section();
	return 0;
}

int tps_clone(pthread_t tid)
{
	void *void_ptr = NULL;
	tps_t cpy_tps = NULL;
	tps_t new_tps = NULL;
	pthread_t current_tid;
	
	current_tid = pthread_self();

	enter_critical_section();

	/* Check if current tid already has tps */
	queue_iterate(tps_queue, find_tid, (void*)current_tid, (void**)&void_ptr);
	if (void_ptr != NULL) {
		exit_critical_section();
		return -1;
	} 

	/* Check if passed tid has tps */
	void_ptr = NULL;
	queue_iterate(tps_queue, find_tid, (void*)tid, (void**)&void_ptr);
	if (void_ptr == NULL) {
		exit_critical_section();
		return -1;
	}
	
	/* Get the tps clone */
	cpy_tps = (tps_t) void_ptr;

	/* Create new tps*/
	new_tps = (tps_t) malloc(sizeof(struct tps));
	new_tps->tid = current_tid;

	/* New tps will point to exisiting page struct and increment ref_count */
	new_tps->page = cpy_tps->page;
	new_tps->page->ref_count += 1;

	/* Enqueue the new tps */
	tps_queue_enqueue_check(&tps_queue, (void*) new_tps);

	exit_critical_section();
	return 0;
}

