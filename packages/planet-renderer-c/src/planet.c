#include "planet.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Convert ephemeral quadtree into a map of desired chunk keys
// Note: This creates a map with NULL chunk pointers - just the keys we want
static ChunkMap* QuadtreeToDesiredChunks(CubicQuadTree* tree) {
    ChunkMap* desiredMap = ChunkMap_Create();

    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(tree, &leafNodes, &leafCount);

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];

        // Generate key for this node
        char key[128];
        ChunkMap_MakeKey(key, node->center, node->size.x, node->faceIndex);

        // Put NULL chunk (we just want the key for diffing)
        ChunkMap_Put(desiredMap, key, NULL);
    }

    free(leafNodes);
    return desiredMap;
}

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin) {
    Planet* planet = (Planet*)malloc(sizeof(Planet));
    planet->radius = radius;
    planet->minCellSize = minCellSize;
    planet->minCellResolution = minCellResolution;
    planet->origin = origin;
    planet->lodDistanceComparisonValue = 1.5f;

    // Create persistent chunk storage
    planet->chunkMap = ChunkMap_Create();

    // Quadtree will be created/destroyed each frame in Planet_Update
    planet->quadtree = NULL;

    return planet;
}

void Planet_Update(Planet* planet, Vector3 cameraPosition) {
    // 1. Build ephemeral quadtree from scratch based on camera position
    CubicQuadTree* newTree = CubicQuadTree_Create(
        planet->radius,
        planet->minCellSize,
        planet->lodDistanceComparisonValue,
        planet->origin
    );

    // Subdivide quadtree based on camera position
    CubicQuadTree_Insert(newTree, cameraPosition);

    // 2. Convert quadtree leaves to desired chunks map
    ChunkMap* desiredChunks = QuadtreeToDesiredChunks(newTree);

    // 3. Diff against current chunks
    // Keep: chunks that exist in both current and desired
    // Create: chunks in desired but not in current
    // Destroy: chunks in current but not in desired

    ChunkMap* toKeep = ChunkMap_Intersection(planet->chunkMap, desiredChunks);
    ChunkMap* toCreate = ChunkMap_Difference(desiredChunks, planet->chunkMap);
    ChunkMap* toDestroy = ChunkMap_Difference(planet->chunkMap, desiredChunks);

    // 4. Destroy old chunks (camera moved away)
    ChunkMapEntry *entry, *tmp;
    HASH_ITER(hh, toDestroy->entries, entry, tmp) {
        if (entry->chunk) {
            Chunk_Free(entry->chunk);
        }
        ChunkMap_Remove(planet->chunkMap, entry->key, false);  // Already freed
    }

    // 5. Create new chunks (camera moved closer)
    HASH_ITER(hh, toCreate->entries, entry, tmp) {
        // Extract parameters from key and quadtree node
        // We need to find the corresponding quadtree node to get bounds/transform

        QuadtreeNode** leafNodes;
        int leafCount;
        CubicQuadTree_GetLeafNodes(newTree, &leafNodes, &leafCount);

        for (int i = 0; i < leafCount; i++) {
            QuadtreeNode* node = leafNodes[i];
            char nodeKey[128];
            ChunkMap_MakeKey(nodeKey, node->center, node->size.x, node->faceIndex);

            if (strcmp(nodeKey, entry->key) == 0) {
                // Found matching node - create chunk
                Chunk* chunk = Chunk_Create(
                    node->bounds.min,
                    node->size.x,
                    node->size.y,
                    planet->radius,
                    planet->minCellResolution,
                    planet->origin,
                    node->localToWorld
                );
                Chunk_Generate(chunk);
                ChunkMap_Put(planet->chunkMap, entry->key, chunk);
                break;
            }
        }

        free(leafNodes);
    }

    // 6. Update internal chunk map (toKeep already has the right chunks)
    // planet->chunkMap already updated via Remove/Put above

    // 7. Cleanup temporary maps and ephemeral quadtree
    ChunkMap_Free(toKeep, false);      // Don't free chunks (still in planet->chunkMap)
    ChunkMap_Free(toCreate, false);    // Don't free chunks (just created)
    ChunkMap_Free(toDestroy, false);   // Don't free chunks (already freed above)
    ChunkMap_Free(desiredChunks, false);  // No chunks to free (all NULL)
    CubicQuadTree_Free(newTree);       // Free ephemeral quadtree structure
}

void Planet_Draw(Planet* planet) {
    ChunkMapEntry *entry, *tmp;
    HASH_ITER(hh, planet->chunkMap->entries, entry, tmp) {
        if (entry->chunk) {
            Chunk_Draw(entry->chunk);
        }
    }
}

void Planet_Free(Planet* planet) {
    if (!planet) return;

    // Free all chunks from chunk map
    ChunkMap_Free(planet->chunkMap, true);  // true = also free chunks

    // Free ephemeral quadtree if it exists (shouldn't, but be safe)
    if (planet->quadtree) {
        CubicQuadTree_Free(planet->quadtree);
    }

    free(planet);
}
