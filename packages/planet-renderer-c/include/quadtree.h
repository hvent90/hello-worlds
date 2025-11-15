#ifndef QUADTREE_H
#define QUADTREE_H

#include <raylib.h>
#include "math_utils.h"

#define MAX_QUADTREE_CHILDREN 4

typedef struct QuadTreeNode {
    Box3 bounds;
    Vector3 center;
    Vector3 sphereCenter;
    Vector3 size;
    struct QuadTreeNode* children[MAX_QUADTREE_CHILDREN];
    int childCount;
    bool isRoot;
} QuadTreeNode;

typedef struct QuadTree {
    QuadTreeNode* root;
    Matrix localToWorld;
    float size;
    float minNodeSize;
    Vector3 origin;
    float comparatorValue;
} QuadTree;

// QuadTree operations
QuadTree* QuadTree_Create(Matrix localToWorld, float size, float minNodeSize, Vector3 origin, float comparatorValue);
void QuadTree_Insert(QuadTree* tree, Vector3 pos);
void QuadTree_GetChildren(QuadTree* tree, QuadTreeNode*** outNodes, int* outCount);
void QuadTree_Destroy(QuadTree* tree);

// Internal operations
QuadTreeNode* QuadTreeNode_Create(Box3 bounds, Matrix localToWorld, Vector3 origin, float size, bool isRoot);
void QuadTreeNode_CreateChildren(QuadTreeNode* node, Matrix localToWorld, Vector3 origin, float size);
void QuadTreeNode_Destroy(QuadTreeNode* node);
void QuadTreeNode_Insert(QuadTreeNode* node, Vector3 pos, float minNodeSize, float comparatorValue, Matrix localToWorld, Vector3 origin, float size);
float QuadTreeNode_DistanceToPoint(QuadTreeNode* node, Vector3 pos);
void QuadTreeNode_CollectLeaves(QuadTreeNode* node, QuadTreeNode*** nodes, int* count, int* capacity);

#endif // QUADTREE_H
