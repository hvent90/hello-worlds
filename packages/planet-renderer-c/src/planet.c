#include "planet.h"
#include "rlgl.h"
#include <stdlib.h>
#include <stdio.h>

// No longer needed as we don't use the callback for freeing anymore
static void OnNodeSplit(QuadtreeNode* node) {
    // Empty for now, logic moved to Update
}

// Worker function for async chunk generation
static void GenerateChunkWorker(void* data) {
    Chunk* chunk = (Chunk*)data;
    Chunk_GenerateAsync(chunk);
}

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin, float terrainFrequency, float terrainAmplitude) {
    Planet* planet = (Planet*)malloc(sizeof(Planet));
    planet->radius = radius;
    planet->minCellSize = minCellSize;
    planet->minCellResolution = minCellResolution;
    planet->origin = origin;
    planet->terrainFrequency = terrainFrequency;
    planet->terrainAmplitude = terrainAmplitude;

    // Comparator value from TS default: 1.1 or similar.
    float comparatorValue = 1.5f;

    // Initialize Quadtree (will be recreated every frame)
    planet->quadtree = CubicQuadTree_Create(radius, minCellSize, comparatorValue, origin);

    // Initialize Chunk Map and Pool
    planet->chunkMap = ChunkMap_Create(1024); // Initial capacity
    planet->chunkPool = ChunkPool_Create(256); // Initial capacity

    // Initialize Thread Pool (4 worker threads, similar to TypeScript implementation)
    planet->threadPool = ThreadPool_Create(4);

    planet->surfaceColor = WHITE;
    planet->wireframeColor = BLACK;

    return planet;
}

void Planet_Update(Planet* planet, Vector3 cameraPosition) {
    // 1. Create NEW Quadtree
    CubicQuadTree* newQuadtree = CubicQuadTree_Create(
        planet->radius, 
        planet->minCellSize, 
        1.5f, // comparator
        planet->origin
    );
    
    // 2. Insert Camera to build tree
    CubicQuadTree_Insert(newQuadtree, cameraPosition, OnNodeSplit);
    
    // 3. Get new leaf nodes
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(newQuadtree, &leafNodes, &leafCount);
    
    // 4. Diffing & Allocation
    ChunkMap* newChunkMap = ChunkMap_Create(planet->chunkMap->capacity);
    
    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        unsigned long long id = node->id;
        
        // Check if we already have this chunk
        Chunk* existingChunk = ChunkMap_Remove(planet->chunkMap, id);
        
        if (existingChunk) {
            // REUSE: Move to new map
            ChunkMap_Insert(newChunkMap, id, existingChunk);
            node->userData = existingChunk;
        } else {
            // CREATE: Try to get from pool first
            Chunk* chunk = ChunkPool_Acquire(planet->chunkPool);
            
            if (!chunk) {
                // Allocate new if pool empty
                chunk = Chunk_Create(
                    node->bounds.min,
                    node->size.x,
                    node->size.y,
                    planet->radius,
                    planet->minCellResolution,
                    planet->origin,
                    node->localToWorld,
                    planet->terrainFrequency,
                    planet->terrainAmplitude
                );
                chunk->id = id;
            } else {
                // Reset pooled chunk
                chunk->offset = node->bounds.min;
                chunk->width = node->size.x;
                chunk->height = node->size.y;
                chunk->radius = planet->radius;
                chunk->resolution = planet->minCellResolution;
                chunk->origin = planet->origin;
                chunk->localToWorld = node->localToWorld;
                chunk->terrainFrequency = planet->terrainFrequency;
                chunk->terrainAmplitude = planet->terrainAmplitude;
                chunk->id = id;
                // Mesh needs regeneration
            }
            
            // Queue async generation for new chunk
            pthread_mutex_lock(&chunk->stateMutex);
            chunk->state = CHUNK_STATE_PENDING;
            pthread_mutex_unlock(&chunk->stateMutex);

            ThreadPool_Enqueue(planet->threadPool, GenerateChunkWorker, chunk);

            ChunkMap_Insert(newChunkMap, id, chunk);
            node->userData = chunk;
        }
    }

    // 4.5. Process chunks ready for upload (must be done on main thread)
    // Iterate through all chunks in the new map and upload any that are ready
    for (int i = 0; i < newChunkMap->capacity; i++) {
        ChunkMapEntry* entry = newChunkMap->buckets[i];
        while (entry) {
            Chunk* chunk = entry->value;
            if (Chunk_GetState(chunk) == CHUNK_STATE_READY_TO_UPLOAD) {
                Chunk_UploadToGPU(chunk);
            }
            entry = entry->next;
        }
    }

    // 5. Recycle remaining chunks in old map
    // Anything left in planet->chunkMap is no longer visible
    for (int i = 0; i < planet->chunkMap->capacity; i++) {
        ChunkMapEntry* entry = planet->chunkMap->buckets[i];
        while (entry) {
            Chunk* unusedChunk = entry->value;
            ChunkPool_Release(planet->chunkPool, unusedChunk);
            entry = entry->next;
        }
    }
    
    // 6. Cleanup
    ChunkMap_Destroy(planet->chunkMap); // Destroys map structure, chunks are now in pool or newMap
    planet->chunkMap = newChunkMap;
    
    CubicQuadTree_Free(planet->quadtree);
    planet->quadtree = newQuadtree;
    
    free(leafNodes);
}

