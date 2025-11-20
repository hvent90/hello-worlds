#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <raylib.h>
#include <raymath.h>

// Basic Vector3 extensions if needed
Vector3 Vector3AddScalar(Vector3 v, float scalar);
Vector3 Vector3MultiplyScalar(Vector3 v, float scalar);

// Bounding Box helpers
typedef struct {
    Vector3 min;
    Vector3 max;
} BoundingBox3;

Vector3 BoundingBoxCenter(BoundingBox3 box);
Vector3 BoundingBoxSize(BoundingBox3 box);

#endif // MATH_UTILS_H
