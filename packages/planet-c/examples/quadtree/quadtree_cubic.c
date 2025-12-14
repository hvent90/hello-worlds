#include <float.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// clang-format off
// clang-format off
#include "shadow.h"
#include "camera.h"
#include "quadtree.h"
#include "mesh.h"
#include "cube_face.h"
#include "async_loader.h"
// clang-format on

// Planet configuration
const float PLANET_RADIUS = 1700000.0f;  // 1000 km
const float NOISE_SCALE = 3000.0f;       // Height variation in meters
const float SKIRT_DEPTH = 4000.0f;        // Skirt depth to hide seams (must be > NOISE_SCALE)
#define MAX_DEPTH 8

// Tweakable parameters
int chunk_resolution = 32;
int min_depth = 0;

// Planet rotation state
float planet_rotation_speed = 5.0f; // Degrees per second
float planet_rotation_angle = 0.0f;

// Mesh data attached to each quadtree node
typedef struct {
  Mesh mesh;
  CubeFace face;
  float u_min, u_max;
  float v_min, v_max;
  float center_elevation;  // Height at center for LOD distance calc
  
  // Async state
  unsigned int pending_child_ids[4];
  unsigned int pending_merge_id;
  bool has_pending_split;
} CubicQuadTreeMeshData;

// Temporary storage for meshes ready to be attached
#define MAX_STAGED_MESHES 64
typedef struct {
    unsigned int id;
    Mesh mesh;
    bool active;
} StagedMesh;

static StagedMesh staged_meshes[MAX_STAGED_MESHES] = {0};

// Track cancelled request IDs so we can discard their results when they arrive
#define MAX_CANCELLED_IDS 256
static unsigned int cancelled_ids[MAX_CANCELLED_IDS] = {0};
static int cancelled_ids_count = 0;

void add_cancelled_id(unsigned int id) {
    if (id == 0) return;
    if (cancelled_ids_count < MAX_CANCELLED_IDS) {
        cancelled_ids[cancelled_ids_count++] = id;
    }
}

bool is_cancelled_id(unsigned int id) {
    for (int i = 0; i < cancelled_ids_count; i++) {
        if (cancelled_ids[i] == id) return true;
    }
    return false;
}

void remove_cancelled_id(unsigned int id) {
    for (int i = 0; i < cancelled_ids_count; i++) {
        if (cancelled_ids[i] == id) {
            cancelled_ids[i] = cancelled_ids[--cancelled_ids_count];
            return;
        }
    }
}

int get_staged_mesh_count(void) {
    int count = 0;
    for(int i=0; i<MAX_STAGED_MESHES; i++) {
        if (staged_meshes[i].active) count++;
    }
    return count;
}

// Debug: count nodes with pending splits
static int g_pending_split_count = 0;
static int g_pending_ids_count = 0;  // Total non-zero pending_child_ids across all nodes
static int g_requests_sent_this_frame = 0;
static int g_splits_cancelled_this_frame = 0;

void add_staged_mesh(unsigned int id, Mesh mesh) {
    for(int i=0; i<MAX_STAGED_MESHES; i++) {
        if (!staged_meshes[i].active) {
            staged_meshes[i].id = id;
            staged_meshes[i].mesh = mesh;
            staged_meshes[i].active = true;
            printf("STAGED id=%u verts=%d slot=%d\n", id, mesh.vertexCount, i);
            return;
        }
    }
    printf("Staged mesh buffer full! Leaking mesh (Unloading).\n");
    UnloadMesh(mesh);
}

