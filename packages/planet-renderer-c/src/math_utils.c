#include "math_utils.h"

Vector3 Vector3AddScalar(Vector3 v, float scalar) {
    return (Vector3){ v.x + scalar, v.y + scalar, v.z + scalar };
}

Vector3 Vector3MultiplyScalar(Vector3 v, float scalar) {
    return (Vector3){ v.x * scalar, v.y * scalar, v.z * scalar };
}

Vector3 BoundingBoxCenter(BoundingBox3 box) {
    return (Vector3){
        (box.min.x + box.max.x) * 0.5f,
        (box.min.y + box.max.y) * 0.5f,
        (box.min.z + box.max.z) * 0.5f
    };
}

Vector3 BoundingBoxSize(BoundingBox3 box) {
    return (Vector3){
        box.max.x - box.min.x,
        box.max.y - box.min.y,
        box.max.z - box.min.z
    };
}
