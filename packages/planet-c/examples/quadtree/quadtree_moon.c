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
#include "moon_mesh.h"
#include "cube_face.h"
// clang-format on

// ============================================================================
// Moon Configuration
// ============================================================================

// Real Moon radius: 1,737.4 km - we use meters as units
const float MOON_RADIUS = 1737400.0f;

// Height range for displacement map (meters)
// The Moon's elevation range is approximately -9 km to +10 km
const float HEIGHT_MIN = -9000.0f;
const float HEIGHT_MAX = 10000.0f;

// Skirt depth to hide seams between LOD levels (must be > max height variation)
const float SKIRT_DEPTH = 15000.0f;

#define MAX_DEPTH 14

// Tweakable parameters
int chunk_resolution = 32;
int min_depth = 0;

// Moon rotation state
float moon_rotation_speed = 0.0f;
float moon_rotation_angle = 0.0f;

// ============================================================================
// Global State
// ============================================================================

// Heightmap data for mesh generation
static HeightmapData g_heightmap = {0};

// Albedo texture for rendering
static Texture2D g_albedo_texture = {0};

// Forward declare MeshRequest for the struct
struct MeshRequest;

// Mesh data attached to each quadtree node
typedef struct {
  Mesh mesh;
  CubeFace face;
  float u_min, u_max;
  float v_min, v_max;
  float center_elevation;  // Height at center for LOD distance calc
} MoonQuadTreeMeshData;

// Root structure holding all 6 face quadtrees
typedef struct {
  QuadTreeNode *faces[CUBE_FACE_COUNT];
  float radius;
} MoonQuadTree;

// ============================================================================
// Texture Loading Helpers
// ============================================================================

bool load_moon_textures(const char *heightmap_path, const char *albedo_path) {
  // Load heightmap for CPU-side mesh generation
  if (!heightmap_load(&g_heightmap, heightmap_path, HEIGHT_MIN, HEIGHT_MAX)) {
    fprintf(stderr, "Failed to load heightmap: %s\n", heightmap_path);
    fprintf(stderr, "Please download Moon textures - see textures/README.md\n");
    return false;
  }

  // Load albedo texture for GPU rendering
  g_albedo_texture = LoadTexture(albedo_path);
  if (g_albedo_texture.id == 0) {
    fprintf(stderr, "Failed to load albedo texture: %s\n", albedo_path);
    fprintf(stderr, "Please download Moon textures - see textures/README.md\n");
    heightmap_unload(&g_heightmap);
    return false;
  }

  // Set texture filtering for quality
  SetTextureFilter(g_albedo_texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(g_albedo_texture, TEXTURE_WRAP_REPEAT);

  printf("Loaded Moon textures:\n");
  printf("  Heightmap: %dx%d\n", g_heightmap.heightmap.width, g_heightmap.heightmap.height);
  printf("  Albedo: %dx%d\n", g_albedo_texture.width, g_albedo_texture.height);

  return true;
}

void unload_moon_textures(void) {
  heightmap_unload(&g_heightmap);
  if (g_albedo_texture.id != 0) {
    UnloadTexture(g_albedo_texture);
    g_albedo_texture.id = 0;
  }
}

// ============================================================================
// Shadow Map Helper Structures
// ============================================================================

typedef struct {
  float minX, maxX;
  float minY, maxY;
  float minZ, maxZ;
} FrustumBounds;

Vector3 compute_stable_up(Vector3 direction) {
  const float threshold = 0.99f;
  Vector3 world_up = {0.0f, 1.0f, 0.0f};
  Vector3 world_forward = {0.0f, 0.0f, 1.0f};

  float dot = fabsf(Vector3DotProduct(direction, world_up));
  Vector3 reference = (dot > threshold) ? world_forward : world_up;

  Vector3 right = Vector3Normalize(Vector3CrossProduct(direction, reference));
  return Vector3CrossProduct(right, direction);
}

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

  corners[0] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, nearHeight * 0.5f),
                                                  Vector3Scale(right, -nearWidth * 0.5f)));
  corners[1] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, nearHeight * 0.5f),
                                                  Vector3Scale(right, nearWidth * 0.5f)));
  corners[2] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, -nearHeight * 0.5f),
                                                  Vector3Scale(right, -nearWidth * 0.5f)));
  corners[3] = Vector3Add(nearCenter, Vector3Add(Vector3Scale(up, -nearHeight * 0.5f),
                                                  Vector3Scale(right, nearWidth * 0.5f)));

  corners[4] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, farHeight * 0.5f),
                                                 Vector3Scale(right, -farWidth * 0.5f)));
  corners[5] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, farHeight * 0.5f),
                                                 Vector3Scale(right, farWidth * 0.5f)));
  corners[6] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, -farHeight * 0.5f),
                                                 Vector3Scale(right, -farWidth * 0.5f)));
  corners[7] = Vector3Add(farCenter, Vector3Add(Vector3Scale(up, -farHeight * 0.5f),
                                                 Vector3Scale(right, farWidth * 0.5f)));
}

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

