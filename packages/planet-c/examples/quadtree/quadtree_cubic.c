#include <float.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// clang-format off
#include "shadow.h"
#include "camera.h"
#include "quadtree.h"
#include "mesh.h"
#include "cube_face.h"
#include "mesh_thread_pool.h"
// clang-format on

// Planet configuration
const float PLANET_RADIUS = 1700000.0f;  // 1000 km
const float NOISE_SCALE = 3000.0f;       // Height variation in meters
const float SKIRT_DEPTH = 4000.0f;        // Skirt depth to hide seams (must be > NOISE_SCALE)
#define MAX_DEPTH 14

// Tweakable parameters
int chunk_resolution = 32;
int min_depth = 0;

// Planet rotation state
float planet_rotation_speed = 0.0f; // Degrees per second
float planet_rotation_angle = 0.0f;

// Forward declare MeshRequest for the struct
struct MeshRequest;

// Mesh data attached to each quadtree node
typedef struct {
  Mesh mesh;
  CubeFace face;
  float u_min, u_max;
  float v_min, v_max;
  float center_elevation;  // Height at center for LOD distance calc
  uint32_t generation;     // For validating async request node pointers

  // Async state - direct pointers to pending requests
  struct MeshRequest* pending_child[4];
  struct MeshRequest* pending_merge;
} CubicQuadTreeMeshData;

// Global thread pool
static MeshThreadPool g_mesh_pool;
static uint32_t g_generation_counter = 1;

// ===== Mesh Data Validity Registry =====
// Tracks which mesh_data pointers are currently valid to prevent dereferencing freed memory
#define MESH_REGISTRY_SIZE 4096
static void* g_valid_mesh_data[MESH_REGISTRY_SIZE];
static int g_valid_mesh_count = 0;

static void registry_add(void* ptr) {
  if (g_valid_mesh_count < MESH_REGISTRY_SIZE) {
    g_valid_mesh_data[g_valid_mesh_count++] = ptr;
  }
}

static void registry_remove(void* ptr) {
  for (int i = 0; i < g_valid_mesh_count; i++) {
    if (g_valid_mesh_data[i] == ptr) {
      // Swap with last and decrement count
      g_valid_mesh_data[i] = g_valid_mesh_data[--g_valid_mesh_count];
      return;
    }
  }
}

static bool registry_contains(void* ptr) {
  for (int i = 0; i < g_valid_mesh_count; i++) {
    if (g_valid_mesh_data[i] == ptr) {
      return true;
    }
  }
  return false;
}

// Debug counters
static int g_pending_split_count = 0;
static int g_pending_requests_count = 0;
static int g_requests_sent_this_frame = 0;
static int g_splits_cancelled_this_frame = 0;
static int g_results_processed_this_frame = 0;

// Root structure holding all 6 face quadtrees
typedef struct {
  QuadTreeNode *faces[CUBE_FACE_COUNT];
  float radius;
  float noise_scale;
} CubicQuadTree;

// ===== Shadow Map Helper Structures =====

typedef struct {
  float minX, maxX;
  float minY, maxY;
  float minZ, maxZ;
} FrustumBounds;

// Compute stable up vector for a given direction (avoids gimbal lock)
Vector3 compute_stable_up(Vector3 direction) {
  const float threshold = 0.99f;
  Vector3 world_up = {0.0f, 1.0f, 0.0f};
  Vector3 world_forward = {0.0f, 0.0f, 1.0f};

  float dot = fabsf(Vector3DotProduct(direction, world_up));
  Vector3 reference = (dot > threshold) ? world_forward : world_up;

  Vector3 right = Vector3Normalize(Vector3CrossProduct(direction, reference));
  return Vector3CrossProduct(right, direction);
}

// Compute 8 corners of view frustum between nearDist and farDist
void get_frustum_corners(Camera3D cam, float nearDist, float farDist,
                         float aspectRatio, Vector3 corners[8]) {
  Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
  Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
  Vector3 up = Vector3CrossProduct(right, forward);

  float fovRad = cam.fovy * DEG2RAD;
  float tanHalfFov = tanf(fovRad * 0.5f);

  float nearHeight = nearDist * tanHalfFov * 2.0f;
  float nearWidth = nearHeight * aspectRatio;
  float farHeight = farDist * tanHalfFov * 2.0f;
  float farWidth = farHeight * aspectRatio;

  Vector3 nearCenter = Vector3Add(cam.position, Vector3Scale(forward, nearDist));
  Vector3 farCenter = Vector3Add(cam.position, Vector3Scale(forward, farDist));

  // Near plane corners (0-3)
  corners[0] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, nearHeight * 0.5f),
                                                  Vector3Scale(right, -nearWidth * 0.5f)));
  corners[1] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, nearHeight * 0.5f),
                                                  Vector3Scale(right, nearWidth * 0.5f)));
  corners[2] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, -nearHeight * 0.5f),
                                                  Vector3Scale(right, -nearWidth * 0.5f)));
  corners[3] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, -nearHeight * 0.5f),
                                                  Vector3Scale(right, nearWidth * 0.5f)));

  // Far plane corners (4-7)
  corners[4] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, farHeight * 0.5f),
                                                 Vector3Scale(right, -farWidth * 0.5f)));
  corners[5] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, farHeight * 0.5f),
                                                 Vector3Scale(right, farWidth * 0.5f)));
  corners[6] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, -farHeight * 0.5f),
                                                 Vector3Scale(right, -farWidth * 0.5f)));
  corners[7] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, -farHeight * 0.5f),
                                                 Vector3Scale(right, farWidth * 0.5f)));
}

// Transform frustum corners to light space and compute axis-aligned bounding box
FrustumBounds compute_light_frustum_bounds(Vector3 corners[8], Vector3 lightDir,
                                           Vector3 lightUp) {
  Vector3 lightRight = Vector3CrossProduct(lightDir, lightUp);

  FrustumBounds bounds = {.minX = FLT_MAX, .maxX = -FLT_MAX,
                          .minY = FLT_MAX, .maxY = -FLT_MAX,
                          .minZ = FLT_MAX, .maxZ = -FLT_MAX};

  for (int i = 0; i < 8; i++) {
    float x = Vector3DotProduct(corners[i], lightRight);
    float y = Vector3DotProduct(corners[i], lightUp);
    float z = Vector3DotProduct(corners[i], lightDir);

    bounds.minX = fminf(bounds.minX, x);
    bounds.maxX = fmaxf(bounds.maxX, x);
    bounds.minY = fminf(bounds.minY, y);
    bounds.maxY = fmaxf(bounds.maxY, y);
    bounds.minZ = fminf(bounds.minZ, z);
    bounds.maxZ = fmaxf(bounds.maxZ, z);
  }

  return bounds;
}