Mesh get_and_remove_staged_mesh(unsigned int id) {
    Mesh m = {0};
    for(int i=0; i<MAX_STAGED_MESHES; i++) {
        if (staged_meshes[i].active && staged_meshes[i].id == id) {
            m = staged_meshes[i].mesh;
            staged_meshes[i].active = false;
            printf("CONSUMED id=%u verts=%d slot=%d\n", id, m.vertexCount, i);
            return m;
        }
    }
    printf("NOT FOUND id=%u\n", id);
    return m;
}

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
  
  // RETRIEVE ASYNC MESH
  unsigned int id = parent_data->pending_child_ids[index];
  Mesh mesh = get_and_remove_staged_mesh(id);
  
  if (mesh.vertexCount == 0) {
      // Synchronous fallback (e.g. initial load or error)
      mesh = create_sphere_patch_mesh(parent_data->face, u_min, u_max, v_min, v_max,
                                      PLANET_RADIUS, NOISE_SCALE,
                                      chunk_resolution, SKIRT_DEPTH);
  }
  
  CubicQuadTreeMeshData *child_data = malloc(sizeof(CubicQuadTreeMeshData));
  if (child_data == NULL) {
    UnloadMesh(mesh);
    return NULL;
  }
  
  child_data->mesh = mesh;
  child_data->face = parent_data->face;
  child_data->u_min = u_min;
  child_data->u_max = u_max;
  child_data->v_min = v_min;
  child_data->v_max = v_max;
  child_data->center_elevation = get_center_elevation(parent_data->face, u_min, u_max, 
                                                       v_min, v_max, PLANET_RADIUS, NOISE_SCALE);
  // Init async state
  child_data->has_pending_split = false;
  for(int i=0; i<4; i++) child_data->pending_child_ids[i] = 0;
  
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

