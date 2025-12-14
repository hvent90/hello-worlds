#include "async_loader.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <raymath.h>

#define MAX_QUEUE_SIZE 2048

// Shared State
typedef struct {
    ChunkGenerationParams requests[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} RequestQueue;

typedef struct {
    ChunkGenerationResult results[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} ResultQueue;

typedef struct {
    Vector3 camera_pos;
    float detail_threshold; // The merge threshold
    float projection_factor; 
    bool active;
} LoaderState;

static pthread_t worker_thread;
static pthread_mutex_t mutex;
static pthread_cond_t cond_var;
static bool running = false;

static RequestQueue request_queue = {0};
static ResultQueue result_queue = {0};
static LoaderState loader_state = {0};

// Debug counters
static int total_processed = 0;
static int total_skipped = 0;
static int worker_loop_count = 0;  // How many times worker has looped
static int worker_waiting = 0;      // 1 if worker is waiting on cond_var

// Helper: Calculate screen space size estimate
static float calculate_screen_size(float radius, float uv_size, Vector3 center, Vector3 cam_pos, float proj_factor) {
    float distance = Vector3Distance(cam_pos, center);
    if (distance < 1.0f) distance = 1.0f;
    
    // Arc length approximation
    float chunk_size = uv_size * radius * 1.570796f; // PI/2
    return (chunk_size / distance) * proj_factor;
}

static void* worker_func(void* arg) {
    while (running) {
        worker_loop_count++;
        ChunkGenerationParams params;
        bool has_work = false;

        // Wait for work
        pthread_mutex_lock(&mutex);
        while (request_queue.count == 0 && running) {
            worker_waiting = 1;
            pthread_cond_wait(&cond_var, &mutex);
            worker_waiting = 0;
        }
        
        if (!running) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        
        // Pop request
        if (request_queue.count > 0) {
            params = request_queue.requests[request_queue.tail];
            request_queue.tail = (request_queue.tail + 1) % MAX_QUEUE_SIZE;
            request_queue.count--;
            has_work = true;
        }
        
        // Capture specific state variables needed for relevance check
        Vector3 cam_pos = loader_state.camera_pos;
        float proj_factor = loader_state.projection_factor;
        // The merge threshold is roughly half the subdivide threshold usually, but let's be conservative.
        // User asked to check if it's relevent.
        // If the chunk is so small it should be merged, we shouldn't generate it.
        // The 'detail_threshold' passed in is usually the SUBDIVIDE threshold.
        // Merge threshold is usually lower. Let's assume 0.5 * threshold.
        float min_needed_size = loader_state.detail_threshold * 0.4f; 
        
        pthread_mutex_unlock(&mutex);
        
        if (has_work) {
            ChunkGenerationResult result = {0};
            result.id = params.id;
            
            // --- RELEVANCE CHECK ---
            // Calculate child screen size
            float uv_size = params.u_max - params.u_min;
            float px_size = calculate_screen_size(params.radius, uv_size, params.center_pos, cam_pos, proj_factor);

            // If the CHILD itself is tiny, skip.
            // DEBUG: Disable relevance check to ensure data flows
            if (false && px_size < min_needed_size) {
                 result.valid = false;
                 total_skipped++;
            } else {
                // Generate
                result.mesh = generate_sphere_patch_cpu(params.face,
                                                        params.u_min, params.u_max,
                                                        params.v_min, params.v_max,
                                                        params.radius, params.noise_scale,
                                                        params.resolution, params.skirt_depth);
                result.valid = true;
                total_processed++;
            }
            
            // Push result (ALWAYS push, so main thread knows it was processed)
            pthread_mutex_lock(&mutex);
            if (result_queue.count < MAX_QUEUE_SIZE) {
                result_queue.results[result_queue.head] = result;
                result_queue.head = (result_queue.head + 1) % MAX_QUEUE_SIZE;
                result_queue.count++;
            } else {
                // Queue full, drop result
                if (result.valid) free_cpu_mesh(&result.mesh);
                printf("Async result queue full, dropped %u\n", result.id);
            }
            pthread_mutex_unlock(&mutex);
        }
    }
    return NULL;
}

void async_loader_init(void) {
    if (running) return;
    
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond_var, NULL);
    
    running = true;
    request_queue.head = 0; request_queue.tail = 0; request_queue.count = 0;
    result_queue.head = 0; result_queue.tail = 0; result_queue.count = 0;
    
    pthread_create(&worker_thread, NULL, worker_func, NULL);
}

void async_loader_shutdown(void) {
    if (!running) return;
    
    pthread_mutex_lock(&mutex);
    running = false;
    pthread_cond_broadcast(&cond_var);
    pthread_mutex_unlock(&mutex);
    
    pthread_join(worker_thread, NULL);
    
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond_var);
    
    // Cleanup generated but unclaimed meshes
    for (int i=0; i < result_queue.count; i++) {
        int idx = (result_queue.tail + i) % MAX_QUEUE_SIZE;
        if (result_queue.results[idx].valid) {
            free_cpu_mesh(&result_queue.results[idx].mesh);
        }
    }
}

