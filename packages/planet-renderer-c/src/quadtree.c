#include "quadtree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// DJB2 hash variant for mixing floats/ints
static unsigned long long HashChunkKey(int faceId, float x, float y, float size) {
    unsigned long long hash = 5381;
    
    // Mix faceId
    hash = ((hash << 5) + hash) + faceId;
    
    // Mix floats as raw bytes to avoid precision issues if possible, 
    // but for now casting to int scaled might be safer or just raw bytes.
    // Let's use raw bytes of the float.
    unsigned int* xi = (unsigned int*)&x;
    unsigned int* yi = (unsigned int*)&y;
    unsigned int* si = (unsigned int*)&size;
    
    hash = ((hash << 5) + hash) + *xi;
    hash = ((hash << 5) + hash) + *yi;
    hash = ((hash << 5) + hash) + *si;
    
    return hash;
}

// Private helper to create a node
static QuadtreeNode* CreateNode(BoundingBox3 bounds, Matrix localToWorld, float planetRadius, Vector3 planetOrigin, int faceId) {
    QuadtreeNode* node = (QuadtreeNode*)malloc(sizeof(QuadtreeNode));
    node->bounds = bounds;
    for (int i = 0; i < 4; i++) node->children[i] = NULL;
    node->isLeaf = true;
    node->userData = NULL;
    node->localToWorld = localToWorld;
    node->faceId = faceId;
    
    node->center = BoundingBoxCenter(bounds);
    node->size = BoundingBoxSize(bounds);
    
    // Generate ID
    // We use bounds min x/y and size x (assuming square nodes) + faceId
    node->id = HashChunkKey(faceId, bounds.min.x, bounds.min.y, node->size.x);
    
    // Calculate sphere center
    Vector3 worldPos = Vector3Transform(node->center, localToWorld);
    Vector3 normalized = Vector3Normalize(worldPos);
    Vector3 scaled = Vector3Scale(normalized, planetRadius);
    node->sphereCenter = Vector3Add(scaled, planetOrigin);
    
    return node;
}

static void FreeNode(QuadtreeNode* node) {
    if (!node) return;
    for (int i = 0; i < 4; i++) {
        FreeNode(node->children[i]);
    }
    free(node);
}

Quadtree* Quadtree_Create(float size, float minNodeSize, float comparatorValue, Vector3 origin, Matrix localToWorld, int faceId) {
    Quadtree* tree = (Quadtree*)malloc(sizeof(Quadtree));
    tree->size = size;
    tree->minNodeSize = minNodeSize;
    tree->comparatorValue = comparatorValue;
    tree->origin = origin;
    tree->localToWorld = localToWorld;
    tree->faceId = faceId;
    
    BoundingBox3 bounds = {
        (Vector3){ -size, -size, 0 },
        (Vector3){ size, size, 0 }
    };
    
    tree->root = CreateNode(bounds, localToWorld, size, origin, faceId);
    
    return tree;
}

static void InsertRecursive(QuadtreeNode* node, Vector3 cameraPos, float minNodeSize, float comparatorValue, Matrix localToWorld, float planetRadius, Vector3 planetOrigin, QuadtreeSplitCallback onSplit) {
    float dist = Vector3Distance(node->sphereCenter, cameraPos);
    
    // Check split condition
    if (dist < node->size.x * comparatorValue && node->size.x > minNodeSize) {
        // Split
        if (node->isLeaf) {
            if (onSplit) {
                onSplit(node);
            }
            
            node->isLeaf = false;
            Vector3 center = node->center;
            Vector3 min = node->bounds.min;
            Vector3 max = node->bounds.max;
            
            // Create 4 children
            // Bottom Left
            BoundingBox3 b1 = { min, center };
            // Bottom Right
            BoundingBox3 b2 = { 
                (Vector3){ center.x, min.y, 0 },
                (Vector3){ max.x, center.y, 0 }
            };
            // Top Left
            BoundingBox3 b3 = {
                (Vector3){ min.x, center.y, 0 },
                (Vector3){ center.x, max.y, 0 }
            };
            // Top Right
            BoundingBox3 b4 = { center, max };
            
            node->children[0] = CreateNode(b1, localToWorld, planetRadius, planetOrigin, node->faceId);
            node->children[1] = CreateNode(b2, localToWorld, planetRadius, planetOrigin, node->faceId);
            node->children[2] = CreateNode(b3, localToWorld, planetRadius, planetOrigin, node->faceId);
            node->children[3] = CreateNode(b4, localToWorld, planetRadius, planetOrigin, node->faceId);
        }
        
        // Recurse
        for (int i = 0; i < 4; i++) {
            InsertRecursive(node->children[i], cameraPos, minNodeSize, comparatorValue, localToWorld, planetRadius, planetOrigin, onSplit);
        }
    }
}

void Quadtree_Insert(Quadtree* tree, Vector3 cameraPosition, QuadtreeSplitCallback onSplit) {
    InsertRecursive(tree->root, cameraPosition, tree->minNodeSize, tree->comparatorValue, tree->localToWorld, tree->size, tree->origin, onSplit);
}

static void GetLeafNodesRecursive(QuadtreeNode* node, QuadtreeNode*** outNodes, int* outCount, int* capacity) {
    if (node->isLeaf) {
        if (*outCount >= *capacity) {
            *capacity *= 2;
            *outNodes = (QuadtreeNode**)realloc(*outNodes, sizeof(QuadtreeNode*) * (*capacity));
        }
        (*outNodes)[*outCount] = node;
        (*outCount)++;
    } else {
        for (int i = 0; i < 4; i++) {
            GetLeafNodesRecursive(node->children[i], outNodes, outCount, capacity);
        }
    }
}

void Quadtree_GetLeafNodes(Quadtree* tree, QuadtreeNode*** outNodes, int* outCount) {
    int capacity = 128;
    *outNodes = (QuadtreeNode**)malloc(sizeof(QuadtreeNode*) * capacity);
    *outCount = 0;
    GetLeafNodesRecursive(tree->root, outNodes, outCount, &capacity);
}

void Quadtree_Free(Quadtree* tree) {
    FreeNode(tree->root);
    free(tree);
}