int Planet_Draw(Planet* planet) {
    // Draw all chunks in the active map
    // We could traverse the quadtree, but iterating the map is faster/easier if we just want to draw all
    // However, traversing quadtree allows for frustum culling later.
    // Let's stick to quadtree traversal for drawing to match original logic

    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);

    int totalTriangles = 0;

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            Chunk* chunk = (Chunk*)node->userData;
            Chunk_DrawWithShadow(chunk, planet->surfaceColor, planet->wireframeColor, planet->lightingShader, planet->shadowMapTexture);

            // Calculate triangles: resolution^2 * 2
            totalTriangles += (chunk->resolution * chunk->resolution * 2);
        }
    }

    free(leafNodes);
    return totalTriangles;
}

int Planet_DrawWithShader(Planet* planet, Shader shader) {
    // Draw all chunks with a custom shader (useful for shadow pass)
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);

    int totalTriangles = 0;

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            Chunk* chunk = (Chunk*)node->userData;
            // Don't draw wireframe in shadow pass, use BLACK for color (doesn't matter for depth)
            Chunk_Draw(chunk, BLACK, BLACK, shader);

            // Calculate triangles: resolution^2 * 2
            totalTriangles += (chunk->resolution * chunk->resolution * 2);
        }
    }

    free(leafNodes);
    return totalTriangles;
}

void Planet_Free(Planet* planet) {
    // Wait for all pending chunk generation to complete
    ThreadPool_WaitAll(planet->threadPool);

    // Destroy thread pool
    ThreadPool_Destroy(planet->threadPool);

    // Free all chunks in map
    ChunkMap_Clear(planet->chunkMap); // We need to actually free the chunks, not just clear
    // Wait, ChunkMap_Clear just clears entries. We need to iterate and free.
    // Actually, let's just destroy the map and pool.

    // The map contains active chunks. The pool contains inactive chunks.
    // We need to free ALL of them.

    // 1. Free active chunks
    for (int i = 0; i < planet->chunkMap->capacity; i++) {
        ChunkMapEntry* entry = planet->chunkMap->buckets[i];
        while (entry) {
            Chunk_Free(entry->value);
            entry = entry->next;
        }
    }
    ChunkMap_Destroy(planet->chunkMap);

    // 2. Free pooled chunks
    ChunkPool_Destroy(planet->chunkPool); // This frees the chunks in the pool

    CubicQuadTree_Free(planet->quadtree);
    free(planet);
}