void async_loader_update_state(Vector3 local_camera_pos, float detail_threshold, int screen_height, float fov) {
    pthread_mutex_lock(&mutex);
    loader_state.camera_pos = local_camera_pos;
    loader_state.detail_threshold = detail_threshold;
    
    float fov_rad = fov * DEG2RAD;
    loader_state.projection_factor = (float)screen_height / (2.0f * tanf(fov_rad * 0.5f));
    
    loader_state.active = true;
    pthread_mutex_unlock(&mutex);
}

bool async_loader_request(ChunkGenerationParams params) {
    bool enqueued = false;
    pthread_mutex_lock(&mutex);
    if (request_queue.count < MAX_QUEUE_SIZE) {
        request_queue.requests[request_queue.head] = params;
        request_queue.head = (request_queue.head + 1) % MAX_QUEUE_SIZE;
        request_queue.count++;
        enqueued = true;
        pthread_cond_signal(&cond_var);
    }
    pthread_mutex_unlock(&mutex);
    return enqueued;
}

bool async_loader_get_result(ChunkGenerationResult *out_result) {
    bool got = false;
    pthread_mutex_lock(&mutex);
    if (result_queue.count > 0) {
        *out_result = result_queue.results[result_queue.tail];
        result_queue.tail = (result_queue.tail + 1) % MAX_QUEUE_SIZE;
        result_queue.count--;
        got = true;
    }
    pthread_mutex_unlock(&mutex);
    return got;
}

bool async_loader_is_pending(unsigned int id) {
    bool pending = false;
    pthread_mutex_lock(&mutex);
    // Check request queue
    for (int i=0; i < request_queue.count; i++) {
        int idx = (request_queue.tail + i) % MAX_QUEUE_SIZE;
        if (request_queue.requests[idx].id == id) {
            pending = true;
            break;
        }
    }
    // Also check result queue (completed but not yet consumed)
    if (!pending) {
        for (int i=0; i < result_queue.count; i++) {
            int idx = (result_queue.tail + i) % MAX_QUEUE_SIZE;
            if (result_queue.results[idx].id == id) {
                pending = true;
                break;
            }
        }
    }
    pthread_mutex_unlock(&mutex);
    return pending;
}

void async_loader_get_stats(AsyncLoaderStats *out_stats) {
    pthread_mutex_lock(&mutex);
    out_stats->request_queue_count = request_queue.count;
    out_stats->result_queue_count = result_queue.count;
    out_stats->total_processed = total_processed;
    out_stats->total_skipped = total_skipped;
    out_stats->worker_loop_count = worker_loop_count;
    out_stats->worker_waiting = worker_waiting;
    pthread_mutex_unlock(&mutex);
}
