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
    void* userData; // No longer used for chunks
    Matrix localToWorld; // Store transformation for chunk generation
    int faceIndex;   // NEW: Which cube face (0-5) this node belongs to
} QuadtreeNode;

typedef struct Quadtree {
    QuadtreeNode* root;
    Matrix localToWorld;
    float size;
    float minNodeSize;
    Vector3 origin;
    float comparatorValue;
    int faceIndex;  // NEW: Store face index on tree
} Quadtree;

Quadtree* Quadtree_Create(float size, float minNodeSize, float comparatorValue, Vector3 origin, Matrix localToWorld);
Quadtree* Quadtree_CreateWithFace(float size, float minNodeSize, float comparatorValue, Vector3 origin, Matrix localToWorld, int faceIndex);
void Quadtree_Insert(Quadtree* tree, Vector3 cameraPosition);
void Quadtree_GetLeafNodes(Quadtree* tree, QuadtreeNode*** outNodes, int* outCount);
void Quadtree_Free(Quadtree* tree);

#endif // QUADTREE_H