// Forward declarations
void cleanup_mesh_data(void *user_data);
void *create_child_mesh_data(QuadTreeNode *parent, QuadTreeNode *child, void *parent_user_data);
void *recreate_parent_mesh_data(QuadTreeNode *node);
int draw_cubic_quadtree(CubicQuadTree *tree, Vector3 local_camera_pos, Material material, Matrix transform);

// ===== Mesh Data Lifecycle =====

void cleanup_mesh_data(void *user_data) {
  if (user_data == NULL) return;
  CubicQuadTreeMeshData *mesh_data = (CubicQuadTreeMeshData *)user_data;

  // Cancel any pending subdivision requests
  for (int i = 0; i < 4; i++) {
    if (mesh_data->pending_child[i] != NULL) {
      mesh_data->pending_child[i]->node = NULL;  // Null out before cancel to prevent dangling access
      mesh_pool_cancel(mesh_data->pending_child[i]);
      mesh_data->pending_child[i] = NULL;
    }
  }

  // Cancel any pending merge request
  if (mesh_data->pending_merge != NULL) {
    mesh_data->pending_merge->node = NULL;  // Null out before cancel to prevent dangling access
    mesh_pool_cancel(mesh_data->pending_merge);
    mesh_data->pending_merge = NULL;
  }

  // Remove from validity registry BEFORE freeing
  registry_remove(mesh_data);

  UnloadMesh(mesh_data->mesh);
  free(mesh_data);
}

// Calculate sphere surface position including noise at UV center
float get_center_elevation(CubeFace face, float u_min, float u_max, float v_min, float v_max, 
                           float radius, float noise_scale) {
  float u_center = (u_min + u_max) * 0.5f;
  float v_center = (v_min + v_max) * 0.5f;
  
  Vector3 normal;
  Vector3 pos = cube_face_to_sphere_point_with_normal(face, u_center, v_center, 
                                                       radius, noise_scale, &normal);
  return Vector3Length(pos);
}

void *create_child_mesh_data(QuadTreeNode *parent, QuadTreeNode *child, void *parent_user_data) {
  if (parent_user_data == NULL) return NULL;

  CubicQuadTreeMeshData *parent_data = (CubicQuadTreeMeshData *)parent_user_data;

  // Determine child index (0-3)
  short index = -1;
  for (unsigned short i = 0; i < 4; i++) {
    if (parent->children[i] == child) {
      index = i;
      break;
    }
  }
  if (index == -1) return NULL;

  // Calculate child UV bounds (subdivide parent's UV range)
  float u_mid = (parent_data->u_min + parent_data->u_max) * 0.5f;
  float v_mid = (parent_data->v_min + parent_data->v_max) * 0.5f;

  float u_min, u_max, v_min, v_max;
  switch (index) {
    case 0: u_min = parent_data->u_min; u_max = u_mid; v_min = parent_data->v_min; v_max = v_mid; break;
    case 1: u_min = u_mid; u_max = parent_data->u_max; v_min = parent_data->v_min; v_max = v_mid; break;
    case 2: u_min = parent_data->u_min; u_max = u_mid; v_min = v_mid; v_max = parent_data->v_max; break;
    case 3: u_min = u_mid; u_max = parent_data->u_max; v_min = v_mid; v_max = parent_data->v_max; break;
    default: return NULL;
  }

  // Get mesh from the pending request
  MeshRequest *req = parent_data->pending_child[index];
  Mesh mesh = {0};

  if (req != NULL && req->result.vertices != NULL) {
    // Upload CPU mesh to GPU
    mesh = upload_mesh_gpu(req->result);
    // Free the request (CPU mesh ownership transferred to GPU mesh)
    mesh_pool_free_request(req);
    parent_data->pending_child[index] = NULL;
  }

  if (mesh.vertexCount == 0) {
    // Synchronous fallback (e.g., initial load or error)
    mesh = create_sphere_patch_mesh(parent_data->face, u_min, u_max, v_min, v_max,
                                    PLANET_RADIUS, NOISE_SCALE,
                                    chunk_resolution, SKIRT_DEPTH);
  }

  CubicQuadTreeMeshData *child_data = malloc(sizeof(CubicQuadTreeMeshData));
  if (child_data == NULL) {
    UnloadMesh(mesh);
    return NULL;
  }
  registry_add(child_data);  // Track as valid

  child_data->mesh = mesh;
  child_data->face = parent_data->face;
  child_data->u_min = u_min;
  child_data->u_max = u_max;
  child_data->v_min = v_min;
  child_data->v_max = v_max;
  child_data->center_elevation = get_center_elevation(parent_data->face, u_min, u_max,
                                                       v_min, v_max, PLANET_RADIUS, NOISE_SCALE);
  child_data->generation = g_generation_counter++;

  // Init async state
  for (int i = 0; i < 4; i++) child_data->pending_child[i] = NULL;
  child_data->pending_merge = NULL;

  return child_data;
}

void *recreate_parent_mesh_data(QuadTreeNode *node) {
  // For merging back - would need to track face/UV bounds in the node
  // For now, return NULL to skip (we'll implement this if needed)
  return NULL;
}

// ===== Drawing =====

int draw_quadtree_meshes_recursive(QuadTreeNode *node, Material material, Matrix transform) {
  if (node == NULL) return 0;
  
  // Check if leaf
  bool is_leaf = true;
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] != NULL) {
      is_leaf = false;
      break;
    }
  }
  
  int count = 0;
  if (is_leaf && node->user_data != NULL) {
    CubicQuadTreeMeshData *mesh_data = (CubicQuadTreeMeshData *)node->user_data;
    DrawMesh(mesh_data->mesh, material, transform);
    count = mesh_data->mesh.vertexCount;
  } else {
    for (unsigned short i = 0; i < 4; i++) {
      count += draw_quadtree_meshes_recursive(node->children[i], material, transform);
    }
  }
  return count;
}

