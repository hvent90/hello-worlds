#ifndef CUBIC_QUADTREE_H
#define CUBIC_QUADTREE_H

#include <raylib.h>
#include "quadtree.h"

#define CUBE_FACES 6

typedef struct CubicQuadTreeSide {
    Matrix transform;
    Matrix worldToLocal;
    QuadTree* quadtree;
} CubicQuadTreeSide;

typedef struct CubicQuadTree {
    CubicQuadTreeSide sides[CUBE_FACES];
    float radius;
    float minNodeSize;
    Vector3 origin;
    float comparatorValue;
} CubicQuadTree;

// CubicQuadTree operations
CubicQuadTree* CubicQuadTree_Create(float radius, float minNodeSize, Vector3 origin, float comparatorValue);
void CubicQuadTree_Insert(CubicQuadTree* tree, Vector3 pos);
void CubicQuadTree_GetSides(CubicQuadTree* tree, CubicQuadTreeSide** outSides);
void CubicQuadTree_Destroy(CubicQuadTree* tree);

#endif // CUBIC_QUADTREE_H
