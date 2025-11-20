#include "cubic_quadtree.h"
#include <stdlib.h>
#include <raymath.h>

CubicQuadTree* CubicQuadTree_Create(float radius, float minNodeSize, float comparatorValue, Vector3 origin) {
    CubicQuadTree* tree = (CubicQuadTree*)malloc(sizeof(CubicQuadTree));
    
    Matrix transforms[6];
    
    // +Y (Top)
    transforms[0] = MatrixMultiply(MatrixRotateX(-PI / 2), MatrixTranslate(0, radius, 0));
    
    // -Y (Bottom)
    transforms[1] = MatrixMultiply(MatrixRotateX(PI / 2), MatrixTranslate(0, -radius, 0));
    
    // +X (Right)
    transforms[2] = MatrixMultiply(MatrixRotateY(PI / 2), MatrixTranslate(radius, 0, 0));
    
    // -X (Left)
    transforms[3] = MatrixMultiply(MatrixRotateY(-PI / 2), MatrixTranslate(-radius, 0, 0));
    
    // +Z (Front)
    transforms[4] = MatrixTranslate(0, 0, radius);
    
    // -Z (Back)
    transforms[5] = MatrixMultiply(MatrixRotateY(PI), MatrixTranslate(0, 0, -radius));
    
    for (int i = 0; i < 6; i++) {
        tree->faces[i] = Quadtree_CreateWithFace(radius, minNodeSize, comparatorValue, origin, transforms[i], i);  // Pass face index
    }

    return tree;
}

void CubicQuadTree_Insert(CubicQuadTree* tree, Vector3 cameraPosition) {
    for (int i = 0; i < 6; i++) {
        Quadtree_Insert(tree->faces[i], cameraPosition);
    }
}

void CubicQuadTree_GetLeafNodes(CubicQuadTree* tree, QuadtreeNode*** outNodes, int* outCount) {
    int capacity = 128 * 6; // Initial guess
    *outNodes = (QuadtreeNode**)malloc(sizeof(QuadtreeNode*) * capacity);
    *outCount = 0;
    
    for (int i = 0; i < 6; i++) {
        QuadtreeNode** faceNodes;
        int faceCount;
        Quadtree_GetLeafNodes(tree->faces[i], &faceNodes, &faceCount);
        
        // Resize if needed
        if (*outCount + faceCount > capacity) {
            capacity = (*outCount + faceCount) * 2;
            *outNodes = (QuadtreeNode**)realloc(*outNodes, sizeof(QuadtreeNode*) * capacity);
        }
        
        // Copy
        for (int j = 0; j < faceCount; j++) {
            (*outNodes)[*outCount + j] = faceNodes[j];
        }
        *outCount += faceCount;
        
        free(faceNodes);
    }
}

void CubicQuadTree_Free(CubicQuadTree* tree) {
    for (int i = 0; i < 6; i++) {
        Quadtree_Free(tree->faces[i]);
    }
    free(tree);
}