// Get face normal direction
Vector3 get_face_normal(CubeFace face) {
  switch (face) {
    case CUBE_FACE_POS_X: return (Vector3){1.0f, 0.0f, 0.0f};
    case CUBE_FACE_NEG_X: return (Vector3){-1.0f, 0.0f, 0.0f};
    case CUBE_FACE_POS_Y: return (Vector3){0.0f, 1.0f, 0.0f};
    case CUBE_FACE_NEG_Y: return (Vector3){0.0f, -1.0f, 0.0f};
    case CUBE_FACE_POS_Z: return (Vector3){0.0f, 0.0f, 1.0f};
    case CUBE_FACE_NEG_Z: return (Vector3){0.0f, 0.0f, -1.0f};
    default: return (Vector3){0.0f, 1.0f, 0.0f};
  }
}

// Horizon-based visibility check for a cube face
// Returns true if the face is potentially visible from camera position
bool is_face_visible(CubeFace face, Vector3 camera_pos, float planet_radius) {
  Vector3 face_normal = get_face_normal(face);
  
  float distance = Vector3Length(camera_pos);
  if (distance < 1.0f) distance = 1.0f;  // Avoid divide by zero
  
  Vector3 cam_dir = Vector3Scale(camera_pos, 1.0f / distance);
  
  // Horizon angle: sin(theta) = R / distance (when distance > R)
  // This determines how far "around" the sphere we can see
  float sin_horizon = (distance > planet_radius) 
                      ? (planet_radius / distance) 
                      : 1.0f;  // On/below surface, can see up to 90 deg around
  
  // Dot product gives cos of angle between camera direction and face normal
  float dot = Vector3DotProduct(cam_dir, face_normal);
  
  // Face is visible if: dot > -(sin_horizon + margin)
  // The 0.71 margin accounts for cube face angular width (cos(45 deg) ~= 0.707)
  // Each face spans ~90 deg on the sphere, so we need to see its edge to include it
  return dot > -(sin_horizon + 0.71f);
}

int draw_cubic_quadtree(CubicQuadTree *tree, Vector3 local_camera_pos, Material material, Matrix transform) {
  int total_vertices = 0;
  for (int i = 0; i < CUBE_FACE_COUNT; i++) {
    if (tree->faces[i] != NULL && is_face_visible((CubeFace)i, local_camera_pos, tree->radius)) {
      total_vertices += draw_quadtree_meshes_recursive(tree->faces[i], material, transform);
    }
  }
  return total_vertices;
}

// ===== Custom LOD for 3D distance =====

// LOD thresholds in screen pixels
// With 1000km planet and 32x32 mesh resolution, use high thresholds
const float SUBDIVIDE_PIXEL_THRESHOLD = 4000.0f;  // Subdivide when chunk covers more than this many pixels
const float MERGE_PIXEL_THRESHOLD = 1900.0f;      // Merge when chunk covers less than this many pixels (must be < SUBDIVIDE / 2)

// Check distance from camera to closest point on chunk (samples corners + center)
float get_chunk_distance(CubicQuadTreeMeshData *data, Vector3 camera_pos) {
  // Sample 5 points: 4 corners + center
  float u_vals[3] = { data->u_min, (data->u_min + data->u_max) * 0.5f, data->u_max };
  float v_vals[3] = { data->v_min, (data->v_min + data->v_max) * 0.5f, data->v_max };

  float min_dist = FLT_MAX;

  // Check corners (0,0), (0,2), (2,0), (2,2) and center (1,1)
  int samples[5][2] = { {0,0}, {0,2}, {2,0}, {2,2}, {1,1} };

  for (int i = 0; i < 5; i++) {
    float u = u_vals[samples[i][0]];
    float v = v_vals[samples[i][1]];
    Vector3 point = cube_face_to_sphere_point(data->face, u, v, PLANET_RADIUS);
    float dist = Vector3Distance(camera_pos, point);
    if (dist < min_dist) min_dist = dist;
  }

  return min_dist;
}

// Get approximate world-space size of a chunk
float get_chunk_world_size(CubicQuadTreeMeshData *data) {
  // Chunk covers (u_max - u_min) in UV space, which is roughly that fraction of the hemisphere
  // Arc length ≈ angle * radius, where angle ≈ uv_size (since UV goes from -1 to 1 = 180 degrees = π radians)
  float uv_size = (data->u_max - data->u_min);  // 2.0 for root, 1.0 for depth 1, etc.
  // uv_size of 2.0 = half the sphere (one cube face), so arc length ≈ π * R / 2
  return uv_size * PLANET_RADIUS * 1.57f;  // Approximate using π/2
}

// Calculate how many screen pixels a world-space object covers at a given distance
float get_screen_space_size(float world_size, float distance, float fov_degrees, int screen_height) {
  // Avoid divide by zero
  if (distance < 1.0f) distance = 1.0f;
  
  // Convert FOV to radians and compute projection factor
  float fov_radians = fov_degrees * DEG2RAD;
  float projection_factor = (float)screen_height / (2.0f * tanf(fov_radians * 0.5f));
  
  // Calculate how many pixels this world-space size covers at this distance
  return (world_size / distance) * projection_factor;
}

// Check if a node has any pending subdivision requests
static bool has_pending_split(CubicQuadTreeMeshData *data) {
  for (int i = 0; i < 4; i++) {
    if (data->pending_child[i] != NULL) return true;
  }
  return false;
}

