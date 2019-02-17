#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"

/***** Structs *****/
typedef struct tps {
	pthread_t tid;
	char *ptr;
} *tps_t;

/***** Global Variables *****/
queue_t tps_queue = NULL;

/***** Internal Functions *****/
/* Enqueue item, if queue is null, create queue */
void tps_queue_enqueue_check(queue_t *queue)
{
	/* If the queue is null, allocate a new queue */
	if (*queue == NULL){
		*queue = queue_create();
	} 
	
	pthread_t tid = pthread_self();
	queue_enqueue(*queue, (void*)tid);

	return;
}

/* Dequeue item, if queue is empty, deallocate and set to NULL */
void uthread_queue_dequeue_check(queue_t *queue, void **data)
{
	/* dequeue item */
	queue_dequeue(*queue, data); 

	/* If the queue is empty, destroy and set to NULL */
	if (queue_length(*queue) == 0){
		queue_destroy(*queue);
		*queue = NULL;
	}

	return;
}

/* Delete item, if queue is empty, deallocate and set to NULL */
void uthread_queue_delete_check(queue_t *queue, void *data)
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
/* Find TID in a queue */
int find_tid(void *data, void *arg)
{
    tps_t tps = (tps_t)data;
    pthread_t tid = (pthread_t)arg;

    if (tps->tid == tid) {
        return 1;
    }

    return 0;
}


/***** API Definitions *****/
int tps_init(int segv)
{
	static int initialized = 0;
	
	if (initialized) {
		return -1;
	}

	initialized = 1;

	return 0;
}

int tps_create(void)
{	
	tps_t new_tps;
	pthread_t tid;
	void *void_ptr;

	tid = pthread_self();

	enter_critical_section();

	/* Check if tps already created */
	if (queue_iterate(tps_queue, find_tid, (void*)tid, NULL) == 1) {
		exit_critical_section();
		return -1;
	}

	/* Allocate memory and check for proper allocation */
	void_ptr = mmap(NULL, TPS_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (void_ptr == MAP_FAILED) {
		exit_critical_section();
		return -1;
	}
	
	/* Create new tps struct */
	new_tps = (tps_t) malloc(sizeof(struct tps));
	new_tps->ptr = (char*) void_ptr;
	new_tps->tid = tid;

	queue_enqueue(tps_queue, (void*) new_tps);

	exit_critical_section();

	return 0;
}

int tps_destroy(void)
{
	tps_t del_tps;
	pthread_t tid;
	void *void_ptr;

	tid = pthread_self();

	enter_critical_section();

	/* Check if tid has allocated tps */
	if (queue_iterate(tps_queue, find_tid, (void*)tid, (void**)&void_ptr) == 0) {
		exit_critical_section();
		return -1;
	}

	/* Unmap memory and free struct data */
	del_tps = (tps_t)void_ptr;
	munmap(del_tps->ptr, TPS_SIZE);
	free(del_tps);

	/* Delete tps struct from queue */
	queue_delete(tps_queue, (void*)del_tps);

	exit_critical_section();

	return 0;
}

int tps_read(size_t offset, size_t length, char *buffer)
{
	tps_t access_tps;
	pthread_t tid;
	void *void_ptr;

	tid = pthread_self();

	enter_critical_section();

	/* 
	 * Check for:
	 * -buffer is NULL
	 * -out of bounds
	 * -nonexistant tps
	 */
	if (buffer == NULL) {
		exit_critical_section();
		return -1;
	} else if ((offset + length) >= TPS_SIZE) {
		exit_critical_section();
		return -1;
	} else if (queue_iterate(tps_queue, find_tid, (void*)tid, (void**)&void_ptr) == 0) {
		exit_critical_section();
		return -1;
	}

	access_tps = (tps_t) void_ptr;
	memcpy(buffer, (void*)(access_tps->ptr + offset), length);

	exit_critical_section();
	return 0;
}

int tps_write(size_t offset, size_t length, char *buffer)
{
	tps_t access_tps;
	pthread_t tid;
	void *void_ptr;

	tid = pthread_self();

	enter_critical_section();

	/* 
	 * Check for:
	 * -buffer is NULL
	 * -out of bounds
	 * -nonexistant tps
	 */
	if (buffer == NULL) {
		exit_critical_section();
		return -1;
	} else if ((offset + length) >= TPS_SIZE) {
		exit_critical_section();
		return -1;
	} else if (queue_iterate(tps_queue, find_tid, (void*)tid, (void**)&void_ptr) == 0) {
		exit_critical_section();
		return -1;
	}

	access_tps = (tps_t) void_ptr;
	memcpy((void*)(access_tps->ptr + offset), buffer, length);

	exit_critical_section();
	return 0;
}

int tps_clone(pthread_t tid)
{
	tps_t cpy_tps;
	tps_t new_tps;
	pthread_t current_tid;
	void *void_ptr;

	current_tid = pthread_self();

	enter_critical_section();

	/* Check if current tid already has tps */
	if (queue_iterate(tps_queue, find_tid, (void*)current_tid, NULL) != 0) {
		exit_critical_section();
		return -1;
	} 
	/* Check if passed tid has tps */
	if (queue_iterate(tps_queue, find_tid, (void*)tid, (void**)&void_ptr) == 0) {
		exit_critical_section();
		return -1;
	}
	
	/* Get the tps clone */
	cpy_tps = (tps_t) void_ptr;

	/* Allocate memory and check for proper allocation */
	void_ptr = mmap(NULL, TPS_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (void_ptr == MAP_FAILED) {
		exit_critical_section();
		return -1;
	}
	
	/* Create new tps struct */
	new_tps = (tps_t) malloc(sizeof(struct tps));
	new_tps->tid = tid;
	new_tps->ptr = (char*) void_ptr;

	/* Copy memory */
	memcpy((new_tps->ptr), (cpy_tps->ptr), TPS_SIZE);

	/* Enqueue the new tps */
	queue_enqueue(tps_queue, (void*) new_tps);

	exit_critical_section();
	return 0;
}

