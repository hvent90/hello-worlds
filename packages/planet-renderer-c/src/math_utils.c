#include "math_utils.h"
#include <math.h>
#include <string.h>

// Box3 operations
Box3 Box3_Create(Vector3 min, Vector3 max) {
    Box3 box;
    box.min = min;
    box.max = max;
    return box;
}

Vector3 Box3_GetCenter(Box3 box) {
    return (Vector3){
        (box.min.x + box.max.x) * 0.5f,
        (box.min.y + box.max.y) * 0.5f,
        (box.min.z + box.max.z) * 0.5f
    };
}

Vector3 Box3_GetSize(Box3 box) {
    return (Vector3){
        box.max.x - box.min.x,
        box.max.y - box.min.y,
        box.max.z - box.min.z
    };
}

Box3 Box3_Clone(Box3 box) {
    return box;
}

// Vector3 utilities
void Vector3_ApplyMatrix(Vector3* v, Matrix mat) {
    *v = Vector3Transform(*v, mat);
}

Vector3 Vector3_TransformWithMatrix(Vector3 v, Matrix mat) {
    return Vector3Transform(v, mat);
}

float Vector3_DistanceSquared(Vector3 a, Vector3 b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;
    return dx * dx + dy * dy + dz * dz;
}

// Matrix utilities
Matrix Matrix_CreateTranslation(float x, float y, float z) {
    return MatrixTranslate(x, y, z);
}

Matrix Matrix_CreateRotationX(float angle) {
    return MatrixRotateX(angle);
}

Matrix Matrix_CreateRotationY(float angle) {
    return MatrixRotateY(angle);
}

Matrix Matrix_Multiply(Matrix left, Matrix right) {
    return MatrixMultiply(left, right);
}

Matrix Matrix_Invert(Matrix mat) {
    return MatrixInvert(mat);
}
