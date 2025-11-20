#ifndef PLANET_H
#define PLANET_H

#include "cubic_quadtree.h"
#include "chunk.h"
#include <raylib.h>

typedef struct Planet {
    CubicQuadTree* quadtree;
    float radius;
    float minCellSize;
    int minCellResolution;
    Vector3 origin;
    // We don't need to store chunks array explicitly if we traverse the tree to draw
} Planet;

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin);
void Planet_Update(Planet* planet, Vector3 cameraPosition);
void Planet_Draw(Planet* planet);
void Planet_Free(Planet* planet);

#endif // PLANET_H
