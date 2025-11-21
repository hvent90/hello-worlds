#include "planet.h"
#include <stdlib.h>
#include <stdio.h>

// No longer needed as we don't use the callback for freeing anymore
static void OnNodeSplit(QuadtreeNode* node) {
    // Empty for now, logic moved to Update
}

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin) {
    Planet* planet = (Planet*)malloc(sizeof(Planet));
    planet->radius = radius;
    planet->minCellSize = minCellSize;
    planet->minCellResolution = minCellResolution;
    planet->origin = origin;
    
    // Comparator value from TS default: 1.1 or similar.
    float comparatorValue = 1.5f; 
    
    // Initialize Quadtree (will be recreated every frame)
    planet->quadtree = CubicQuadTree_Create(radius, minCellSize, comparatorValue, origin);
    
    // Initialize Chunk Map and Pool
    planet->chunkMap = ChunkMap_Create(1024); // Initial capacity
    planet->chunkPool = ChunkPool_Create(256); // Initial capacity
    
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
                    node->localToWorld
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
                chunk->id = id;
                // Mesh needs regeneration
            }
            
            // Generate Mesh (TODO: Optimize to only upload if needed, but for now we generate)
            // We need to ensure we don't leak VRAM if we reuse a chunk that already has a mesh
            // Chunk_Generate handles VRAM upload. 
            // If it's a pooled chunk, it might already have buffers allocated? 
            // For now, let Chunk_Generate handle it. Ideally we separate CPU generation from GPU upload.
            Chunk_Generate(chunk);
            
            ChunkMap_Insert(newChunkMap, id, chunk);
            node->userData = chunk;
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
            Chunk_Draw(chunk, planet->surfaceColor, planet->wireframeColor, planet->lightingShader);

            // Calculate triangles: resolution^2 * 2
            totalTriangles += (chunk->resolution * chunk->resolution * 2);
        }
    }
    
    free(leafNodes);
    return totalTriangles;
}

void Planet_Free(Planet* planet) {
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
