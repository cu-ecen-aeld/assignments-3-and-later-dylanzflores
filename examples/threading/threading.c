#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */


void* thread_func(void* params){
	// Set parameters from struct
	struct thread_data* thr = (struct thread_data*) params;
	// Sleep a number of "wait_to_obtain_ms" in milliseconds
	usleep(thr->wait_to_obtain_ms * 1000);
	
	// Obtain the mutex
	if(pthread_mutex_lock(thr->mutex) != 0){
		thr->thread_complete_success = false;
		return thr;
	}
	// Sleep a "wait_to_release_ms" in milliseconds
	usleep(thr->wait_to_release_ms * 1000);
	// Obtain the mutex
	if(pthread_mutex_unlock(thr->mutex) != 0){
		thr->thread_complete_success = false;
		return thr;
	}
	// Bool check the status of the thread
	thr->thread_complete_success = true;
	return thr;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms){
	struct thread_data* data = malloc(sizeof(struct thread_data));

	if(!data){
		return false;

	}
	
	data->mutex = mutex;
	data->wait_to_obtain_ms = wait_to_obtain_ms;
	data->wait_to_release_ms = wait_to_release_ms;
	data->thread_complete_success = false;
	int rc = pthread_create(thread, NULL, thread_func, data);
	if(rc != 0){
		free(data);
		return false;
	}
	
	return true;


}
