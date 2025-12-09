#ifndef CUBE_FACE_H
#define CUBE_FACE_H

#include <raymath.h>
#include <stdbool.h>

// Enum for the 6 faces of a cube
typedef enum {
    CUBE_FACE_POS_X = 0,  // +X (right)
    CUBE_FACE_NEG_X = 1,  // -X (left)
    CUBE_FACE_POS_Y = 2,  // +Y (top)
    CUBE_FACE_NEG_Y = 3,  // -Y (bottom)
    CUBE_FACE_POS_Z = 4,  // +Z (front)
    CUBE_FACE_NEG_Z = 5   // -Z (back)
} CubeFace;

#define CUBE_FACE_COUNT 6

// Convert local UV coordinates [-1,1] on a cube face to a point on the unit cube
// u and v are in range [-1, 1] where (0,0) is the face center
Vector3 cube_face_to_cube_point(CubeFace face, float u, float v);

// Normalize a cube point to project it onto the unit sphere
Vector3 cube_point_to_sphere(Vector3 cube_point);

// Combined helper: (face, u, v) -> sphere position at given radius with noise
// Returns the final world position including radial noise displacement
Vector3 cube_face_to_sphere_point(CubeFace face, float u, float v, float radius);

// Get sphere position with noise displacement and output the surface normal
Vector3 cube_face_to_sphere_point_with_normal(CubeFace face, float u, float v, 
                                               float radius, float noise_scale,
                                               Vector3 *out_normal);

// Get the local axes for a cube face (for computing tangent space)
void cube_face_get_axes(CubeFace face, Vector3 *out_right, Vector3 *out_up, Vector3 *out_forward);

// Check if a cube face needs its triangle winding order flipped
// Returns true if triangles should use reversed winding (swap v1 and v2)
bool cube_face_needs_flip(CubeFace face);

#endif // CUBE_FACE_H
