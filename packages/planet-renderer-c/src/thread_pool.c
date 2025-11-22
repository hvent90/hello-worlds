#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>

// Worker thread function
static void* ThreadPool_WorkerThread(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;

    while (true) {
        pthread_mutex_lock(&pool->queueMutex);

        // Wait for work or shutdown signal
        while (pool->workQueueHead == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->workAvailable, &pool->queueMutex);
        }

        // Check if we should shutdown
        if (pool->shutdown && pool->workQueueHead == NULL) {
            pthread_mutex_unlock(&pool->queueMutex);
            break;
        }

        // Get work item from queue
        WorkItem* item = pool->workQueueHead;
        if (item != NULL) {
            pool->workQueueHead = item->next;
            if (pool->workQueueHead == NULL) {
                pool->workQueueTail = NULL;
            }
            pool->queueSize--;
            pool->activeThreads++;
        }

        pthread_mutex_unlock(&pool->queueMutex);

        // Execute work outside of lock
        if (item != NULL) {
            item->function(item->data);
            free(item);

            // Mark thread as no longer active
            pthread_mutex_lock(&pool->queueMutex);
            pool->activeThreads--;
            pthread_cond_signal(&pool->workComplete);
            pthread_mutex_unlock(&pool->queueMutex);
        }
    }

    return NULL;
}

ThreadPool* ThreadPool_Create(int threadCount) {
    ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    if (!pool) {
        return NULL;
    }

    pool->threadCount = threadCount;
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * threadCount);
    pool->workQueueHead = NULL;
    pool->workQueueTail = NULL;
    pool->queueSize = 0;
    pool->shutdown = false;
    pool->activeThreads = 0;

    pthread_mutex_init(&pool->queueMutex, NULL);
    pthread_cond_init(&pool->workAvailable, NULL);
    pthread_cond_init(&pool->workComplete, NULL);

    // Create worker threads
    for (int i = 0; i < threadCount; i++) {
        if (pthread_create(&pool->threads[i], NULL, ThreadPool_WorkerThread, pool) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            // Cleanup and return NULL
            ThreadPool_Destroy(pool);
            return NULL;
        }
    }

    return pool;
}

void ThreadPool_Enqueue(ThreadPool* pool, WorkFunction function, void* data) {
    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->function = function;
    item->data = data;
    item->next = NULL;

    pthread_mutex_lock(&pool->queueMutex);

    // Add to queue
    if (pool->workQueueTail != NULL) {
        pool->workQueueTail->next = item;
    } else {
        pool->workQueueHead = item;
    }
    pool->workQueueTail = item;
    pool->queueSize++;

    // Signal that work is available
    pthread_cond_signal(&pool->workAvailable);

    pthread_mutex_unlock(&pool->queueMutex);
}

void ThreadPool_WaitAll(ThreadPool* pool) {
    pthread_mutex_lock(&pool->queueMutex);

    // Wait until queue is empty and no threads are active
    while (pool->queueSize > 0 || pool->activeThreads > 0) {
        pthread_cond_wait(&pool->workComplete, &pool->queueMutex);
    }

    pthread_mutex_unlock(&pool->queueMutex);
}

int ThreadPool_GetQueueSize(ThreadPool* pool) {
    pthread_mutex_lock(&pool->queueMutex);
    int size = pool->queueSize;
    pthread_mutex_unlock(&pool->queueMutex);
    return size;
}

int ThreadPool_GetActiveThreads(ThreadPool* pool) {
    pthread_mutex_lock(&pool->queueMutex);
    int active = pool->activeThreads;
    pthread_mutex_unlock(&pool->queueMutex);
    return active;
}

void ThreadPool_Destroy(ThreadPool* pool) {
    if (!pool) {
        return;
    }

    // Signal shutdown
    pthread_mutex_lock(&pool->queueMutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->workAvailable);
    pthread_mutex_unlock(&pool->queueMutex);

    // Wait for all threads to finish
    for (int i = 0; i < pool->threadCount; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    // Clean up remaining work items
    WorkItem* item = pool->workQueueHead;
    while (item != NULL) {
        WorkItem* next = item->next;
        free(item);
        item = next;
    }

    pthread_mutex_destroy(&pool->queueMutex);
    pthread_cond_destroy(&pool->workAvailable);
    pthread_cond_destroy(&pool->workComplete);

    free(pool->threads);
    free(pool);
}
