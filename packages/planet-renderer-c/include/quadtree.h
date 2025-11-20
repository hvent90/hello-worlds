#ifndef QUADTREE_H
#define QUADTREE_H

#include "math_utils.h"
#include <stdbool.h>

typedef struct QuadtreeNode {
    BoundingBox3 bounds;
    struct QuadtreeNode* children[4];
    Vector3 center;
    Vector3 sphereCenter;
    Vector3 size;
    bool isLeaf;
    void* userData; // For attaching Chunk*
    Matrix localToWorld; // Store transformation for chunk generation
} QuadtreeNode;

typedef struct Quadtree {
    QuadtreeNode* root;
    Matrix localToWorld;
    float size;
    float minNodeSize;
    Vector3 origin;
    float comparatorValue;
} Quadtree;

typedef void (*QuadtreeSplitCallback)(QuadtreeNode* node);

Quadtree* Quadtree_Create(float size, float minNodeSize, float comparatorValue, Vector3 origin, Matrix localToWorld);
void Quadtree_Insert(Quadtree* tree, Vector3 cameraPosition, QuadtreeSplitCallback onSplit);
void Quadtree_GetLeafNodes(Quadtree* tree, QuadtreeNode*** outNodes, int* outCount);
void Quadtree_Free(Quadtree* tree);

#endif // QUADTREE_H
