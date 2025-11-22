#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

// Forward declarations
typedef struct ThreadPool ThreadPool;
typedef struct WorkItem WorkItem;

// Function pointer for work to be executed by threads
typedef void (*WorkFunction)(void* data);

// Work item structure
struct WorkItem {
    WorkFunction function;
    void* data;
    struct WorkItem* next;
};

// Thread pool structure
struct ThreadPool {
    pthread_t* threads;
    int threadCount;

    WorkItem* workQueueHead;
    WorkItem* workQueueTail;
    int queueSize;

    pthread_mutex_t queueMutex;
    pthread_cond_t workAvailable;
    pthread_cond_t workComplete;

    bool shutdown;
    int activeThreads;
};

// Thread pool API
ThreadPool* ThreadPool_Create(int threadCount);
void ThreadPool_Enqueue(ThreadPool* pool, WorkFunction function, void* data);
void ThreadPool_WaitAll(ThreadPool* pool);
int ThreadPool_GetQueueSize(ThreadPool* pool);
int ThreadPool_GetActiveThreads(ThreadPool* pool);
void ThreadPool_Destroy(ThreadPool* pool);

#endif // THREAD_POOL_H
