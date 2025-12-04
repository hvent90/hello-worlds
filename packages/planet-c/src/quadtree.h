#include <raylib.h>
#include <raymath.h>

typedef struct {
  Matrix local_to_world;
  float size;
  float min_node_size;
  Vector3 origin;
  float comparator_value;
} QuadTreeParams;

typedef struct {
  struct Node *children[4];
  BoundingBox bounds;
  Vector3 sphere_center;
  Vector3 size;
  bool root;
} Node;

const Vector3 temp_vector3 = {0};

typedef struct {
  Node *root;
  Vector3 origin;
} Quadtree;
