#ifndef MESH_H
#define MESH_H

#include <raylib.h>
#include "cube_face.h"

Mesh create_plane_mesh_with_noise(float scale, int resolution);
Mesh create_plane_mesh_with_noise_ws(Vector3 position, float scale,
                                     int resolution);
Mesh create_plane_mesh(float scale, int resolution);
Mesh create_triangle_mesh(float scale);

// Sphere patch mesh for cubic quadtree planet rendering
// face: which cube face this patch belongs to
// u_min/u_max/v_min/v_max: UV bounds on the face, range [-1, 1]
// radius: base radius of the sphere
// noise_scale: multiplier for terrain height displacement
// resolution: number of subdivisions per edge
// skirt_depth: how far skirt vertices extend toward planet center (0 = no skirt)
Mesh create_sphere_patch_mesh(CubeFace face, 
                               float u_min, float u_max, 
                               float v_min, float v_max,
                               float radius, float noise_scale,
                               int resolution, float skirt_depth);

#endif
