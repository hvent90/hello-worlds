#ifndef MESH_H
#define MESH_H

#include <raylib.h>

Mesh create_plane_mesh_with_noise(float scale, int resolution);
Mesh create_plane_mesh(float scale, int resolution);
Mesh create_triangle_mesh(float scale);

#endif