// ============================================================================
// Mesh Data Lifecycle
// ============================================================================

void cleanup_mesh_data(void *user_data);
void *create_child_mesh_data(QuadTreeNode *parent, QuadTreeNode *child, void *parent_user_data);

void cleanup_mesh_data(void *user_data) {
  if (user_data == NULL) return;
  MoonQuadTreeMeshData *mesh_data = (MoonQuadTreeMeshData *)user_data;
  UnloadMesh(mesh_data->mesh);
  free(mesh_data);
}

float get_center_elevation(CubeFace face, float u_min, float u_max, float v_min, float v_max,
                           float radius) {
  float u_center = (u_min + u_max) * 0.5f;
  float v_center = (v_min + v_max) * 0.5f;

  Vector3 cube_point = cube_face_to_cube_point(face, u_center, v_center);
  Vector3 sphere_normal = Vector3Normalize(cube_point);

  float height = heightmap_sample(&g_heightmap, sphere_normal);
  return radius + height;
}

void *create_child_mesh_data(QuadTreeNode *parent, QuadTreeNode *child, void *parent_user_data) {
  if (parent_user_data == NULL) return NULL;

  MoonQuadTreeMeshData *parent_data = (MoonQuadTreeMeshData *)parent_user_data;

  // Determine child index (0-3)
  short index = -1;
  for (unsigned short i = 0; i < 4; i++) {
    if (parent->children[i] == child) {
      index = i;
      break;
    }
  }
  if (index == -1) return NULL;

  // Calculate child UV bounds
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

  // Generate mesh using texture-based heightmap
  Mesh mesh = create_moon_patch_mesh(parent_data->face, u_min, u_max, v_min, v_max,
                                     MOON_RADIUS, &g_heightmap,
                                     chunk_resolution, SKIRT_DEPTH);

  if (mesh.vertexCount == 0) {
    return NULL;
  }

  MoonQuadTreeMeshData *child_data = malloc(sizeof(MoonQuadTreeMeshData));
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
                                                       v_min, v_max, MOON_RADIUS);

  return child_data;
}

// ============================================================================
// Drawing
// ============================================================================

int draw_quadtree_meshes_recursive(QuadTreeNode *node, Material material, Matrix transform) {
  if (node == NULL) return 0;

  bool is_leaf = true;
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] != NULL) {
      is_leaf = false;
      break;
    }
  }

  int count = 0;
  if (is_leaf && node->user_data != NULL) {
    MoonQuadTreeMeshData *mesh_data = (MoonQuadTreeMeshData *)node->user_data;
    DrawMesh(mesh_data->mesh, material, transform);
    count = mesh_data->mesh.vertexCount;
  } else {
    for (unsigned short i = 0; i < 4; i++) {
      count += draw_quadtree_meshes_recursive(node->children[i], material, transform);
    }
  }
  return count;
}

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

