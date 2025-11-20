#include "planet.h"
#include <stdlib.h>
#include <stdio.h>

static void OnNodeSplit(QuadtreeNode* node) {
    if (node->userData) {
        Chunk* chunk = (Chunk*)node->userData;
        Chunk_Free(chunk);
        node->userData = NULL;
    }
}

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin) {
    Planet* planet = (Planet*)malloc(sizeof(Planet));
    planet->radius = radius;
    planet->minCellSize = minCellSize;
    planet->minCellResolution = minCellResolution;
    planet->origin = origin;
    
    // Comparator value from TS default: 1.1 or similar.
    // "dist < size * comparatorValue"
    // If comparator is large, it splits sooner.
    float comparatorValue = 1.5f; 
    
    planet->quadtree = CubicQuadTree_Create(radius, minCellSize, comparatorValue, origin);
    
    return planet;
}

void Planet_Update(Planet* planet, Vector3 cameraPosition) {
    // Insert/Update Quadtree
    CubicQuadTree_Insert(planet->quadtree, cameraPosition, OnNodeSplit);
    
    // Get all leaf nodes
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);
    
    // Create chunks for new leaves
    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (!node->userData) {
            // Create chunk
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
            node->userData = chunk;
        }
    }
    
    free(leafNodes);
}

void Planet_Draw(Planet* planet) {
    // Get all leaf nodes to draw
    // Optimization: We could cache the list of active chunks instead of traversing every frame
    // But traversing is fast enough for now.
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);
    
    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            Chunk* chunk = (Chunk*)node->userData;
            Chunk_Draw(chunk);
        }
    }
    
    free(leafNodes);
}

void Planet_Free(Planet* planet) {
    // We need to free all chunks attached to nodes before freeing the tree
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);
    
    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            Chunk_Free((Chunk*)node->userData);
            node->userData = NULL;
        }
    }
    free(leafNodes);
    
    CubicQuadTree_Free(planet->quadtree);
    free(planet);
}
