/* 
 * tps_testsuite.c
 * Tests the functionality of tps.h
 * 
 * - No runtime arguments will run the default test
 * - "./tps_testsuite.x protection" will check for segvhandler
 * - "./tps_testsuite.x error" will check the error handling of API
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>

#include <sem.h>
#include <tps.h>

/***** Test Macros *****/
#define TEST_START  printf("TEST: " "\x1b[33m" "%s" "\x1b[0m" "\n", __func__)
#define TEST_END    printf("\x1b[32m" "PASS" "\x1b[0m" "\n")

/***** Global Definitions *****/
static sem_t sem1, sem2;
void *latest_mmap_addr;

/***** mmap wrapper *****/
void *__real_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);
void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    latest_mmap_addr = __real_mmap(addr, len, prot, flags, fildes, off);
    return latest_mmap_addr;
}

/***** Threads *****/
/* 
 * Protection Testing 
 * -This thread will check the correct calling of our custom segvhandler 
 * -It attempts to write to write to an address within the tps 
 * -Upon success this test progam will print to std error the provided error
 * message and stop the program. For this reason this test is separate from the
 * other tests and must be called by adding the command line argument "protection".
 * */
void *protection_thread(void *arg) 
{	TEST_START;

	char *tps_addr;

    /* Create TPS */
    tps_create();

    /* Get TPS page address as allocated via mmap() */
    tps_addr = latest_mmap_addr;

    /* Cause an intentional TPS protection error */
    tps_addr[0] = '\0';

	TEST_END;
	return NULL;
}

/* 
 * Error Testing
 * - These threads check all the possibilities when the api function should return
 * -1.
 * - These tests are separate from the other tests. To run this have "error" as
 *   a runtime argument. 
 */
void *error_helper_thread(void *arg)
{
	sem_down(sem1);
	tps_create();
	sem_up(sem2);

	sem_down(sem1);
	tps_destroy();
	sem_up(sem2);

	return NULL;
}
void *error_thread(void *arg) 
{	TEST_START;

	char buffer[TPS_SIZE];
	pthread_t tid;

	/* Create error helper thread for clone checking */
	pthread_create(&tid, NULL, error_helper_thread, NULL);

	/* Test error after recalling tps_init */
	assert(tps_init(0) == -1);

	/* Clone error, TID does not have tps */
	assert(tps_clone(tid) == -1);
	sem_up(sem1);
	sem_down(sem2);

	/* Destroy Errors */
	assert(tps_destroy() == -1);

	/* Reading and Writing to not created tps */
	assert(tps_read(0,TPS_SIZE,buffer) == -1);
	assert(tps_write(0,TPS_SIZE,buffer) == -1);

    /* Create Errors */
    assert(tps_create() == 0);
	assert(tps_create() == -1);

	/* Reading out of bounds and passing NULL*/
	assert(tps_read(0,TPS_SIZE,NULL) == -1);
	assert(tps_read(TPS_SIZE,0,buffer) == -1);
	assert(tps_read(0,TPS_SIZE + 1,buffer) == -1);
	assert(tps_read(TPS_SIZE,TPS_SIZE + 1,buffer) == -1);

	/* Writing out of bounds and passing NULL*/
	assert(tps_write(0,TPS_SIZE,NULL) == -1);
	assert(tps_write(TPS_SIZE,0,buffer) == -1);
	assert(tps_write(0,TPS_SIZE + 1,buffer) == -1);
	assert(tps_write(TPS_SIZE,TPS_SIZE + 1,buffer) == -1);

	/* Clone error, current thread already has tps */
	assert(tps_clone(tid) == -1);

	/* Destroy tps */
	assert(tps_destroy() == 0);

	/* Join the helper thread */
	sem_up(sem1);
	sem_down(sem2);
	pthread_join(tid, NULL);

	TEST_END;
	return NULL;
}

/* 
 * Default Testing 
 * - These tests will check the proper functionality of the API
 * - default_thread is the encapsulating thread that will be started by main.
 *   This thread will then run all the test functions.
 */
