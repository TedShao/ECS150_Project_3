#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>

#include "queue.h"
#include "sem.h"
#include "thread.h"

struct semaphore {
	queue_t wait_queue;
	size_t count;
};

sem_t sem_create(size_t count)
{
	sem_t new_sem;

	new_sem = (sem_t) malloc(sizeof(struct semaphore));

	new_sem->wait_queue = queue_create();
	new_sem->count = count;

	return new_sem;
}

int sem_destroy(sem_t sem)
{
	/* Check for NULL sem or non-empty wait_queue */
	if (sem == NULL) {
		return -1;
	} else if (queue_destroy(sem->wait_queue) == -1) {
		return -1;
	}

	free(sem);

	return 0;
}

int sem_down(sem_t sem)
{
	pthread_t tid;

	/* Check for NULL sem */
	if (sem == NULL) {
		return -1;
	}

	enter_critical_section();

	if (sem->count == 0) {
		tid = pthread_self();
		queue_enqueue(sem->wait_queue, (void*)tid);
		thread_block();
	}

	sem->count -= 1;

	exit_critical_section();

	return 0;
}

int sem_up(sem_t sem)
{
	pthread_t tid;
	void *ptr;

	/* Check for NULL sem */
	if (sem == NULL) {
		return -1;
	}

	enter_critical_section();

	sem->count += 1;

	if (queue_dequeue(sem->wait_queue, (void**)&ptr) != -1) {
		tid = (pthread_t) ptr;
		thread_unblock(tid);
	}

	exit_critical_section();

	return 0;
}

int sem_getvalue(sem_t sem, int *sval)
{
	/* Check for NULL sem */
	if (sem == NULL) {
		return -1;
	}

	if (sem->count > 0) {
		*sval = sem->count;
	} else {
		*sval = (-1) * queue_length(sem->wait_queue);
	}

	return 0;
}

