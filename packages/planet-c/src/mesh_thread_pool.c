// Windows includes must come BEFORE raylib to avoid symbol conflicts
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

#include "mesh_thread_pool.h"
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#endif

// Worker thread function
static void* worker_thread(void* arg) {
    MeshThreadPool* pool = (MeshThreadPool*)arg;

    while (true) {
        MeshRequest* request = NULL;

        // Acquire lock and wait for work
        pthread_mutex_lock(&pool->mutex);

        while (pool->queue_head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->work_available, &pool->mutex);
        }

        if (pool->shutdown && pool->queue_head == NULL) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        // Dequeue request (FIFO)
        if (pool->queue_head != NULL) {
            request = pool->queue_head;
            pool->queue_head = request->next;
            if (pool->queue_head == NULL) {
                pool->queue_tail = NULL;
            }
            pool->queue_count--;
            request->next = NULL;
        }

        pthread_mutex_unlock(&pool->mutex);

        if (request == NULL) {
            continue;
        }

        // Check if cancelled before starting work
        MeshRequestStatus status = atomic_load(&request->status);
        if (status == REQUEST_CANCELLED) {
            // Move directly to completed list for cleanup
            pthread_mutex_lock(&pool->mutex);
            request->next = pool->completed_head;
            pool->completed_head = request;
            pool->completed_count++;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        // Mark as generating
        atomic_store(&request->status, REQUEST_GENERATING);

        // Generate mesh on CPU (thread-safe)
        request->result = generate_sphere_patch_cpu(
            request->face,
            request->u_min, request->u_max,
            request->v_min, request->v_max,
            request->radius, request->noise_scale,
            request->resolution, request->skirt_depth
        );

        // Check if cancelled during generation
        status = atomic_load(&request->status);
        if (status == REQUEST_CANCELLED) {
            // Free the generated mesh since we won't use it
            free_cpu_mesh(&request->result);
        } else {
            // Mark as ready
            atomic_store(&request->status, REQUEST_READY);
        }

        // Move to completed list
        pthread_mutex_lock(&pool->mutex);
        request->next = pool->completed_head;
        pool->completed_head = request;
        pool->completed_count++;
        pthread_mutex_unlock(&pool->mutex);
    }

    return NULL;
}

// Get number of CPU cores
static int get_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int)sysinfo.dwNumberOfProcessors;
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return (count > 0) ? (int)count : 4;  // Default to 4 if detection fails
#endif
}

bool mesh_pool_init(MeshThreadPool* pool) {
    int cores = get_cpu_count();
    int thread_count = (cores > 1) ? cores - 1 : 1;  // Leave 1 core for main thread
    return mesh_pool_init_with_threads(pool, thread_count);
}

bool mesh_pool_init_with_threads(MeshThreadPool* pool, int thread_count) {
    if (pool == NULL || thread_count < 1) {
        return false;
    }

    // Initialize pool state
    pool->threads = NULL;
    pool->thread_count = 0;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->queue_count = 0;
    pool->completed_head = NULL;
    pool->completed_count = 0;
    pool->shutdown = false;

    // Initialize mutex and condition variable
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        return false;
    }

    if (pthread_cond_init(&pool->work_available, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        return false;
    }

    // Allocate thread handles
    pool->threads = malloc(sizeof(pthread_t) * thread_count);
    if (pool->threads == NULL) {
        pthread_cond_destroy(&pool->work_available);
        pthread_mutex_destroy(&pool->mutex);
        return false;
    }

    // Create worker threads
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            // Failed to create thread - shutdown already created ones
            pool->thread_count = i;
            mesh_pool_shutdown(pool);
            return false;
        }
    }

    pool->thread_count = thread_count;
    printf("[MeshPool] Initialized with %d worker threads\n", thread_count);
    return true;
}

