#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <raylib.h>
#include <raymath.h>

// Box3 structure for bounding boxes
typedef struct Box3 {
    Vector3 min;
    Vector3 max;
} Box3;

// Box3 operations
Box3 Box3_Create(Vector3 min, Vector3 max);
Vector3 Box3_GetCenter(Box3 box);
Vector3 Box3_GetSize(Box3 box);
Box3 Box3_Clone(Box3 box);

// Additional Vector3 utilities
void Vector3_ApplyMatrix(Vector3* v, Matrix mat);
Vector3 Vector3_TransformWithMatrix(Vector3 v, Matrix mat);
float Vector3_DistanceSquared(Vector3 a, Vector3 b);

// Matrix utilities
Matrix Matrix_CreateTranslation(float x, float y, float z);
Matrix Matrix_CreateRotationX(float angle);
Matrix Matrix_CreateRotationY(float angle);
Matrix Matrix_Multiply(Matrix left, Matrix right);
Matrix Matrix_Invert(Matrix mat);

#endif // MATH_UTILS_H
