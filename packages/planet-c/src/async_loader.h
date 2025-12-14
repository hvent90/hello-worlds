#ifndef ASYNC_LOADER_H
#define ASYNC_LOADER_H

#include "mesh.h"
#include <stdbool.h>

// Parameters required to generate a chunk
typedef struct {
    CubeFace face;
    float u_min, u_max;
    float v_min, v_max;
    float radius;
    float noise_scale;
    int resolution;
    float skirt_depth;
    
    // For relevance checking
    int depth;
    Vector3 center_pos; // World space center
    
    // ID to match request with result
    unsigned int id;
} ChunkGenerationParams;

typedef struct {
    unsigned int id;
    CpuMesh mesh;
    bool valid; // If false, generation was skipped/aborted
} ChunkGenerationResult;

// Initialize the async loader (starts worker thread)
void async_loader_init(void);

// Serialize cleanup
void async_loader_shutdown(void);

// Update global state for relevance checks (call every frame)
// local_camera_pos: Camera position in planet-local space
// detail_threshold: Pixel threshold value (e.g. SUBDIVIDE_PIXEL_THRESHOLD)
// screen_height: For projection calculations
// fov: Camera field of view
void async_loader_update_state(Vector3 local_camera_pos, float detail_threshold, int screen_height, float fov);

// Request a chunk to be generated
// returns true if added to queue, false if queue full
bool async_loader_request(ChunkGenerationParams params);

// Retrieve a completed result (non-blocking)
// returns true if a result was retrieved
bool async_loader_get_result(ChunkGenerationResult *out_result);

// Check if a request ID is currently pending (in queue or processing)
bool async_loader_is_pending(unsigned int id);

// Debug stats
typedef struct {
    int request_queue_count;
    int result_queue_count;
    int total_processed;
    int total_skipped;
    int worker_loop_count;
    int worker_waiting;  // 1 if worker is blocked waiting for work
} AsyncLoaderStats;

void async_loader_get_stats(AsyncLoaderStats *out_stats);

#endif // ASYNC_LOADER_H
