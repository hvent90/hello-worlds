#ifndef CUBIC_QUADTREE_H
#define CUBIC_QUADTREE_H

#include "quadtree.h"

typedef struct CubicQuadTree {
    Quadtree* faces[6];
} CubicQuadTree;

CubicQuadTree* CubicQuadTree_Create(float radius, float minNodeSize, float comparatorValue, Vector3 origin);
void CubicQuadTree_Insert(CubicQuadTree* tree, Vector3 cameraPosition, QuadtreeSplitCallback onSplit);
void CubicQuadTree_GetLeafNodes(CubicQuadTree* tree, QuadtreeNode*** outNodes, int* outCount);
void CubicQuadTree_Free(CubicQuadTree* tree);

#endif // CUBIC_QUADTREE_H