bool is_face_visible(CubeFace face, Vector3 camera_pos, float planet_radius) {
  Vector3 face_normal = get_face_normal(face);

  float distance = Vector3Length(camera_pos);
  if (distance < 1.0f) distance = 1.0f;

  Vector3 cam_dir = Vector3Scale(camera_pos, 1.0f / distance);

  float sin_horizon = (distance > planet_radius)
                      ? (planet_radius / distance)
                      : 1.0f;

  float dot = Vector3DotProduct(cam_dir, face_normal);

  return dot > -(sin_horizon + 0.71f);
}

int draw_moon_quadtree(MoonQuadTree *tree, Vector3 local_camera_pos, Material material, Matrix transform) {
  int total_vertices = 0;
  for (int i = 0; i < CUBE_FACE_COUNT; i++) {
    if (tree->faces[i] != NULL && is_face_visible((CubeFace)i, local_camera_pos, tree->radius)) {
      total_vertices += draw_quadtree_meshes_recursive(tree->faces[i], material, transform);
    }
  }
  return total_vertices;
}

// ============================================================================
// LOD System
// ============================================================================

const float SUBDIVIDE_PIXEL_THRESHOLD = 4000.0f;
const float MERGE_PIXEL_THRESHOLD = 1900.0f;

float get_chunk_distance(MoonQuadTreeMeshData *data, Vector3 camera_pos) {
  float u_vals[3] = { data->u_min, (data->u_min + data->u_max) * 0.5f, data->u_max };
  float v_vals[3] = { data->v_min, (data->v_min + data->v_max) * 0.5f, data->v_max };

  float min_dist = FLT_MAX;
  int samples[5][2] = { {0,0}, {0,2}, {2,0}, {2,2}, {1,1} };

  for (int i = 0; i < 5; i++) {
    float u = u_vals[samples[i][0]];
    float v = v_vals[samples[i][1]];
    Vector3 point = cube_face_to_sphere_point(data->face, u, v, MOON_RADIUS);
    float dist = Vector3Distance(camera_pos, point);
    if (dist < min_dist) min_dist = dist;
  }

  return min_dist;
}

float get_chunk_world_size(MoonQuadTreeMeshData *data) {
  float uv_size = (data->u_max - data->u_min);
  return uv_size * MOON_RADIUS * 1.57f;
}

float get_screen_space_size(float world_size, float distance, float fov_degrees, int screen_height) {
  if (distance < 1.0f) distance = 1.0f;

  float fov_radians = fov_degrees * DEG2RAD;
  float projection_factor = (float)screen_height / (2.0f * tanf(fov_radians * 0.5f));

  return (world_size / distance) * projection_factor;
}

void process_face_lod(QuadTreeNode *node, Vector3 camera_pos, float camera_fov, int screen_height, int max_depth) {
  if (node == NULL) return;

  bool is_leaf = true;
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] != NULL) {
      is_leaf = false;
      break;
    }
  }

  if (!is_leaf) {
    for (unsigned short i = 0; i < 4; i++) {
      process_face_lod(node->children[i], camera_pos, camera_fov, screen_height, max_depth);
    }
    return;
  }

  if (node->user_data == NULL) {
    return;
  }

  MoonQuadTreeMeshData *data = (MoonQuadTreeMeshData *)node->user_data;

  float distance = get_chunk_distance(data, camera_pos);
  float chunk_size = get_chunk_world_size(data);
  float screen_pixels = get_screen_space_size(chunk_size, distance, camera_fov, screen_height);

  if ((screen_pixels > SUBDIVIDE_PIXEL_THRESHOLD || node->depth < min_depth) && node->depth < max_depth) {
    subdivide_quadtree_node(node, create_child_mesh_data, cleanup_mesh_data);
  }
}