// Process single face quadtree for LOD based on screen-space error
void process_face_lod(QuadTreeNode *node, Vector3 camera_pos, float camera_fov, int screen_height, int max_depth) {
  if (node == NULL) return;

  // Check if this is a leaf node FIRST (before checking user_data)
  bool is_leaf = true;
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] != NULL) {
      is_leaf = false;
      break;
    }
  }

  if (!is_leaf) {
    // Non-leaf: recurse into children (user_data is NULL for non-leaves, that's correct)
    for (unsigned short i = 0; i < 4; i++) {
      process_face_lod(node->children[i], camera_pos, camera_fov, screen_height, max_depth);
    }
    return;
  }

  // Leaf node: must have user_data
  if (node->user_data == NULL) {
    printf("ERROR: Leaf at depth %d has NULL user_data!\n", node->depth);
    return;
  }

  CubicQuadTreeMeshData *data = (CubicQuadTreeMeshData *)node->user_data;

  // Debug tracking
  if (has_pending_split(data)) g_pending_split_count++;
  for (int i = 0; i < 4; i++) {
    if (data->pending_child[i] != NULL) g_pending_requests_count++;
  }

  float distance = get_chunk_distance(data, camera_pos);
  float chunk_size = get_chunk_world_size(data);

  // Screen-space error: how many pixels does this chunk cover?
  float screen_pixels = get_screen_space_size(chunk_size, distance, camera_fov, screen_height);

  // Subdivide if chunk covers more than threshold pixels on screen OR if below min_depth
  if ((screen_pixels > SUBDIVIDE_PIXEL_THRESHOLD || node->depth < min_depth) && node->depth < max_depth) {

    // Enqueue requests for any children not yet requested
    float u_mid = (data->u_min + data->u_max) * 0.5f;
    float v_mid = (data->v_min + data->v_max) * 0.5f;

    for (int i = 0; i < 4; i++) {
      if (data->pending_child[i] == NULL) {
        // Need to send this request
        float u_min, u_max, v_min, v_max;
        switch (i) {
          case 0: u_min = data->u_min; u_max = u_mid; v_min = data->v_min; v_max = v_mid; break;
          case 1: u_min = u_mid; u_max = data->u_max; v_min = data->v_min; v_max = v_mid; break;
          case 2: u_min = data->u_min; u_max = u_mid; v_min = v_mid; v_max = data->v_max; break;
          case 3: u_min = u_mid; u_max = data->u_max; v_min = v_mid; v_max = data->v_max; break;
          default: continue;
        }

        MeshRequest *req = mesh_pool_enqueue(&g_mesh_pool,
                                             REQUEST_SUBDIVIDE,
                                             data->face,
                                             u_min, u_max, v_min, v_max,
                                             PLANET_RADIUS, NOISE_SCALE,
                                             chunk_resolution, SKIRT_DEPTH,
                                             node->depth + 1,
                                             data,
                                             data->generation,
                                             i);
        if (req != NULL) {
          data->pending_child[i] = req;
          g_requests_sent_this_frame++;
        }
        // If NULL (alloc failed), will retry next frame
      }
    }

    // Check if all 4 children are ready
    bool all_ready = true;
    for (int i = 0; i < 4; i++) {
      MeshRequest *req = data->pending_child[i];
      if (req == NULL) {
        all_ready = false;
        break;
      }
      MeshRequestStatus status = atomic_load(&req->status);
      if (status != REQUEST_READY) {
        all_ready = false;
        break;
      }
    }

    if (all_ready) {
      subdivide_quadtree_node(node, create_child_mesh_data, cleanup_mesh_data);
      // Note: data (parent's user_data) has been freed by cleanup_mesh_data
      return;  // Parent is no longer a leaf, don't touch freed data
    }
  } else {
    // No longer need to subdivide - cancel any pending requests
    if (has_pending_split(data)) {
      g_splits_cancelled_this_frame++;
      for (int i = 0; i < 4; i++) {
        if (data->pending_child[i] != NULL) {
          data->pending_child[i]->node = NULL;  // Null out before cancel to prevent dangling access
          mesh_pool_cancel(data->pending_child[i]);
          // Don't free here - will be freed when drained from completed list
          data->pending_child[i] = NULL;
        }
      }
    }
  }
}

// Merge distant chunks back to parent (called after subdivision pass)
void merge_face_lod(QuadTreeNode *node, Vector3 camera_pos, float camera_fov, int screen_height) {
  if (node == NULL) return;

  // Only process non-leaf nodes
  if (node->children[0] == NULL) return;

  // Recurse into children first (bottom-up merge)
  for (unsigned short i = 0; i < 4; i++) {
    merge_face_lod(node->children[i], camera_pos, camera_fov, screen_height);
  }

  // Check if all children are now leaves (with null safety)
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] == NULL || node->children[i]->children[0] != NULL) {
      return;  // Child missing or has grandchildren, can't merge
    }
  }

  // Check if all children are small enough on screen to merge
  // Use lower threshold than subdivide to provide hysteresis and prevent flicker
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i]->user_data == NULL) continue;

    CubicQuadTreeMeshData *child_data = (CubicQuadTreeMeshData *)node->children[i]->user_data;
    float distance = get_chunk_distance(child_data, camera_pos);
    float chunk_size = get_chunk_world_size(child_data);
    float screen_pixels = get_screen_space_size(chunk_size, distance, camera_fov, screen_height);

    // If any child is still large enough on screen, don't merge
    if (screen_pixels > MERGE_PIXEL_THRESHOLD) {
      return;
    }
  }

  // If node depth is less than min_depth, don't merge
  if (node->depth < min_depth) {
    return;
  }

  // All children are small on screen - merge them back to parent
  // First, get parent's UV bounds from first child
  CubicQuadTreeMeshData *first_child = (CubicQuadTreeMeshData *)node->children[0]->user_data;
  if (first_child == NULL) return;  // Safety check
  CubeFace face = first_child->face;

  // Calculate parent UV bounds (double the child's range)
  float u_size = first_child->u_max - first_child->u_min;
  float v_size = first_child->v_max - first_child->v_min;
  float u_min = first_child->u_min;
  float v_min = first_child->v_min;
  float u_max = u_min + u_size * 2;
  float v_max = v_min + v_size * 2;

  // ASYNC MERGE - use first child's pending_merge to track the request
  if (first_child->pending_merge == NULL) {
    // Request generation of the parent mesh
    MeshRequest *req = mesh_pool_enqueue(&g_mesh_pool,
                                         REQUEST_MERGE,
                                         face,
                                         u_min, u_max, v_min, v_max,
                                         PLANET_RADIUS, NOISE_SCALE,
                                         chunk_resolution, SKIRT_DEPTH,
                                         node->depth,
                                         first_child,
                                         first_child->generation,
                                         -1);  // -1 indicates merge request
    if (req != NULL) {
      first_child->pending_merge = req;
      g_requests_sent_this_frame++;
    }
    // If NULL, will retry next frame
  } else {
    // Check if result is ready
    MeshRequest *req = first_child->pending_merge;
    MeshRequestStatus status = atomic_load(&req->status);

    if (status == REQUEST_READY && req->result.vertices != NULL) {
      // Upload mesh to GPU
      Mesh parent_mesh = upload_mesh_gpu(req->result);

      // Free the request
      mesh_pool_free_request(req);
      first_child->pending_merge = NULL;

      // Clean up children
      for (unsigned short i = 0; i < 4; i++) {
        cleanup_mesh_data(node->children[i]->user_data);
        free(node->children[i]);
        node->children[i] = NULL;
      }

      // Assign parent mesh
      CubicQuadTreeMeshData *parent_data = malloc(sizeof(CubicQuadTreeMeshData));
      if (parent_data) {
        registry_add(parent_data);  // Track as valid
        parent_data->face = face;
        parent_data->u_min = u_min;
        parent_data->u_max = u_max;
        parent_data->v_min = v_min;
        parent_data->v_max = v_max;
        parent_data->mesh = parent_mesh;
        parent_data->center_elevation = get_center_elevation(face, u_min, u_max, v_min, v_max,
                                                              PLANET_RADIUS, NOISE_SCALE);
        parent_data->generation = g_generation_counter++;
        for (int i = 0; i < 4; i++) parent_data->pending_child[i] = NULL;
        parent_data->pending_merge = NULL;

        node->user_data = parent_data;
      } else {
        UnloadMesh(parent_mesh);
      }
    } else if (status == REQUEST_CANCELLED) {
      // Request was cancelled, clear and retry
      mesh_pool_free_request(req);
      first_child->pending_merge = NULL;
    }
    // Otherwise still waiting
  }
}