// Check distance from camera to chunk center (on sphere surface)
float get_chunk_distance(CubicQuadTreeMeshData *data, Vector3 camera_pos) {
  float u_center = (data->u_min + data->u_max) * 0.5f;
  float v_center = (data->v_min + data->v_max) * 0.5f;
  
  // Use PLANET_RADIUS directly - center_elevation includes noise which makes it unreliable
  Vector3 chunk_center = cube_face_to_sphere_point(data->face, u_center, v_center, PLANET_RADIUS);
  
  return Vector3Distance(camera_pos, chunk_center);
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

static unsigned int global_req_id = 1;

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
  if (data->has_pending_split) g_pending_split_count++;
  for (int i = 0; i < 4; i++) {
      if (data->pending_child_ids[i] != 0) g_pending_ids_count++;
  }

  float distance = get_chunk_distance(data, camera_pos);
  float chunk_size = get_chunk_world_size(data);
  
  // Screen-space error: how many pixels does this chunk cover?
  float screen_pixels = get_screen_space_size(chunk_size, distance, camera_fov, screen_height);
  
  // Subdivide if chunk covers more than threshold pixels on screen OR if below min_depth
  if ((screen_pixels > SUBDIVIDE_PIXEL_THRESHOLD || node->depth < min_depth) && node->depth < max_depth) {
      
      // If not splitting yet, start the process
      if (!data->has_pending_split) {
          data->has_pending_split = true;
          // Clean ensure IDs are 0
          for(int i=0; i<4; i++) data->pending_child_ids[i] = 0;
      }
      
      // Ensure all 4 requests are sent
      float u_mid = (data->u_min + data->u_max) * 0.5f;
      float v_mid = (data->v_min + data->v_max) * 0.5f;
      bool all_sent = true;
      
      for(int i=0; i<4; i++) {
          if (data->pending_child_ids[i] == 0) {
              // Need to send this request
              ChunkGenerationParams params = {
                  .face = data->face,
                  .radius = PLANET_RADIUS,
                  .noise_scale = NOISE_SCALE,
                  .resolution = chunk_resolution,
                  .skirt_depth = SKIRT_DEPTH,
                  .depth = node->depth + 1,
                  .id = global_req_id  // Don't increment yet
              };
              
              switch(i) {
                  case 0: params.u_min = data->u_min; params.u_max = u_mid; params.v_min = data->v_min; params.v_max = v_mid; break;
                  case 1: params.u_min = u_mid; params.u_max = data->u_max; params.v_min = data->v_min; params.v_max = v_mid; break;
                  case 2: params.u_min = data->u_min; params.u_max = u_mid; params.v_min = v_mid; params.v_max = data->v_max; break;
                  case 3: params.u_min = u_mid; params.u_max = data->u_max; params.v_min = v_mid; params.v_max = data->v_max; break;
              }
               Vector3 center = cube_face_to_sphere_point(params.face, 
                            (params.u_min + params.u_max)*0.5f,
                            (params.v_min + params.v_max)*0.5f, PLANET_RADIUS);
               params.center_pos = center;
               
              if (async_loader_request(params)) {
                  data->pending_child_ids[i] = params.id;
                  global_req_id++;  // Only increment after successful enqueue
                  g_requests_sent_this_frame++;
              } else {
                  // Queue full. Keep ID as 0 to retry next frame.
                  all_sent = false;
              }
          }
      }
      
      if (!all_sent) {
          return; // Wait for next frame to try sending remaining requests
      }
      
      // Check if all 4 children are ready in the staging area
      bool all_ready = true;
      for(int i=0; i<4; i++) {
          bool found = false;
          unsigned int needed_id = data->pending_child_ids[i];
          
          if (needed_id == 0) { 
              // Should not happen if all_sent is true, but safety check
              all_ready = false; 
              break; 
          }
          
          // Check staged meshes
          for(int j=0; j<MAX_STAGED_MESHES; j++) {
              if (staged_meshes[j].active && staged_meshes[j].id == needed_id) {
                  found = true;
                  
                  // Check if it was a failed/skipped result
                  if (staged_meshes[j].mesh.vertexCount == 0) {
                      // Abort split.
                       data->has_pending_split = false;
                       get_and_remove_staged_mesh(needed_id);
                       // Optimization: We could consume others if we found them, 
                       // but looping again is safer to find all pending.
                       all_ready = false;
                       break;
                  }
                  break;
              }
          }
          
          if (!found) {
               // Still waiting.
               all_ready = false;
               break;
          }
      }
      
      if (!all_ready && !data->has_pending_split) {
           // We aborted inside the loop due to failure. Clean up others.
           for(int i=0; i<4; i++) {
               if (data->pending_child_ids[i] != 0) {
                   get_and_remove_staged_mesh(data->pending_child_ids[i]); // Clean staging
                   data->pending_child_ids[i] = 0;
               }
           }
           return;
      }
      
      if (all_ready) {
          subdivide_quadtree_node(node, create_child_mesh_data, cleanup_mesh_data);
          // Note: data (parent's user_data) has been freed by cleanup_mesh_data
          // No need to clear state - the parent is no longer a leaf
          return;  // Parent is no longer a leaf, don't touch freed data
      }
  } else {
      // No longer need to subdivide - cancel any pending split
      if (data->has_pending_split) {
          g_splits_cancelled_this_frame++;
          for (int i = 0; i < 4; i++) {
              if (data->pending_child_ids[i] != 0) {
                  // Try to remove from staged, but also mark as cancelled
                  // in case it hasn't arrived yet
                  get_and_remove_staged_mesh(data->pending_child_ids[i]);
                  add_cancelled_id(data->pending_child_ids[i]);
                  data->pending_child_ids[i] = 0;
              }
          }
          data->has_pending_split = false;
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

  // If node depth is less than min_depth, we cannot merge (we need to be at least min_depth deep)
  // Wait... merge happens at parent node. If parent node depth < min_depth, that means parent IS min_depth - 1. 
  // We want to KEEP children if parent_depth < min_depth.
  // Actually, node->depth is the depth of the PARENT node.
  // If node->depth < min_depth, we should NOT have children at all... wait.
  // If min_depth is 2. Root is depth 0. We want depth 0 to split to 1, 1 to split to 2. 
  // We want to merge depth 2 nodes back to depth 1 ONLY if depth 1 >= min_depth.
  // So if min_depth is 2, we can merge depth 3 to depth 2. But we CANNOT merge depth 2 to depth 1.
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
  
  // Find the actual bounds from all children (assuming standard quadrant layout)
  // Or just reconstruct from known child 0 (it is always u_min, v_min relative to parent?)
  // Children layout:
  // 0: u_min, v_min
  // 1: u_mid, v_min
  // 2: u_min, v_mid
  // 3: u_mid, v_mid
  // So child 0's min is parent's min.
  // We can verify this but for this specific tree structure it's consistent.
  
  float u_max = u_min + u_size * 2;
  float v_max = v_min + v_size * 2;

  // ASYNC MERGE
  // We attach the pending request ID to the first child for tracking
  if (first_child->pending_merge_id == 0) {
      // Request generation of the parent mesh (lower resolution relative to world size, but same grid res)
      ChunkGenerationParams params = {
          .face = face,
          .radius = PLANET_RADIUS,
          .noise_scale = NOISE_SCALE,
          .resolution = chunk_resolution,
          .skirt_depth = SKIRT_DEPTH,
          .u_min = u_min, .u_max = u_max,
          .v_min = v_min, .v_max = v_max,
          .depth = node->depth, // Parent depth
          .center_pos = cube_face_to_sphere_point(face,
                                (u_min + u_max)*0.5f,
                                (v_min + v_max)*0.5f, PLANET_RADIUS),
          .id = global_req_id  // Don't increment yet
      };

      if (async_loader_request(params)) {
          first_child->pending_merge_id = params.id;
          global_req_id++;  // Only increment after successful enqueue
      }
      // If queue full, pending_merge_id stays 0, will retry next frame
  } else {
      // Check if result is ready
      Mesh parent_mesh = {0};
      bool found = false;
      unsigned int needed_id = first_child->pending_merge_id;
      
      // Check staged meshes
      for(int j=0; j<MAX_STAGED_MESHES; j++) {
          if (staged_meshes[j].active && staged_meshes[j].id == needed_id) {
              if (staged_meshes[j].mesh.vertexCount > 0) {
                  parent_mesh = staged_meshes[j].mesh;
                  found = true;
              } else {
                  // Failed generation (invalid). Reset.
                  first_child->pending_merge_id = 0;
                  get_and_remove_staged_mesh(needed_id);
                  return; // Try again next frame
              }
              // Mark as consumed (will be removed below)
              staged_meshes[j].active = false; 
              break;
          }
      }
      
       if (!found) {
           // If not pending/staged, it was dropped/lost. Reset.
           if (!async_loader_is_pending(needed_id)) {
               first_child->pending_merge_id = 0; 
           }
           return; // Still waiting
       }
      
      // Merge is ready!
      // Clean up children
      for (unsigned short i = 0; i < 4; i++) {
        cleanup_mesh_data(node->children[i]->user_data);
        free(node->children[i]);
        node->children[i] = NULL;
      }
      
      // Assign parent mesh
      CubicQuadTreeMeshData *parent_data = malloc(sizeof(CubicQuadTreeMeshData));
      if (parent_data) {
        parent_data->face = face;
        parent_data->u_min = u_min;
        parent_data->u_max = u_max;
        parent_data->v_min = v_min;
        parent_data->v_max = v_max;
        parent_data->mesh = parent_mesh;
        parent_data->center_elevation = get_center_elevation(face, u_min, u_max, v_min, v_max,
                                                              PLANET_RADIUS, NOISE_SCALE);
        parent_data->has_pending_split = false;
        for(int i=0; i<4; i++) parent_data->pending_child_ids[i] = 0;
        parent_data->pending_merge_id = 0; // Clear
        
        node->user_data = parent_data;
      } else {
           UnloadMesh(parent_mesh); // Safety
      }
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
    
    root_data->face = (CubeFace)face;
    root_data->u_min = -1.0f;
    root_data->u_max = 1.0f;
    root_data->v_min = -1.0f;
    root_data->v_max = 1.0f;
    root_data->mesh = create_sphere_patch_mesh((CubeFace)face, -1.0f, 1.0f, -1.0f, 1.0f,
                                                radius, noise_scale, chunk_resolution, SKIRT_DEPTH);
    root_data->center_elevation = get_center_elevation((CubeFace)face, -1.0f, 1.0f, -1.0f, 1.0f,
                                                        radius, noise_scale);
    
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
  
  // Initialize Async Loader
  async_loader_init();
  
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
    async_loader_update_state(local_camera_pos, SUBDIVIDE_PIXEL_THRESHOLD, screenHeight, camera.fovy);

    // Poll results
    ChunkGenerationResult res;
    while(async_loader_get_result(&res)) {
        // Check if this result was cancelled
        if (is_cancelled_id(res.id)) {
            remove_cancelled_id(res.id);
            if (res.valid) {
                free_cpu_mesh(&res.mesh);  // Discard the CPU mesh data
            }
            printf("DISCARDED cancelled id=%u\n", res.id);
            continue;
        }

        if (res.valid) {
            Mesh m = upload_mesh_gpu(res.mesh); // Upload to VRAM
            // Note: upload_mesh_gpu transfers ownership of pointers to m.vertices etc.
            // DO NOT call free_cpu_mesh(&res.mesh) here!
            add_staged_mesh(res.id, m);
        } else {
             // Invalid/skipped.
             // Stage an empty mesh so we know the request 'completed' (but failed/skipped)
             Mesh empty = {0};
             add_staged_mesh(res.id, empty);
        }
    }

    // LOD processing
    t_lod_start = GetTime();
    g_pending_split_count = 0;
    g_pending_ids_count = 0;
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
    rlSetClipPlanes(100.0f, 10000000.0f);  // 100m to 10,000km
    
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
    
    // Draw profiling info
    double lod_ms = (t_lod_end - t_lod_start) * 1000.0;
    double shadow_ms = (t_shadows_end - t_shadows_start) * 1000.0;
    double render_ms = (t_render_end - t_render_start) * 1000.0;
    
    DrawText(TextFormat("LOD Update: %.2f ms", lod_ms), 10, 230, 20, GREEN);
    DrawText(TextFormat("Shadow Maps: %.2f ms", shadow_ms), 10, 260, 20, GREEN);
    DrawText(TextFormat("Main Render: %.2f ms", render_ms), 10, 290, 20, GREEN);
    DrawText(TextFormat("Total CPU Work: %.2f ms", lod_ms + shadow_ms + render_ms), 10, 320, 20, GREEN);
    DrawText(TextFormat("Active Vertices: %d", vertexCount), 10, 350, 20, GREEN);

    // Async loader debug stats
    AsyncLoaderStats async_stats;
    async_loader_get_stats(&async_stats);
    DrawText(TextFormat("Async: Req Q: %d | Result Q: %d | Staged: %d",
             async_stats.request_queue_count, async_stats.result_queue_count, get_staged_mesh_count()),
             10, 380, 20, ORANGE);
    DrawText(TextFormat("Async Total: Processed: %d | Skipped: %d | Next ID: %u",
             async_stats.total_processed, async_stats.total_skipped, global_req_id),
             10, 410, 20, ORANGE);
    DrawText(TextFormat("Worker: loops=%d waiting=%d",
             async_stats.worker_loop_count, async_stats.worker_waiting),
             10, 440, 20, async_stats.worker_waiting ? RED : LIME);
    DrawText(TextFormat("Pending: splits=%d child_ids=%d | Sent: %d | Cancelled: %d",
             g_pending_split_count, g_pending_ids_count, g_requests_sent_this_frame, g_splits_cancelled_this_frame),
             10, 470, 20, SKYBLUE);

    DrawFPS(screenWidth - 100, 10);
    
    EndDrawing();
  }
  
  // Cleanup
  async_loader_shutdown();
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    UnloadRenderTexture(shadowMaps[i]);
  }
  UnloadShader(shadowShader);
  UnloadMaterial(shadowMaterial);
  destroy_cubic_quadtree(planet);
  CloseWindow();
  
  return 0;
}