void merge_face_lod(QuadTreeNode *node, Vector3 camera_pos, float camera_fov, int screen_height) {
  if (node == NULL) return;

  if (node->children[0] == NULL) return;

  for (unsigned short i = 0; i < 4; i++) {
    merge_face_lod(node->children[i], camera_pos, camera_fov, screen_height);
  }

  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] == NULL || node->children[i]->children[0] != NULL) {
      return;
    }
  }

  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i]->user_data == NULL) continue;

    MoonQuadTreeMeshData *child_data = (MoonQuadTreeMeshData *)node->children[i]->user_data;
    float distance = get_chunk_distance(child_data, camera_pos);
    float chunk_size = get_chunk_world_size(child_data);
    float screen_pixels = get_screen_space_size(chunk_size, distance, camera_fov, screen_height);

    if (screen_pixels > MERGE_PIXEL_THRESHOLD) {
      return;
    }
  }

  if (node->depth < min_depth) {
    return;
  }

  // Merge: recreate parent mesh
  MoonQuadTreeMeshData *first_child = (MoonQuadTreeMeshData *)node->children[0]->user_data;
  if (first_child == NULL) return;

  CubeFace face = first_child->face;
  float u_size = first_child->u_max - first_child->u_min;
  float v_size = first_child->v_max - first_child->v_min;
  float u_min = first_child->u_min;
  float v_min = first_child->v_min;
  float u_max = u_min + u_size * 2;
  float v_max = v_min + v_size * 2;

  Mesh parent_mesh = create_moon_patch_mesh(face, u_min, u_max, v_min, v_max,
                                            MOON_RADIUS, &g_heightmap,
                                            chunk_resolution, SKIRT_DEPTH);

  for (unsigned short i = 0; i < 4; i++) {
    cleanup_mesh_data(node->children[i]->user_data);
    free(node->children[i]);
    node->children[i] = NULL;
  }

  MoonQuadTreeMeshData *parent_data = malloc(sizeof(MoonQuadTreeMeshData));
  if (parent_data) {
    parent_data->face = face;
    parent_data->u_min = u_min;
    parent_data->u_max = u_max;
    parent_data->v_min = v_min;
    parent_data->v_max = v_max;
    parent_data->mesh = parent_mesh;
    parent_data->center_elevation = get_center_elevation(face, u_min, u_max, v_min, v_max, MOON_RADIUS);
    node->user_data = parent_data;
  } else {
    UnloadMesh(parent_mesh);
  }
}

void process_moon_quadtree_lod(MoonQuadTree *tree, Vector3 local_camera_pos, float camera_fov, int screen_height, int max_depth) {
  for (int i = 0; i < CUBE_FACE_COUNT; i++) {
    if (tree->faces[i] != NULL && is_face_visible((CubeFace)i, local_camera_pos, tree->radius)) {
      process_face_lod(tree->faces[i], local_camera_pos, camera_fov, screen_height, max_depth);
      merge_face_lod(tree->faces[i], local_camera_pos, camera_fov, screen_height);
    }
  }
}

// ============================================================================
// Initialization
// ============================================================================

