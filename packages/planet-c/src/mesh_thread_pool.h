#ifndef MESH_THREAD_POOL_H
#define MESH_THREAD_POOL_H

#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include "mesh.h"
#include "cube_face.h"

// Request status (atomic for safe cross-thread reads)
typedef enum {
    REQUEST_QUEUED,       // In queue, not started
    REQUEST_GENERATING,   // Worker is processing
    REQUEST_READY,        // Result available for upload
    REQUEST_CANCELLED     // Should be discarded
} MeshRequestStatus;

// Request type
typedef enum {
    REQUEST_SUBDIVIDE,    // Generating child mesh
    REQUEST_MERGE         // Regenerating parent mesh
} MeshRequestType;

// Self-contained mesh generation request
typedef struct MeshRequest {
    // Request type
    MeshRequestType type;

    // Generation parameters
    CubeFace face;
    float u_min, u_max, v_min, v_max;
    float radius;
    float noise_scale;
    float skirt_depth;
    int resolution;
    int depth;

    // Result (written by worker, read by main thread)
    CpuMesh result;

    // Status (atomic for safe cross-thread reads)
    _Atomic MeshRequestStatus status;

    // Link back to requesting node
    void* node;
    uint32_t node_generation;  // For validating node is still valid
    int child_index;           // Which child slot (0-3), or -1 for merge

    // Intrusive linked list
    struct MeshRequest* next;
} MeshRequest;

// Thread pool for async mesh generation
typedef struct {
    pthread_t* threads;
    int thread_count;

    // Work queue (singly-linked list, FIFO)
    MeshRequest* queue_head;
    MeshRequest* queue_tail;
    int queue_count;

    // Completed requests (singly-linked list for main thread polling)
    MeshRequest* completed_head;
    int completed_count;

    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t work_available;

    // Shutdown flag
    bool shutdown;
} MeshThreadPool;

// Initialize thread pool with (system_cores - 1) worker threads
// Returns true on success
bool mesh_pool_init(MeshThreadPool* pool);

// Initialize thread pool with specific thread count
bool mesh_pool_init_with_threads(MeshThreadPool* pool, int thread_count);

// Shutdown thread pool and wait for workers to finish
void mesh_pool_shutdown(MeshThreadPool* pool);

// Create and enqueue a mesh generation request
// Returns the request pointer (caller can store in node->pending_child[i])
// Returns NULL on allocation failure
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
                               int child_index);

// Poll and drain completed requests (call from main thread each frame)
// Returns linked list of completed requests (caller must process and free)
// List is detached atomically - safe to process without holding lock
MeshRequest* mesh_pool_drain_completed(MeshThreadPool* pool);

// Cancel a pending request (sets status to CANCELLED)
// Safe to call even if request is already processing/completed
void mesh_pool_cancel(MeshRequest* request);

// Free a processed request (after GPU upload or discard)
void mesh_pool_free_request(MeshRequest* request);

// Get stats for debugging
typedef struct {
    int queued_count;
    int completed_count;
    int thread_count;
} MeshPoolStats;

MeshPoolStats mesh_pool_get_stats(MeshThreadPool* pool);

#endif // MESH_THREAD_POOL_H
