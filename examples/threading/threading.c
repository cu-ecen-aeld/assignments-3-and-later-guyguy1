#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    bool success = true;
    struct thread_data* data = (struct thread_data*)thread_param;

    usleep(data->wait_to_obtain_ms);

    if (pthread_mutex_lock(data->mutex) != 0)
    {
        ERROR_LOG("Failed locking");
        success = false;
        goto cleanup;
    }

    usleep(data->wait_to_release_ms);

    if (pthread_mutex_unlock(data->mutex) != 0)
    {
        ERROR_LOG("Failed unlocking");
        success = false;
        goto cleanup;
    }

cleanup:
    data->thread_complete_success = success;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    bool success = true;

    struct thread_data *data = (struct thread_data *)malloc(sizeof(struct thread_data));
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, threadfunc, data) != 0)
    {
        ERROR_LOG("Failed pthread_create");
        success = false;
        goto cleanup;
    }

    *thread = thread_id;

cleanup:
    return success;
}