MoonQuadTree *create_moon_quadtree(float radius) {
  MoonQuadTree *tree = malloc(sizeof(MoonQuadTree));
  if (tree == NULL) return NULL;

  tree->radius = radius;

  for (int face = 0; face < CUBE_FACE_COUNT; face++) {
    tree->faces[face] = create_quadtree_node(-1.0f, -1.0f, 2.0f, 2.0f, 0, NULL);
    if (tree->faces[face] == NULL) {
      for (int j = 0; j < face; j++) {
        if (tree->faces[j] != NULL) {
          delete_quadtree_node(tree->faces[j], cleanup_mesh_data);
        }
      }
      free(tree);
      return NULL;
    }

    MoonQuadTreeMeshData *root_data = malloc(sizeof(MoonQuadTreeMeshData));
    if (root_data == NULL) {
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
    root_data->mesh = create_moon_patch_mesh((CubeFace)face, -1.0f, 1.0f, -1.0f, 1.0f,
                                              radius, &g_heightmap, chunk_resolution, SKIRT_DEPTH);
    root_data->center_elevation = get_center_elevation((CubeFace)face, -1.0f, 1.0f, -1.0f, 1.0f, radius);

    tree->faces[face]->user_data = root_data;
  }

  return tree;
}

void destroy_moon_quadtree(MoonQuadTree *tree) {
  if (tree == NULL) return;

  for (int i = 0; i < CUBE_FACE_COUNT; i++) {
    if (tree->faces[i] != NULL) {
      delete_quadtree_node(tree->faces[i], cleanup_mesh_data);
    }
  }
  free(tree);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
  const int screenWidth = 1280;
  const int screenHeight = 720;
  InitWindow(screenWidth, screenHeight, "Moon Quadtree - Texture-Based Terrain");
  SetTargetFPS(60);
  ToggleBorderlessWindowed();

  // Load Moon textures
  // Default paths - user should download these from NASA or similar sources
  const char *heightmap_path = "textures/moon_displacement.png";
  const char *albedo_path = "textures/moon_albedo.png";

  if (!load_moon_textures(heightmap_path, albedo_path)) {
    printf("\n");
    printf("=============================================================\n");
    printf("Moon textures not found!\n");
    printf("Please download Moon textures and place them in:\n");
    printf("  textures/moon_displacement.png (heightmap)\n");
    printf("  textures/moon_albedo.png (color texture)\n");
    printf("\n");
    printf("Recommended sources:\n");
    printf("  NASA CGI Moon Kit: https://svs.gsfc.nasa.gov/4720\n");
    printf("  Solar System Scope: https://www.solarsystemscope.com/textures/\n");
    printf("=============================================================\n");
    CloseWindow();
    return -1;
  }

  // Create Moon quadtree
  MoonQuadTree *moon = create_moon_quadtree(MOON_RADIUS);
  if (moon == NULL) {
    unload_moon_textures();
    CloseWindow();
    return -1;
  }

  // Camera setup
  Camera3D camera = {0};
  camera.position = (Vector3){0.0f, MOON_RADIUS + 50000.0f, 0.0f};
  camera.target = (Vector3){0.0f, MOON_RADIUS, 0.0f};
  camera.up = (Vector3){0.0f, 0.0f, 1.0f};
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  // State
  int enable_wireframe = false;
  int enable_lod = true;
  int enable_lighting = true;

  DisableCursor();

  // ===== Shadow Mapping Setup =====

  float lightYaw = 120.0f;
  float lightPitch = -25.0f;
  Vector3 lightDir = {cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                      sinf(lightPitch * DEG2RAD),
                      cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
  lightDir = Vector3Normalize(lightDir);
  Color lightColor = WHITE;
  Vector4 lightColorNormalized = ColorNormalize(lightColor);

  float cascadeSplits[CASCADE_COUNT + 1];
  cascadeSplits[0] = 100.0f;
  cascadeSplits[1] = 5000.0f;
  cascadeSplits[2] = 50000.0f;
  cascadeSplits[3] = 500000.0f;
  cascadeSplits[4] = 5000000.0f;

  const float lightViewDistance = 128.0f;
  Camera3D lightCameras[CASCADE_COUNT];
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    lightCameras[i].position = Vector3Scale(lightDir, -200.0f);
    lightCameras[i].target = Vector3Zero();
    lightCameras[i].projection = CAMERA_ORTHOGRAPHIC;
    lightCameras[i].up = (Vector3){0.0f, 1.0f, 0.0f};
    lightCameras[i].fovy = lightViewDistance;
  }

  // Load Moon shader
  const Shader moonShader = LoadShader("shaders/moon.vs", "shaders/moon.fs");
  if (moonShader.id == 0) {
    destroy_moon_quadtree(moon);
    unload_moon_textures();
    CloseWindow();
    return -1;
  }

  moonShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(moonShader, "viewPos");
  int lightDirLoc = GetShaderLocation(moonShader, "lightDir");
  int lightColLoc = GetShaderLocation(moonShader, "lightColor");
  int ambientLoc = GetShaderLocation(moonShader, "ambient");

  SetShaderValue(moonShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  SetShaderValue(moonShader, lightColLoc, &lightColorNormalized, SHADER_UNIFORM_VEC4);

  float ambient[4] = {0.02f, 0.02f, 0.02f, 1.0f};  // Very low ambient for Moon
  SetShaderValue(moonShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

  for (int i = 0; i < CASCADE_COUNT + 1; i++) {
    char locName[32];
    sprintf(locName, "cascadeSplits[%d]", i);
    int loc = GetShaderLocation(moonShader, locName);
    SetShaderValue(moonShader, loc, &cascadeSplits[i], SHADER_UNIFORM_FLOAT);
  }

  int shadowMapResolution = SHADOWMAP_RESOLUTION;
  SetShaderValue(moonShader, GetShaderLocation(moonShader, "shadowMapResolution"),
                 &shadowMapResolution, SHADER_UNIFORM_INT);

  int debugCascadesLoc = GetShaderLocation(moonShader, "debugCascades");
  int debugCascades = 0;
  SetShaderValue(moonShader, debugCascadesLoc, &debugCascades, SHADER_UNIFORM_INT);

  Matrix lightViews[CASCADE_COUNT] = {0};
  Matrix lightProjs[CASCADE_COUNT] = {0};
  Matrix lightViewProjs[CASCADE_COUNT] = {0};

  int lightVPLocs[CASCADE_COUNT];
  int shadowMapLocs[CASCADE_COUNT];
  for (int i = 0; i < CASCADE_COUNT; i++) {
    char locName[32];
    sprintf(locName, "lightVPs[%d]", i);
    lightVPLocs[i] = GetShaderLocation(moonShader, locName);
    sprintf(locName, "shadowMaps[%d]", i);
    shadowMapLocs[i] = GetShaderLocation(moonShader, locName);
  }

  // Create Moon material with albedo texture
  Material moonMaterial = LoadMaterialDefault();
  moonMaterial.shader = moonShader;
  moonMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = g_albedo_texture;

  RenderTexture2D shadowMaps[CASCADE_COUNT];
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    shadowMaps[i] = LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
    if (shadowMaps[i].id == 0) {
      destroy_moon_quadtree(moon);
      unload_moon_textures();
      CloseWindow();
      return -1;
    }
  }

  FrustumBounds cascadeBounds[CASCADE_COUNT];
  float aspectRatio = (float)screenWidth / (float)screenHeight;

  const Vector3 moonCenter = {0.0f, 0.0f, 0.0f};

  while (!WindowShouldClose()) {
    float deltaTime = GetFrameTime();

    double t_lod_start = 0, t_lod_end = 0;
    double t_shadows_start = 0, t_shadows_end = 0;
    double t_render_start = 0, t_render_end = 0;

    if (IsKeyPressed(KEY_F)) {
      enable_wireframe = !enable_wireframe;
      if (enable_wireframe) rlEnableWireMode();
      else rlDisableWireMode();
    }

    if (IsKeyPressed(KEY_L)) {
      enable_lod = !enable_lod;
    }

    if (IsKeyPressed(KEY_C)) {
      debugCascades = !debugCascades;
      SetShaderValue(moonShader, debugCascadesLoc, &debugCascades, SHADER_UNIFORM_INT);
    }

    if (IsKeyPressed(KEY_G)) {
      enable_lighting = !enable_lighting;
    }

    const float lightRotateSpeed = 60.0f;
    if (IsKeyDown(KEY_LEFT)) lightYaw += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_RIGHT)) lightYaw -= lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_UP)) lightPitch += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_DOWN)) lightPitch -= lightRotateSpeed * deltaTime;

    if (lightPitch > 89.0f) lightPitch = 89.0f;
    if (lightPitch < -89.0f) lightPitch = -89.0f;

    lightDir = (Vector3){cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                         sinf(lightPitch * DEG2RAD),
                         cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
    lightDir = Vector3Normalize(lightDir);
    SetShaderValue(moonShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

    UpdateCameraMovement(&camera, deltaTime);

    Vector3 cameraPos = camera.position;
    SetShaderValue(moonShader, moonShader.locs[SHADER_LOC_VECTOR_VIEW],
                   &cameraPos, SHADER_UNIFORM_VEC3);

    if (IsKeyPressed(KEY_RIGHT_BRACKET)) chunk_resolution = (chunk_resolution < 128) ? chunk_resolution + 4 : 128;
    if (IsKeyPressed(KEY_LEFT_BRACKET)) chunk_resolution = (chunk_resolution > 4) ? chunk_resolution - 4 : 4;

    if (IsKeyPressed(KEY_EQUAL)) min_depth = (min_depth < MAX_DEPTH) ? min_depth + 1 : MAX_DEPTH;
    if (IsKeyPressed(KEY_MINUS)) min_depth = (min_depth > 0) ? min_depth - 1 : 0;

    const float zoomSpeed = 30.0f;
    if (IsKeyDown(KEY_Z)) camera.fovy = fmaxf(10.0f, camera.fovy - zoomSpeed * deltaTime);
    if (IsKeyDown(KEY_X)) camera.fovy = fminf(90.0f, camera.fovy + zoomSpeed * deltaTime);

    if (IsKeyDown(KEY_COMMA)) moon_rotation_speed -= 10.0f * deltaTime;
    if (IsKeyDown(KEY_PERIOD)) moon_rotation_speed += 10.0f * deltaTime;

    moon_rotation_angle += moon_rotation_speed * deltaTime;
    Matrix moon_transform = MatrixRotateY(moon_rotation_angle * DEG2RAD);
    Matrix moon_transform_inv = MatrixInvert(moon_transform);

    Vector3 local_camera_pos = Vector3Transform(camera.position, moon_transform_inv);

    t_lod_start = GetTime();
    if (enable_lod) {
      process_moon_quadtree_lod(moon, local_camera_pos, camera.fovy, GetScreenHeight(), MAX_DEPTH);
    }
    t_lod_end = GetTime();

    Vector3 lightUp = compute_stable_up(lightDir);
    Vector3 lightRight = Vector3CrossProduct(lightDir, lightUp);

    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      if (i < 3) {
        float nearDist = cascadeSplits[i];
        float farDist = cascadeSplits[i + 1];

        Vector3 corners[8];
        get_frustum_corners(camera, nearDist, farDist, aspectRatio, corners);

        FrustumBounds bounds = compute_light_frustum_bounds(corners, lightDir, lightUp);

        float padX = (bounds.maxX - bounds.minX) * 0.1f;
        float padY = (bounds.maxY - bounds.minY) * 0.1f;
        bounds.minX -= padX;
        bounds.maxX += padX;
        bounds.minY -= padY;
        bounds.maxY += padY;

        float depth = bounds.maxZ - bounds.minZ;
        bounds.minZ -= depth * 2.0f;

        cascadeBounds[i] = bounds;

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
        lightCameras[i].fovy = MOON_RADIUS * 2.2f;
        float backDistance = MOON_RADIUS * 2.5f;
        lightCameras[i].target = moonCenter;
        lightCameras[i].position = Vector3Add(moonCenter, Vector3Scale(lightDir, -backDistance));

        cascadeBounds[i] = (FrustumBounds){0};
      }

      lightCameras[i].up = lightUp;
    }

    // Shadow pass
    t_shadows_start = GetTime();
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      BeginTextureMode(shadowMaps[i]);
      ClearBackground(WHITE);

      BeginMode3D(lightCameras[i]);
      lightViews[i] = rlGetMatrixModelview();

      if (i < 3) {
        FrustumBounds b = cascadeBounds[i];
        float depth = b.maxZ - b.minZ;

        float halfWidth = (b.maxX - b.minX) * 0.5f;
        float halfHeight = (b.maxY - b.minY) * 0.5f;

        Matrix customProj = MatrixOrtho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                        0.1f, depth * 3.0f);
        rlSetMatrixProjection(customProj);
        lightProjs[i] = customProj;
      } else {
        float nearPlane = MOON_RADIUS * 1.0f;
        float farPlane = MOON_RADIUS * 4.0f;
        rlSetClipPlanes(nearPlane, farPlane);
        lightProjs[i] = rlGetMatrixProjection();
      }

      draw_moon_quadtree(moon, local_camera_pos, moonMaterial, moon_transform);

      EndMode3D();
      EndTextureMode();

      lightViewProjs[i] = MatrixMultiply(lightViews[i], lightProjs[i]);
    }
    t_shadows_end = GetTime();

    // Main render pass
    rlSetClipPlanes(10.0f, 10000000.0f);

    BeginDrawing();
    ClearBackground(BLACK);

    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      SetShaderValueMatrix(moonShader, lightVPLocs[i], lightViewProjs[i]);
    }

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
      vertexCount = draw_moon_quadtree(moon, local_camera_pos, moonMaterial, moon_transform);
    } else {
      Material unlitMat = LoadMaterialDefault();
      unlitMat.maps[MATERIAL_MAP_DIFFUSE].texture = g_albedo_texture;
      vertexCount = draw_moon_quadtree(moon, local_camera_pos, unlitMat, moon_transform);
    }

    EndMode3D();
    t_render_end = GetTime();

    // UI
    DrawText("Moon Quadtree - Texture-Based Terrain", 10, 10, 20, WHITE);
    DrawText("WASD: Move | Mouse: Look | Shift: Speed", 10, 40, 20, WHITE);
    DrawText("Arrows: Move Light | F: Wireframe | L: LOD | C: Debug | G: Lighting", 10, 70, 20, WHITE);
    DrawText(TextFormat("F: Wire %s | L: LOD %s | C: Debug %s | G: Light %s",
             enable_wireframe ? "ON" : "OFF",
             enable_lod ? "ON" : "OFF",
             debugCascades ? "ON" : "OFF",
             enable_lighting ? "ON" : "OFF"), 10, 100, 20, WHITE);

    float altitude = Vector3Length(camera.position) - MOON_RADIUS;
    DrawText(TextFormat("Altitude: %.1f km", altitude / 1000.0f), 10, 130, 20, YELLOW);
    DrawText(TextFormat("Camera: (%.0f, %.0f, %.0f)",
             camera.position.x, camera.position.y, camera.position.z), 10, 160, 20, GRAY);

    DrawText(TextFormat("[ / ]: Chunk Res (%d) | - / =: Min Depth (%d)", chunk_resolution, min_depth), 10, 185, 20, YELLOW);
    DrawText(TextFormat("< / >: Rotation Speed (%.1f deg/s)", moon_rotation_speed), 10, 210, 20, YELLOW);
    DrawText(TextFormat("Z / X: Zoom (FOV %.1f)", camera.fovy), 10, 235, 20, YELLOW);

    double lod_ms = (t_lod_end - t_lod_start) * 1000.0;
    double shadow_ms = (t_shadows_end - t_shadows_start) * 1000.0;
    double render_ms = (t_render_end - t_render_start) * 1000.0;

    DrawText(TextFormat("LOD Update: %.2f ms", lod_ms), 10, 265, 20, GREEN);
    DrawText(TextFormat("Shadow Maps: %.2f ms", shadow_ms), 10, 295, 20, GREEN);
    DrawText(TextFormat("Main Render: %.2f ms", render_ms), 10, 325, 20, GREEN);
    DrawText(TextFormat("Active Vertices: %d", vertexCount), 10, 355, 20, GREEN);

    DrawText(TextFormat("Textures: %dx%d heightmap, %dx%d albedo",
             g_heightmap.heightmap.width, g_heightmap.heightmap.height,
             g_albedo_texture.width, g_albedo_texture.height), 10, 385, 20, SKYBLUE);

    DrawFPS(screenWidth - 100, 10);

    EndDrawing();
  }

  // Cleanup
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    UnloadRenderTexture(shadowMaps[i]);
  }
  UnloadShader(moonShader);
  UnloadMaterial(moonMaterial);
  destroy_moon_quadtree(moon);
  unload_moon_textures();
  CloseWindow();

  return 0;
}