// Process all faces for LOD (only visible faces)
void process_cubic_quadtree_lod(CubicQuadTree *tree, Vector3 local_camera_pos, float camera_fov, int screen_height, int max_depth) {
  for (int i = 0; i < CUBE_FACE_COUNT; i++) {
    if (tree->faces[i] != NULL && is_face_visible((CubeFace)i, local_camera_pos, tree->radius)) {
      process_face_lod(tree->faces[i], local_camera_pos, camera_fov, screen_height, max_depth);
      merge_face_lod(tree->faces[i], local_camera_pos, camera_fov, screen_height);
    }
  }
}

// ===== Initialization =====

CubicQuadTree *create_cubic_quadtree(float radius, float noise_scale) {
  CubicQuadTree *tree = malloc(sizeof(CubicQuadTree));
  if (tree == NULL) return NULL;
  
  tree->radius = radius;
  tree->noise_scale = noise_scale;
  
  // Initialize all 6 faces
  for (int face = 0; face < CUBE_FACE_COUNT; face++) {
    // Create root quadtree node for this face
    // Using dummy x/y for the 2D quadtree structure - actual positions come from UV
    tree->faces[face] = create_quadtree_node(-1.0f, -1.0f, 2.0f, 2.0f, 0, NULL);
    if (tree->faces[face] == NULL) {
      // Cleanup already created faces
      for (int j = 0; j < face; j++) {
        if (tree->faces[j] != NULL) {
          delete_quadtree_node(tree->faces[j], cleanup_mesh_data);
        }
      }
      free(tree);
      return NULL;
    }
    
    // Create root mesh data for this face
    CubicQuadTreeMeshData *root_data = malloc(sizeof(CubicQuadTreeMeshData));
    if (root_data == NULL) {
      // Cleanup
      for (int j = 0; j <= face; j++) {
        if (tree->faces[j] != NULL) {
          delete_quadtree_node(tree->faces[j], cleanup_mesh_data);
        }
      }
      free(tree);
      return NULL;
    }
    registry_add(root_data);  // Track as valid

    root_data->face = (CubeFace)face;
    root_data->u_min = -1.0f;
    root_data->u_max = 1.0f;
    root_data->v_min = -1.0f;
    root_data->v_max = 1.0f;
    root_data->mesh = create_sphere_patch_mesh((CubeFace)face, -1.0f, 1.0f, -1.0f, 1.0f,
                                                radius, noise_scale, chunk_resolution, SKIRT_DEPTH);
    root_data->center_elevation = get_center_elevation((CubeFace)face, -1.0f, 1.0f, -1.0f, 1.0f,
                                                        radius, noise_scale);
    root_data->generation = g_generation_counter++;
    for (int j = 0; j < 4; j++) root_data->pending_child[j] = NULL;
    root_data->pending_merge = NULL;

    tree->faces[face]->user_data = root_data;
  }
  
  return tree;
}

void destroy_cubic_quadtree(CubicQuadTree *tree) {
  if (tree == NULL) return;
  
  for (int i = 0; i < CUBE_FACE_COUNT; i++) {
    if (tree->faces[i] != NULL) {
      delete_quadtree_node(tree->faces[i], cleanup_mesh_data);
    }
  }
  free(tree);
}

// ===== Main =====

