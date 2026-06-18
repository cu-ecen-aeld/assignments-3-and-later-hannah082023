#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // 將 void 指標轉型回 thread_data 指標
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // 1. 等待取得鎖之前的時間 (毫秒轉微秒)
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // 2. 嘗試取得 Mutex (上鎖)
    int rc = pthread_mutex_lock(thread_func_args->mutex);
    if (rc != 0) {
        ERROR_LOG("pthread_mutex_lock failed with %d", rc);
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // 3. 取得鎖之後持有的一段時間
    usleep(thread_func_args->wait_to_release_ms * 1000);

    // 4. 【關鍵修正】標記執行成功 (一定要在解鎖前設定，避免與主執行緒發生 Race Condition)
    thread_func_args->thread_complete_success = true;

    // 5. 釋放 Mutex (解鎖)
    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if (rc != 0) {
        ERROR_LOG("pthread_mutex_unlock failed with %d", rc);
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    // 1. 動態配置記憶體 (使用 calloc 確保記憶體乾淨)
    struct thread_data* data = (struct thread_data*) calloc(1, sizeof(struct thread_data));
    if (data == NULL) {
        ERROR_LOG("Failed to allocate memory for thread_data");
        return false;
    }

    // 2. 設定參數
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;

    // 3. 建立並啟動執行緒
    int rc = pthread_create(thread, NULL, threadfunc, (void *)data);
    
    if (rc != 0) {
        ERROR_LOG("pthread_create failed with %d", rc);
        free(data);
        return false;
    }

    return true;
}
