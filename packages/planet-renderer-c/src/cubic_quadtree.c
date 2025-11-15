#include "cubic_quadtree.h"
#include <stdlib.h>
#include <math.h>

CubicQuadTree* CubicQuadTree_Create(float radius, float minNodeSize, Vector3 origin, float comparatorValue) {
    CubicQuadTree* tree = (CubicQuadTree*)malloc(sizeof(CubicQuadTree));
    tree->radius = radius;
    tree->minNodeSize = minNodeSize;
    tree->origin = origin;
    tree->comparatorValue = comparatorValue;

    Matrix transforms[CUBE_FACES];

    // +Y (top)
    transforms[0] = Matrix_Multiply(
        Matrix_CreateTranslation(0, radius, 0),
        Matrix_CreateRotationX(-PI / 2.0f)
    );

    // -Y (bottom)
    transforms[1] = Matrix_Multiply(
        Matrix_CreateTranslation(0, -radius, 0),
        Matrix_CreateRotationX(PI / 2.0f)
    );

    // +X (right)
    transforms[2] = Matrix_Multiply(
        Matrix_CreateTranslation(radius, 0, 0),
        Matrix_CreateRotationY(PI / 2.0f)
    );

    // -X (left)
    transforms[3] = Matrix_Multiply(
        Matrix_CreateTranslation(-radius, 0, 0),
        Matrix_CreateRotationY(-PI / 2.0f)
    );

    // +Z (front)
    transforms[4] = Matrix_CreateTranslation(0, 0, radius);

    // -Z (back)
    transforms[5] = Matrix_Multiply(
        Matrix_CreateTranslation(0, 0, -radius),
        Matrix_CreateRotationY(PI)
    );

    for (int i = 0; i < CUBE_FACES; i++) {
        tree->sides[i].transform = transforms[i];
        tree->sides[i].worldToLocal = Matrix_Invert(transforms[i]);
        tree->sides[i].quadtree = QuadTree_Create(
            transforms[i],
            radius,
            minNodeSize,
            origin,
            comparatorValue
        );
    }

    return tree;
}

void CubicQuadTree_Insert(CubicQuadTree* tree, Vector3 pos) {
    for (int i = 0; i < CUBE_FACES; i++) {
        QuadTree_Insert(tree->sides[i].quadtree, pos);
    }
}

void CubicQuadTree_GetSides(CubicQuadTree* tree, CubicQuadTreeSide** outSides) {
    *outSides = tree->sides;
}

void CubicQuadTree_Destroy(CubicQuadTree* tree) {
    if (!tree) return;

    for (int i = 0; i < CUBE_FACES; i++) {
        QuadTree_Destroy(tree->sides[i].quadtree);
    }

    free(tree);
}