int main(void) {
  const int screenWidth = 1280;
  const int screenHeight = 720;
  InitWindow(screenWidth, screenHeight, "Cubic Quadtree Planet - Shadows");
  SetTargetFPS(60);
  ToggleBorderlessWindowed();
  
  // Initialize mesh thread pool
  if (!mesh_pool_init(&g_mesh_pool)) {
    printf("Failed to initialize mesh thread pool\n");
    CloseWindow();
    return -1;
  }
  
  // Create cubic quadtree (single face for testing)
  CubicQuadTree *planet = create_cubic_quadtree(PLANET_RADIUS, NOISE_SCALE);
  if (planet == NULL) {
    CloseWindow();
    return -1;
  }
  
  // Camera setup - start above the +Y face
  Camera3D camera = {0};
  camera.position = (Vector3){0.0f, PLANET_RADIUS + 50000.0f, 0.0f};  // 50km above surface
  camera.target = (Vector3){0.0f, PLANET_RADIUS, 0.0f};  // Look at surface
  camera.up = (Vector3){0.0f, 0.0f, 1.0f};  // Z is "up" when looking down at +Y face
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;
  
  // State
  int enable_wireframe = false;
  int enable_lod = true;
  int enable_lighting = true;
  
  DisableCursor();
  
  // ===== Shadow Mapping Setup =====
  
  // Light direction (controllable with arrow keys)
  float lightYaw = 120.0f;
  float lightPitch = -25.0f;
  Vector3 lightDir = {cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                      sinf(lightPitch * DEG2RAD),
                      cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
  lightDir = Vector3Normalize(lightDir);
  Color lightColor = WHITE;
  Vector4 lightColorNormalized = ColorNormalize(lightColor);
  
  // Define cascade split distances (1 unit = 1 meter)
  float cascadeSplits[CASCADE_COUNT + 1];
  cascadeSplits[0] = 100.0f;      // Near plane (100m)
  cascadeSplits[1] = 5000.0f;     // 5km - surface detail
  cascadeSplits[2] = 50000.0f;    // 50km - regional terrain
  cascadeSplits[3] = 500000.0f;   // 500km - horizon/mountains
  cascadeSplits[4] = 5000000.0f;  // 5000km - far plane (orbital view)
  
  // Set up light cameras (one per cascade)
  const float lightViewDistance = 128.0f;
  Camera3D lightCameras[CASCADE_COUNT];
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    lightCameras[i].position = Vector3Scale(lightDir, -200.0f);
    lightCameras[i].target = Vector3Zero();
    lightCameras[i].projection = CAMERA_ORTHOGRAPHIC;
    lightCameras[i].up = (Vector3){0.0f, 1.0f, 0.0f};
    lightCameras[i].fovy = lightViewDistance;
  }
  
  // Load shadow shader
  const Shader shadowShader = LoadShader("shaders/shadowmap.vs", "shaders/shadowmap.fs");
  if (shadowShader.id == 0) {
    destroy_cubic_quadtree(planet);
    CloseWindow();
    return -1;
  }
  
  // Set up shader uniform locations
  shadowShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shadowShader, "viewPos");
  int lightDirLoc = GetShaderLocation(shadowShader, "lightDir");
  int lightColLoc = GetShaderLocation(shadowShader, "lightColor");
  int ambientLoc = GetShaderLocation(shadowShader, "ambient");
  
  SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized, SHADER_UNIFORM_VEC4);
  
  float ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // Zero ambient - moon has no atmosphere
  SetShaderValue(shadowShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);
  
  // Set cascade splits individually
  for (int i = 0; i < CASCADE_COUNT + 1; i++) {
    char locName[32];
    sprintf(locName, "cascadeSplits[%d]", i);
    int loc = GetShaderLocation(shadowShader, locName);
    SetShaderValue(shadowShader, loc, &cascadeSplits[i], SHADER_UNIFORM_FLOAT);
  }
  
  int shadowMapResolution = SHADOWMAP_RESOLUTION;
  SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "shadowMapResolution"),
                 &shadowMapResolution, SHADER_UNIFORM_INT);
  
  // Debug cascade colors toggle
  int debugCascadesLoc = GetShaderLocation(shadowShader, "debugCascades");
  int debugCascades = 0;
  SetShaderValue(shadowShader, debugCascadesLoc, &debugCascades, SHADER_UNIFORM_INT);
  
  // Store light matrices
  Matrix lightViews[CASCADE_COUNT] = {0};
  Matrix lightProjs[CASCADE_COUNT] = {0};
  Matrix lightViewProjs[CASCADE_COUNT] = {0};
  
  // Cache shader uniform locations
  int lightVPLocs[CASCADE_COUNT];
  int shadowMapLocs[CASCADE_COUNT];
  for (int i = 0; i < CASCADE_COUNT; i++) {
    char locName[32];
    sprintf(locName, "lightVPs[%d]", i);
    lightVPLocs[i] = GetShaderLocation(shadowShader, locName);
    sprintf(locName, "shadowMaps[%d]", i);
    shadowMapLocs[i] = GetShaderLocation(shadowShader, locName);
  }
  
  // Create shadow material
  Material shadowMaterial = LoadMaterialDefault();
  shadowMaterial.shader = shadowShader;
  
  // Load shadow map render textures
  RenderTexture2D shadowMaps[CASCADE_COUNT];
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    shadowMaps[i] = LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
    if (shadowMaps[i].id == 0) {
      destroy_cubic_quadtree(planet);
      CloseWindow();
      return -1;
    }
  }
  
  // Frustum bounds for cascade fitting
  FrustumBounds cascadeBounds[CASCADE_COUNT];
  float aspectRatio = (float)screenWidth / (float)screenHeight;
  
  // Planet configuration for cascade 3
  const Vector3 planetCenter = {0.0f, 0.0f, 0.0f};
  
  while (!WindowShouldClose()) {
    float deltaTime = GetFrameTime();
    
    // Profiling variables
    double t_lod_start = 0, t_lod_end = 0;
    double t_shadows_start = 0, t_shadows_end = 0;
    double t_render_start = 0, t_render_end = 0;
    
    // Input: Wireframe toggle
    if (IsKeyPressed(KEY_F)) {
      enable_wireframe = !enable_wireframe;
      if (enable_wireframe) rlEnableWireMode();
      else rlDisableWireMode();
    }
    
    // Input: LOD toggle
    if (IsKeyPressed(KEY_L)) {
      enable_lod = !enable_lod;
    }
    
    // Input: Cascade debug toggle
    if (IsKeyPressed(KEY_C)) {
      debugCascades = !debugCascades;
      SetShaderValue(shadowShader, debugCascadesLoc, &debugCascades, SHADER_UNIFORM_INT);
    }
    
    // Input: Lighting toggle
    if (IsKeyPressed(KEY_G)) {
      enable_lighting = !enable_lighting;
    }
    
    // Input: Light direction (arrow keys)
    const float lightRotateSpeed = 60.0f;
    if (IsKeyDown(KEY_LEFT)) lightYaw += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_RIGHT)) lightYaw -= lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_UP)) lightPitch += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_DOWN)) lightPitch -= lightRotateSpeed * deltaTime;
    
    if (lightPitch > 89.0f) lightPitch = 89.0f;
    if (lightPitch < -89.0f) lightPitch = -89.0f;
    
    // Recalculate light direction
    lightDir = (Vector3){cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                         sinf(lightPitch * DEG2RAD),
                         cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
    lightDir = Vector3Normalize(lightDir);
    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    
    // Camera movement
    UpdateCameraMovement(&camera, deltaTime);
    
    // Update camera position in shader
    Vector3 cameraPos = camera.position;
    SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW],
                   &cameraPos, SHADER_UNIFORM_VEC3);

    // Input: Resolution control with brackets [ ]
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) chunk_resolution = (chunk_resolution < 128) ? chunk_resolution + 4 : 128;
    if (IsKeyPressed(KEY_LEFT_BRACKET)) chunk_resolution = (chunk_resolution > 4) ? chunk_resolution - 4 : 4;

    // Input: Min Depth control with - and =
    if (IsKeyPressed(KEY_EQUAL)) min_depth = (min_depth < MAX_DEPTH) ? min_depth + 1 : MAX_DEPTH;
    if (IsKeyPressed(KEY_MINUS)) min_depth = (min_depth > 0) ? min_depth - 1 : 0;

    // Input: Zoom (FOV) control with Z/X
    const float zoomSpeed = 30.0f;
    if (IsKeyDown(KEY_Z)) camera.fovy = fmaxf(10.0f, camera.fovy - zoomSpeed * deltaTime);
    if (IsKeyDown(KEY_X)) camera.fovy = fminf(90.0f, camera.fovy + zoomSpeed * deltaTime);

    // Input: Rotation speed
    if (IsKeyDown(KEY_COMMA)) planet_rotation_speed -= 10.0f * deltaTime;
    if (IsKeyDown(KEY_PERIOD)) planet_rotation_speed += 10.0f * deltaTime;
    
    // Update Rotation
    planet_rotation_angle += planet_rotation_speed * deltaTime;
    Matrix planet_transform = MatrixRotateY(planet_rotation_angle * DEG2RAD);
    Matrix planet_transform_inv = MatrixInvert(planet_transform);
    
    // Calculate local_camera_pos for LOD and Culling
    Vector3 local_camera_pos = Vector3Transform(camera.position, planet_transform_inv);

    t_lod_start = GetTime();

    // --- ASYNC PROCESSING ---
    // Drain completed requests and free orphaned/cancelled ones
    g_results_processed_this_frame = 0;
    MeshRequest *completed = mesh_pool_drain_completed(&g_mesh_pool);
    while (completed != NULL) {
      // Check for use-after-free
      if (completed->magic == MESH_REQUEST_FREED) {
        fprintf(stderr, "ERROR: Accessing freed MeshRequest %p in drain loop\n", (void*)completed);
        break;  // Stop processing to avoid further corruption
      }
      if (completed->magic != MESH_REQUEST_MAGIC) {
        fprintf(stderr, "ERROR: Invalid MeshRequest %p in drain loop (magic=%08X)\n",
                (void*)completed, completed->magic);
        break;
      }

      MeshRequest *next = completed->next;
      MeshRequestStatus status = atomic_load(&completed->status);

      // Handle based on request status
      if (status == REQUEST_CANCELLED) {
        // CANCELLED requests are safe to free - their mesh was already freed by worker
        // (if CAS failed) or was never generated (if cancelled before processing)
        if (completed->result.vertices != NULL) {
          free_cpu_mesh(&completed->result);
        }
        mesh_pool_free_request(completed);
      } else if (status == REQUEST_READY) {
        // READY requests need careful handling - check if still referenced
        CubicQuadTreeMeshData *node_data = (CubicQuadTreeMeshData *)completed->node;
        bool can_free = false;

        // Only check reference if node is valid (in registry)
        if (node_data == NULL) {
          // Node was nulled by cancellation - safe to free
          can_free = true;
        } else if (registry_contains(node_data) &&
                   node_data->generation == completed->node_generation) {
          // Node is valid - check if it still points to this request
          bool still_referenced = false;
          if (completed->child_index >= 0 && completed->child_index < 4) {
            still_referenced = (node_data->pending_child[completed->child_index] == completed);
          } else if (completed->child_index == -1) {
            still_referenced = (node_data->pending_merge == completed);
          }
          // Only free if not referenced (orphaned)
          can_free = !still_referenced;
        }
        // else: node is invalid/freed but not NULL - can't safely determine reference
        //       DON'T free to avoid double-free (safer to leak than crash)

        if (can_free) {
          if (completed->result.vertices != NULL) {
            free_cpu_mesh(&completed->result);
          }
          mesh_pool_free_request(completed);
        }
      }
      // Other statuses (QUEUED, GENERATING) shouldn't appear in completed queue

      g_results_processed_this_frame++;
      completed = next;
    }

    // LOD processing
    g_pending_split_count = 0;
    g_pending_requests_count = 0;
    g_requests_sent_this_frame = 0;
    g_splits_cancelled_this_frame = 0;
    if (enable_lod) {
      process_cubic_quadtree_lod(planet, local_camera_pos, camera.fovy, GetScreenHeight(), MAX_DEPTH);
    }
    t_lod_end = GetTime();
    
    // Compute stable up vector for light cameras
    Vector3 lightUp = compute_stable_up(lightDir);
    Vector3 lightRight = Vector3CrossProduct(lightDir, lightUp);
    
    // Update light cameras for each cascade
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      if (i < 3) {
        // Cascades 0-2: Frustum-fitted for optimal shadow map usage
        float nearDist = cascadeSplits[i];
        float farDist = cascadeSplits[i + 1];
        
        Vector3 corners[8];
        get_frustum_corners(camera, nearDist, farDist, aspectRatio, corners);
        
        FrustumBounds bounds = compute_light_frustum_bounds(corners, lightDir, lightUp);
        
        // Add padding to avoid edge artifacts
        float padX = (bounds.maxX - bounds.minX) * 0.1f;
        float padY = (bounds.maxY - bounds.minY) * 0.1f;
        bounds.minX -= padX;
        bounds.maxX += padX;
        bounds.minY -= padY;
        bounds.maxY += padY;
        
        // Extend back to capture shadow casters
        float depth = bounds.maxZ - bounds.minZ;
        bounds.minZ -= depth * 2.0f;
        
        cascadeBounds[i] = bounds;
        
        // Compute center of bounds in world space
        float centerX = (bounds.minX + bounds.maxX) * 0.5f;
        float centerY = (bounds.minY + bounds.maxY) * 0.5f;
        float centerZ = (bounds.minZ + bounds.maxZ) * 0.5f;
        
        Vector3 boundsCenter = Vector3Add(Vector3Scale(lightRight, centerX),
                                          Vector3Add(Vector3Scale(lightUp, centerY),
                                                     Vector3Scale(lightDir, centerZ)));
        
        lightCameras[i].target = boundsCenter;
        lightCameras[i].position = Vector3Subtract(boundsCenter, Vector3Scale(lightDir, depth * 2.0f));
        lightCameras[i].fovy = bounds.maxY - bounds.minY;
      } else {
        // Cascade 3: Planet-relative (covers entire visible hemisphere)
        lightCameras[i].fovy = PLANET_RADIUS * 2.2f;
        float backDistance = PLANET_RADIUS * 2.5f;
        lightCameras[i].target = planetCenter;
        lightCameras[i].position = Vector3Add(planetCenter, Vector3Scale(lightDir, -backDistance));
        
        cascadeBounds[i] = (FrustumBounds){0};
      }
      
      lightCameras[i].up = lightUp;
    }
    
    // ===== PASS 1: Render shadow maps =====
    t_shadows_start = GetTime();
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      BeginTextureMode(shadowMaps[i]);
      ClearBackground(WHITE);
      
      BeginMode3D(lightCameras[i]);
      lightViews[i] = rlGetMatrixModelview();
      
      if (i < 3) {
        // Custom tight-fit orthographic projection
        FrustumBounds b = cascadeBounds[i];
        float depth = b.maxZ - b.minZ;
        
        float halfWidth = (b.maxX - b.minX) * 0.5f;
        float halfHeight = (b.maxY - b.minY) * 0.5f;
        
        Matrix customProj = MatrixOrtho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                        0.1f, depth * 3.0f);
        rlSetMatrixProjection(customProj);
        lightProjs[i] = customProj;
      } else {
        // Planet-relative cascade
        float nearPlane = PLANET_RADIUS * 1.0f;
        float farPlane = PLANET_RADIUS * 4.0f;
        rlSetClipPlanes(nearPlane, farPlane);
        lightProjs[i] = rlGetMatrixProjection();
      }
      
      draw_cubic_quadtree(planet, local_camera_pos, shadowMaterial, planet_transform);
      
      EndMode3D();
      EndTextureMode();
      
      lightViewProjs[i] = MatrixMultiply(lightViews[i], lightProjs[i]);
    }
    t_shadows_end = GetTime();
    
    // ===== PASS 2: Render main scene with shadows =====
    rlSetClipPlanes(10.0f, 10000000.0f);  // 100m to 10,000km
    
    BeginDrawing();
    ClearBackground(BLACK);
    
    // Set all cascade light view-projection matrices
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      SetShaderValueMatrix(shadowShader, lightVPLocs[i], lightViewProjs[i]);
    }
    
    // Bind all cascade shadow maps to texture slots
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      int textureSlot = 10 + i;
      rlActiveTextureSlot(textureSlot);
      rlEnableTexture(shadowMaps[i].depth.id);
      rlSetUniform(shadowMapLocs[i], &textureSlot, SHADER_UNIFORM_INT, 1);
    }
    
    BeginMode3D(camera);
    
    t_render_start = GetTime();
    int vertexCount = 0;
    if (enable_lighting) {
      vertexCount = draw_cubic_quadtree(planet, local_camera_pos, shadowMaterial, planet_transform);
    } else {
      Material unlitMat = LoadMaterialDefault();
      vertexCount = draw_cubic_quadtree(planet, local_camera_pos, unlitMat, planet_transform);
    }
    
    EndMode3D();
    t_render_end = GetTime();
    
    // UI
    DrawText("Cubic Quadtree Planet - Shadows", 10, 10, 20, WHITE);
    DrawText("WASD: Move | Mouse: Look | Shift: Speed", 10, 40, 20, WHITE);
    DrawText("Arrows: Move Light | F: Wireframe | L: LOD | C: Debug | G: Lighting", 10, 70, 20, WHITE);
    DrawText(TextFormat("F: Wireframe %s | L: LOD %s | C: Debug %s | G: Lighting %s", 
             enable_wireframe ? "ON" : "OFF",
             enable_lod ? "ON" : "OFF",
             debugCascades ? "ON" : "OFF",
             enable_lighting ? "ON" : "OFF"), 10, 100, 20, WHITE);
    
    float altitude = Vector3Length(camera.position) - PLANET_RADIUS;
    DrawText(TextFormat("Altitude: %.1f km", altitude / 1000.0f), 10, 130, 20, YELLOW);
    DrawText(TextFormat("Camera: (%.0f, %.0f, %.0f)", 
             camera.position.x, camera.position.y, camera.position.z), 10, 160, 20, GRAY);

    DrawText(TextFormat("[ / ]: Chunk Res (%d) | - / =: Min Depth (%d)", chunk_resolution, min_depth), 10, 185, 20, YELLOW);
    DrawText(TextFormat("< / >: Rotation Speed (%.1f deg/s)", planet_rotation_speed), 10, 210, 20, YELLOW);
    DrawText(TextFormat("Z / X: Zoom (FOV %.1f)", camera.fovy), 10, 235, 20, YELLOW);
    
    // Draw profiling info
    double lod_ms = (t_lod_end - t_lod_start) * 1000.0;
    double shadow_ms = (t_shadows_end - t_shadows_start) * 1000.0;
    double render_ms = (t_render_end - t_render_start) * 1000.0;
    
    DrawText(TextFormat("LOD Update: %.2f ms", lod_ms), 10, 255, 20, GREEN);
    DrawText(TextFormat("Shadow Maps: %.2f ms", shadow_ms), 10, 285, 20, GREEN);
    DrawText(TextFormat("Main Render: %.2f ms", render_ms), 10, 315, 20, GREEN);
    DrawText(TextFormat("Total CPU Work: %.2f ms", lod_ms + shadow_ms + render_ms), 10, 345, 20, GREEN);
    DrawText(TextFormat("Active Vertices: %d", vertexCount), 10, 375, 20, GREEN);

    // Mesh thread pool debug stats
    MeshPoolStats pool_stats = mesh_pool_get_stats(&g_mesh_pool);
    DrawText(TextFormat("Pool: Queued: %d | Completed: %d | Threads: %d",
             pool_stats.queued_count, pool_stats.completed_count, pool_stats.thread_count),
             10, 405, 20, ORANGE);
    DrawText(TextFormat("Frame: Sent: %d | Processed: %d | Cancelled: %d",
             g_requests_sent_this_frame, g_results_processed_this_frame, g_splits_cancelled_this_frame),
             10, 435, 20, ORANGE);
    DrawText(TextFormat("Pending: splits=%d requests=%d",
             g_pending_split_count, g_pending_requests_count),
             10, 465, 20, SKYBLUE);

    DrawFPS(screenWidth - 100, 10);
    
    EndDrawing();
  }
  
  // Cleanup
  mesh_pool_shutdown(&g_mesh_pool);
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    UnloadRenderTexture(shadowMaps[i]);
  }
  UnloadShader(shadowShader);
  UnloadMaterial(shadowMaterial);
  destroy_cubic_quadtree(planet);
  CloseWindow();
  
  return 0;
}

