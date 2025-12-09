#include "cube_face.h"
#include "noise.h"
#include <math.h>

// Get the local coordinate axes for each cube face
// These define how UV maps to 3D space for each face
void cube_face_get_axes(CubeFace face, Vector3 *out_right, Vector3 *out_up, Vector3 *out_forward) {
    switch (face) {
        case CUBE_FACE_POS_X:  // +X face: looking from +X toward origin
            *out_right   = (Vector3){ 0.0f,  0.0f, -1.0f};  // U maps to -Z
            *out_up      = (Vector3){ 0.0f,  1.0f,  0.0f};  // V maps to +Y
            *out_forward = (Vector3){ 1.0f,  0.0f,  0.0f};  // Face normal is +X
            break;
        case CUBE_FACE_NEG_X:  // -X face: looking from -X toward origin
            *out_right   = (Vector3){ 0.0f,  0.0f,  1.0f};  // U maps to +Z
            *out_up      = (Vector3){ 0.0f,  1.0f,  0.0f};  // V maps to +Y
            *out_forward = (Vector3){-1.0f,  0.0f,  0.0f};  // Face normal is -X
            break;
        case CUBE_FACE_POS_Y:  // +Y face (top): looking down from +Y
            *out_right   = (Vector3){ 1.0f,  0.0f,  0.0f};  // U maps to +X
            *out_up      = (Vector3){ 0.0f,  0.0f,  1.0f};  // V maps to +Z
            *out_forward = (Vector3){ 0.0f,  1.0f,  0.0f};  // Face normal is +Y
            break;
        case CUBE_FACE_NEG_Y:  // -Y face (bottom): looking up from -Y
            *out_right   = (Vector3){ 1.0f,  0.0f,  0.0f};  // U maps to +X
            *out_up      = (Vector3){ 0.0f,  0.0f, -1.0f};  // V maps to -Z
            *out_forward = (Vector3){ 0.0f, -1.0f,  0.0f};  // Face normal is -Y
            break;
        case CUBE_FACE_POS_Z:  // +Z face (front): looking from +Z toward origin
            *out_right   = (Vector3){ 1.0f,  0.0f,  0.0f};  // U maps to +X
            *out_up      = (Vector3){ 0.0f,  1.0f,  0.0f};  // V maps to +Y
            *out_forward = (Vector3){ 0.0f,  0.0f,  1.0f};  // Face normal is +Z
            break;
        case CUBE_FACE_NEG_Z:  // -Z face (back): looking from -Z toward origin
            *out_right   = (Vector3){-1.0f,  0.0f,  0.0f};  // U maps to -X
            *out_up      = (Vector3){ 0.0f,  1.0f,  0.0f};  // V maps to +Y
            *out_forward = (Vector3){ 0.0f,  0.0f, -1.0f};  // Face normal is -Z
            break;
    }
}

// Check if a cube face needs its triangle winding order flipped
// This is determined by whether (right × up) points in the same direction as forward
// If they point in opposite directions, the default winding produces inward-facing triangles
bool cube_face_needs_flip(CubeFace face) {
    Vector3 right, up, forward;
    cube_face_get_axes(face, &right, &up, &forward);
    
    // Compute right × up
    Vector3 cross = Vector3CrossProduct(right, up);
    
    // If dot(cross, forward) > 0, they point in the same direction
    // The original winding order (v0, v2, v1) was designed for faces where cross points opposite to forward
    // So we need to flip when they point in the SAME direction
    float dot = cross.x * forward.x + cross.y * forward.y + cross.z * forward.z;
    
    return dot > 0.0f;
}

Vector3 cube_face_to_cube_point(CubeFace face, float u, float v) {
    Vector3 right, up, forward;
    cube_face_get_axes(face, &right, &up, &forward);
    
    // Combine: forward gives the face offset (±1), right/up give the UV position
    Vector3 point = {
        forward.x + right.x * u + up.x * v,
        forward.y + right.y * u + up.y * v,
        forward.z + right.z * u + up.z * v
    };
    
    return point;
}

Vector3 cube_point_to_sphere(Vector3 cube_point) {
    return Vector3Normalize(cube_point);
}

Vector3 cube_face_to_sphere_point(CubeFace face, float u, float v, float radius) {
    Vector3 cube_point = cube_face_to_cube_point(face, u, v);
    Vector3 sphere_normal = cube_point_to_sphere(cube_point);
    return Vector3Scale(sphere_normal, radius);
}

Vector3 cube_face_to_sphere_point_with_normal(CubeFace face, float u, float v,
                                               float radius, float noise_scale,
                                               Vector3 *out_normal) {
    Vector3 cube_point = cube_face_to_cube_point(face, u, v);
    Vector3 sphere_normal = cube_point_to_sphere(cube_point);
    
    // Store the surface normal (before displacement, but after sphere projection)
    if (out_normal) {
        *out_normal = sphere_normal;
    }
    
    // Sample noise using the sphere normal (ensures seamless noise across cube edges)
    float noise = evaluate_moon_noise_3d(sphere_normal.x, sphere_normal.y, sphere_normal.z);
    
    // Apply radial displacement
    float displaced_radius = radius + noise * noise_scale;
    
    return Vector3Scale(sphere_normal, displaced_radius);
}