/* Reading and writing tests */
void read_write_test(void)
{
	TEST_START;

	char msg1[TPS_SIZE] = "This is a test message";
	char buffer[TPS_SIZE] = "";

	tps_create();

	/* confirm value written equals value read */
	tps_write(0,TPS_SIZE,msg1);
	tps_read(0,TPS_SIZE,buffer);
	assert(strcmp(buffer,msg1) == 0);

	/* Check offset read functionality by reading "test" */
	tps_read(10, 4, buffer);
	buffer[4] = '\0';
	assert(strcmp(buffer,"test") == 0);

	/*
	 * Check offset write by changing tps to 
	 * "This is a modified message"
	 */
	tps_write(10, 17,"modified message");
	tps_read(0,TPS_SIZE,buffer);
	assert(strcmp(buffer, "This is a modified message") == 0);

	assert(tps_destroy() == 0);
	
	TEST_END;
	return;
}

/* Cloning tests */
void *clone_helper_thread(void *arg)
{
	char *msg1 = (char*) arg;
	char buffer[TPS_SIZE] = "";
	
	/* Create tps and write msg1 */
	sem_down(sem1);
	tps_create();
	tps_write(0, TPS_SIZE, msg1);
	sem_up(sem2);

	/* Read tps */
	sem_down(sem1);
	tps_read(0,TPS_SIZE,buffer);
	assert(strcmp(buffer,msg1) == 0);
	sem_up(sem2);
	
	/* Confirm cloned tps modification does not modify this tps */
	sem_down(sem1);
	tps_read(0,TPS_SIZE,buffer);
	assert(strcmp(buffer,msg1) == 0);
	tps_destroy();
	sem_up(sem2);

	return NULL;
}

void clone_test(void)
{
	TEST_START;

	char msg1[TPS_SIZE] = "This is the original thread";
	char msg2[TPS_SIZE] = "This is the cloned thread";
	char buffer[TPS_SIZE];
	void *old_mmap_addr;
	pthread_t tid;

	/* 
	 * Create clone helper thread for clone checking, block this thread until
	 * the helper thread makes its tps and writes msg1.
	 */
	pthread_create(&tid, NULL, clone_helper_thread, (void*)msg1);
	sem_up(sem1);
	sem_down(sem2);

	/* 
	 * Save current lates_mmap_addr to later check that no new memory is
	 * allocated when tps is cloned. (copy on write feature)
	 */
	old_mmap_addr = latest_mmap_addr;

	/* Clone the helper threads tps */
	tps_clone(tid);

	/* Check cloned properly, and no new memory is allocated after read */
	tps_read(0,TPS_SIZE,buffer);
	assert(strcmp(buffer,msg1) == 0);
	assert(old_mmap_addr == latest_mmap_addr);
	
	/* Allow helper thread to read shared page */
	sem_up(sem1);
	sem_down(sem2);

	/* Confirm after original tps reads, still no new allocation */
	assert(old_mmap_addr == latest_mmap_addr);

	/* Now write to tps and switch to helper thread */
	tps_write(0,TPS_SIZE,msg2);
	sem_up(sem1);
	sem_down(sem2);

	/* Confirm Copy on Write */
	tps_read(0, TPS_SIZE, buffer);
	assert(strcmp(buffer,msg2) == 0);
	assert(old_mmap_addr != latest_mmap_addr);

	tps_destroy();

	TEST_END;
	return;
}

/* Default thread to be run without runtime args */
void *default_thread(void *arg)
{
	read_write_test();
	clone_test();

	return NULL;
}


/***** Main *****/
int main(int argc, char** argv) 
{
	pthread_t tid;
	char test_name[32] = "";

	if (argc > 1) {
		strcpy(test_name, argv[1]);
	}

	/* Create two semaphores for thread synchro */
	sem1 = sem_create(0);
	sem2 = sem_create(0);

	/* Init TPS API */
	tps_init(1);

	if (strcmp(test_name,"protection") == 0) {
		/* TPS protection Test */
		pthread_create(&tid, NULL, protection_thread, NULL);
		pthread_join(tid, NULL);
	} else if (strcmp(test_name,"error") == 0) {
		/* API Error handling Test */
		pthread_create(&tid, NULL, error_thread, NULL);
		pthread_join(tid, NULL);
	} else {
		/* Default Test */
		pthread_create(&tid, NULL, default_thread, NULL);
		pthread_join(tid, NULL);
	}

	/* Destroy resources and quit */
	sem_destroy(sem1);
	sem_destroy(sem2);
	return 0;
}