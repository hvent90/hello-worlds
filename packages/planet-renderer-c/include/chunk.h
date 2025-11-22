#ifndef CHUNK_H
#define CHUNK_H

#include <raylib.h>
#include <pthread.h>

// Chunk generation states
typedef enum {
    CHUNK_STATE_UNINITIALIZED,  // No data allocated
    CHUNK_STATE_PENDING,         // Waiting to be generated
    CHUNK_STATE_GENERATING,      // Currently being generated on worker thread
    CHUNK_STATE_READY_TO_UPLOAD, // Generation complete, ready for GPU upload
    CHUNK_STATE_UPLOADED         // Uploaded to GPU and ready to render
} ChunkState;

typedef struct Chunk {
    Mesh mesh;
    Model model;
    Vector3 offset;
    float width;
    float height;
    float radius;
    int resolution;
    Matrix localToWorld;
    Vector3 origin;
    unsigned long long id; // Unique ID matching QuadtreeNode
    bool isUploaded; // Track if VRAM is allocated
    float terrainFrequency;
    float terrainAmplitude;

    // Async generation state
    ChunkState state;
    pthread_mutex_t stateMutex;
} Chunk;

Chunk* Chunk_Create(Vector3 offset, float width, float height, float radius, int resolution, Vector3 origin, Matrix localToWorld, float terrainFrequency, float terrainAmplitude);

// Synchronous generation (old API, still supported)
void Chunk_Generate(Chunk* chunk);

// Async generation API
void Chunk_GenerateAsync(Chunk* chunk);      // Generate mesh data on worker thread
void Chunk_UploadToGPU(Chunk* chunk);        // Upload to GPU (must be called from main thread)
ChunkState Chunk_GetState(Chunk* chunk);     // Thread-safe state getter

void Chunk_Draw(Chunk* chunk, Color surfaceColor, Color wireframeColor, Shader lightingShader);
void Chunk_DrawWithShadow(Chunk* chunk, Color surfaceColor, Color wireframeColor, Shader lightingShader, Texture2D shadowMap);
void Chunk_Free(Chunk* chunk);

#endif // CHUNK_H