void mesh_pool_shutdown(MeshThreadPool* pool) {
    if (pool == NULL) {
        return;
    }

    // Signal shutdown
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->work_available);
    pthread_mutex_unlock(&pool->mutex);

    // Wait for all threads to finish
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    // Free remaining queued requests
    MeshRequest* req = pool->queue_head;
    while (req != NULL) {
        MeshRequest* next = req->next;
        if (req->result.vertices != NULL) {
            free_cpu_mesh(&req->result);
        }
        free(req);
        req = next;
    }

    // Free remaining completed requests
    req = pool->completed_head;
    while (req != NULL) {
        MeshRequest* next = req->next;
        if (req->result.vertices != NULL) {
            free_cpu_mesh(&req->result);
        }
        free(req);
        req = next;
    }

    // Cleanup
    free(pool->threads);
    pool->threads = NULL;
    pool->thread_count = 0;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->queue_count = 0;
    pool->completed_head = NULL;
    pool->completed_count = 0;

    pthread_cond_destroy(&pool->work_available);
    pthread_mutex_destroy(&pool->mutex);

    printf("[MeshPool] Shutdown complete\n");
}

MeshRequest* mesh_pool_enqueue(MeshThreadPool* pool,
                               MeshRequestType type,
                               CubeFace face,
                               float u_min, float u_max,
                               float v_min, float v_max,
                               float radius, float noise_scale,
                               int resolution, float skirt_depth,
                               int depth,
                               void* node,
                               uint32_t node_generation,
                               int child_index) {
    if (pool == NULL) {
        return NULL;
    }

    // Allocate request
    MeshRequest* request = malloc(sizeof(MeshRequest));
    if (request == NULL) {
        return NULL;
    }

    // Initialize request
    request->type = type;
    request->face = face;
    request->u_min = u_min;
    request->u_max = u_max;
    request->v_min = v_min;
    request->v_max = v_max;
    request->radius = radius;
    request->noise_scale = noise_scale;
    request->skirt_depth = skirt_depth;
    request->resolution = resolution;
    request->depth = depth;
    request->node = node;
    request->node_generation = node_generation;
    request->child_index = child_index;
    request->next = NULL;

    // Initialize result as empty
    request->result.vertices = NULL;
    request->result.normals = NULL;
    request->result.indices = NULL;
    request->result.vertexCount = 0;
    request->result.triangleCount = 0;

    // Set initial status
    atomic_store(&request->status, REQUEST_QUEUED);

    // Enqueue (FIFO - add to tail)
    pthread_mutex_lock(&pool->mutex);

    if (pool->queue_tail == NULL) {
        pool->queue_head = request;
        pool->queue_tail = request;
    } else {
        pool->queue_tail->next = request;
        pool->queue_tail = request;
    }
    pool->queue_count++;

    // Wake one worker
    pthread_cond_signal(&pool->work_available);

    pthread_mutex_unlock(&pool->mutex);

    return request;
}

MeshRequest* mesh_pool_drain_completed(MeshThreadPool* pool) {
    if (pool == NULL) {
        return NULL;
    }

    MeshRequest* list;

    pthread_mutex_lock(&pool->mutex);
    list = pool->completed_head;
    pool->completed_head = NULL;
    pool->completed_count = 0;
    pthread_mutex_unlock(&pool->mutex);

    return list;
}

void mesh_pool_cancel(MeshRequest* request) {
    if (request != NULL) {
        atomic_store(&request->status, REQUEST_CANCELLED);
    }
}

void mesh_pool_free_request(MeshRequest* request) {
    if (request != NULL) {
        // Note: CpuMesh should already be freed or uploaded
        // We don't free it here to avoid double-free
        free(request);
    }
}

MeshPoolStats mesh_pool_get_stats(MeshThreadPool* pool) {
    MeshPoolStats stats = {0, 0, 0};

    if (pool != NULL) {
        pthread_mutex_lock(&pool->mutex);
        stats.queued_count = pool->queue_count;
        stats.completed_count = pool->completed_count;
        stats.thread_count = pool->thread_count;
        pthread_mutex_unlock(&pool->mutex);
    }

    return stats;
}
