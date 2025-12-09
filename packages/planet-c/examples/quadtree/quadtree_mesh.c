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
// clang-format on

const float MAX_SCALE = 1700000.0f;
const int MAX_DEPTH = 8;

typedef struct {
  Mesh mesh;
  Vector3 position; // World-space position of this mesh patch
  float scale;      // Size of this mesh patch
} QuadTreeMeshData;

void cleanup_mesh_data(void *user_data) {
  if (user_data == NULL) {
    return;
  }
  QuadTreeMeshData *mesh_data = (QuadTreeMeshData *)user_data;
  UnloadMesh(mesh_data->mesh);
  free(mesh_data);
}

// Helper to recreate mesh data for a node that becomes a leaf after merge
void *recreate_parent_mesh_data(QuadTreeNode *node) {
  if (node == NULL || node->user_data != NULL) {
    return node->user_data; // Already has data or invalid node
  }

  // Calculate position by traversing up to root and summing offsets
  Vector3 position = {0.0f, 0.0f, 0.0f};
  float scale = MAX_SCALE; // Start with root scale

  QuadTreeNode *current = node;
  QuadTreeNode *path[MAX_DEPTH]; // Max depth we support
  int depth = 0;

  // Build path from node to root
  while (current->parent != NULL && depth < MAX_DEPTH) {
    path[depth++] = current;
    current = current->parent;
  }

  // Traverse from root down, calculating position and scale
  for (int i = depth - 1; i >= 0; i--) {
    QuadTreeNode *parent_node = path[i]->parent;
    QuadTreeNode *child_node = path[i];

    // Find child index
    short index = -1;
    for (unsigned short j = 0; j < 4; j++) {
      if (parent_node->children[j] == child_node) {
        index = j;
        break;
      }
    }

    if (index != -1) {
      scale *= 0.5f;
      const float offset = scale * 0.5f;
      const float x_offset = (index % 2 == 0) ? -offset : offset;
      const float z_offset = (index < 2) ? -offset : offset;
      position = Vector3Add(position, (Vector3){x_offset, 0, z_offset});
    }
  }

  // Create mesh for this node
  Mesh mesh = create_plane_mesh_with_noise_ws((Vector3){node->x, 0, node->y},
                                              scale, 50);

  QuadTreeMeshData *data = malloc(sizeof(QuadTreeMeshData));
  if (data == NULL) {
    UnloadMesh(mesh);
    return NULL;
  }

  data->mesh = mesh;
  data->position = position;
  data->scale = scale;

  return data;
}

void *create_child_mesh_data(QuadTreeNode *parent, QuadTreeNode *child,
                             void *parent_user_data) {
  if (parent_user_data == NULL) {
    return NULL;
  }

  QuadTreeMeshData *parent_data = (QuadTreeMeshData *)parent_user_data;

  // 1. Determine Child Index
  short index = -1;
  for (unsigned short i = 0; i < 4; i++) {
    if (parent->children[i] == child) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    // Child not found in parent's children array
    return NULL;
  }

  // 2. Calculate Child Position Offset
  const float child_scale = parent_data->scale * 0.5f;
  const float offset = child_scale * 0.5f;
  const float x_offset = (index % 2 == 0) ? -offset : offset;
  const float z_offset = (index < 2) ? -offset : offset;
  const float y_offset = 0;

  Vector3 position = Vector3Add(parent_data->position,
                                (Vector3){x_offset, y_offset, z_offset});

  // 3. Generate Child Mesh
  Mesh mesh = create_plane_mesh_with_noise_ws((Vector3){child->x, 0, child->y},
                                              child_scale, 50);

  // 4. Allocate and Populate return structure
  QuadTreeMeshData *child_data = malloc(sizeof(QuadTreeMeshData));
  if (child_data == NULL) {
    UnloadMesh(mesh);
    return NULL;
  }

  child_data->mesh = mesh;
  child_data->position = position;
  child_data->scale = child_scale;

  return child_data;
}

// Draw all quadtree meshes recursively (only draws leaf nodes)
void draw_quadtree_meshes(QuadTreeNode *node, Material material) {
  if (node == NULL) {
    return;
  }

  // Check if this is a leaf node (no children)
  bool is_leaf = true;
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] != NULL) {
      is_leaf = false;
      break;
    }
  }

  // If leaf node, draw its mesh
  if (is_leaf && node->user_data != NULL) {
    QuadTreeMeshData *mesh_data = (QuadTreeMeshData *)node->user_data;

    Matrix transform = MatrixTranslate(node->x, 0, node->y);
    DrawMesh(mesh_data->mesh, material, transform);

    // Debug bounding boxs
    // Vector3 bounds = (Vector3){mesh_data->scale, 0.0f, mesh_data->scale};
    // Vector3 top_left =
    //    Vector3Subtract(mesh_data->position,
    //                    Vector3Multiply(bounds, (Vector3){0.5f, 0.5f, 0.5f}));
    // BoundingBox bounding_box =
    //
    //                    (BoundingBox){top_left, Vector3Add(top_left, bounds)};
    // DrawBoundingBox(bounding_box, GREEN);
  }

  // If not leaf, recursively draw children
  if (!is_leaf) {
    for (unsigned short i = 0; i < 4; i++) {
      draw_quadtree_meshes(node->children[i], material);
    }
  }
}

int main(void) {
  // ===== Raylib Init =====
  const int screenWidth = 1280;
  const int screenHeight = 720;
  InitWindow(screenWidth, screenHeight, "Planet Renderer - Shadows");
  SetTargetFPS(60);
  ToggleBorderlessWindowed();

  // ===== Misc =====
  int enable_wireframe = false;

  // ===== Initialize Quadtree =====
  QuadTreeMeshData *mesh_data = malloc(sizeof(QuadTreeMeshData));
  if (mesh_data == NULL) {
    return 0;
  }
  mesh_data->mesh = create_plane_mesh_with_noise_ws((Vector3){0.0f, 0.0f, 0.0f},
                                                    MAX_SCALE, 50);
  mesh_data->position = (Vector3){0.0f, 0.0f, 0.0f};
  mesh_data->scale = MAX_SCALE;
  QuadTreeNode *root_node = create_quadtree_node(
      -MAX_SCALE * 0.5f, -MAX_SCALE * 0.5f, MAX_SCALE, MAX_SCALE, 0, NULL);
  root_node->user_data = mesh_data;

  // ===== Set up 3D Camera =====
  Camera3D camera = {0};
  camera.position = (Vector3){0.0f, 100.0f, 100.0f};
  camera.target = (Vector3){0.0f, 0.0f, 0.0f};
  camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;
  // rlSetClipPlanes(0.1, 3400000);

  // ===== Create Material =====
  Material material = LoadMaterialDefault();

  // ===== Mode State =====
  typedef enum { MODE_MANUAL, MODE_AUTOMATIC } Mode;
  Mode current_mode = MODE_AUTOMATIC;

  // ===== Camera Rotation State =====
  float camera_yaw = 0.0f;   // Horizontal rotation
  float camera_pitch = 0.0f; // Vertical rotation
  DisableCursor();           // Hide and lock cursor for FPS-style control

  // ===== Shadow maps =====
  // Set up the light direction
  float lightYaw = 120.0f;
  float lightPitch = -25.0f;
  Vector3 lightDir = {cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                      sinf(lightPitch * DEG2RAD),
                      cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
  lightDir = Vector3Normalize(lightDir);
  Color lightColor = WHITE;
  Vector4 lightColorNormalized = ColorNormalize(lightColor);

  // Define cascade split distances
  // 1 unit = 1 meter
  float cascadeSplits[CASCADE_COUNT + 1];
  cascadeSplits[0] = 0.1f;     // 10cm - Near plane
  cascadeSplits[1] = 500.0f;   // 500m - End of cascade 0 (surface detail)
  cascadeSplits[2] = 5000.0f;  // 5km - End of cascade 1 (regional terrain)
  cascadeSplits[3] = 50000.0f; // 50km - End of cascade 2 (horizon/mountains)
  cascadeSplits[4] =
      500000.0f; // 500km - Far plane (cascade 3 - full planet from orbit)

  // Set up the light camera
  const float lightViewDistance = 128.0f;
  Camera3D lightCameras[CASCADE_COUNT];
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    lightCameras[i].position = Vector3Scale(lightDir, -200.0f);
    lightCameras[i].target = Vector3Zero();
    lightCameras[i].projection = CAMERA_ORTHOGRAPHIC;
    lightCameras[i].up = (Vector3){0.0f, 1.0f, 0.0f};
    lightCameras[i].fovy = lightViewDistance; // Will adjust per-cascade later
  }

  // Load shadow shader
  const Shader shadowShader =
      LoadShader("shaders/shadowmap.vs", "shaders/shadowmap.fs");
  if (shadowShader.id == 0) {
    CloseWindow();
    return -1;
  }

  // Set up shader uniforms
  shadowShader.locs[SHADER_LOC_VECTOR_VIEW] =
      GetShaderLocation(shadowShader, "viewPos");

  int lightDirLoc = GetShaderLocation(shadowShader, "lightDir");
  int lightColLoc = GetShaderLocation(shadowShader, "lightColor");
  int ambientLoc = GetShaderLocation(shadowShader, "ambient");
  int lightVPsLoc = GetShaderLocation(shadowShader, "lightVPs");
  int shadowMapsLoc = GetShaderLocation(shadowShader, "shadowMaps");
  int cascadeSplitsLoc = GetShaderLocation(shadowShader, "cascadeSplits");

  SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized,
                 SHADER_UNIFORM_VEC4);

  // float ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
  float ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  SetShaderValue(shadowShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

  // Set cascade splits individually to ensure correct array handling
  for (int i = 0; i < CASCADE_COUNT + 1; i++) {
    char locName[32];
    sprintf(locName, "cascadeSplits[%d]", i);
    int loc = GetShaderLocation(shadowShader, locName);
    SetShaderValue(shadowShader, loc, &cascadeSplits[i], SHADER_UNIFORM_FLOAT);
  }

  int shadowMapResolution = SHADOWMAP_RESOLUTION;
  SetShaderValue(shadowShader,
                 GetShaderLocation(shadowShader, "shadowMapResolution"),
                 &shadowMapResolution, SHADER_UNIFORM_INT);

  // Store light matrices
  Matrix lightViews[CASCADE_COUNT] = {0};
  Matrix lightProjs[CASCADE_COUNT] = {0};
  Matrix lightViewProjs[CASCADE_COUNT] = {0};
  int textureActiveSlot = 10;

  // Create material
  Material shadowMaterial = LoadMaterialDefault();
  shadowMaterial.shader = shadowShader;

  // Load shadow map render textures
  RenderTexture2D shadowMaps[CASCADE_COUNT];
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    shadowMaps[i] =
        LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
    if (shadowMaps[i].id == 0) {
      CloseWindow();
      return -1;
    }
  }

  while (!WindowShouldClose()) {
    // ===== Wireframe =====
    if (IsKeyPressed(KEY_F)) {
      enable_wireframe = !enable_wireframe;
      if (enable_wireframe) {
        rlEnableWireMode();
      } else {
        rlDisableWireMode();
      }
    }

    float deltaTime = GetFrameTime();

    // Update camera position for shader
    Vector3 cameraPos = camera.position;
    SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW],
                   &cameraPos, SHADER_UNIFORM_VEC3);
    UpdateCameraMovement(&camera, deltaTime);

    // ===== LOD Processing =====
    if (current_mode == MODE_AUTOMATIC) {
      Vector2 cam_pos_2d = (Vector2){camera.position.x, camera.position.z};
      process_leaf_nodes(root_node, cam_pos_2d, MAX_DEPTH,
                         create_child_mesh_data, cleanup_mesh_data);
      merge_distant_leaves(root_node, cam_pos_2d, cleanup_mesh_data,
                           recreate_parent_mesh_data);
    }

    // Move light with arrow keys
    const float lightRotateSpeed = 60.0f;
    if (IsKeyDown(KEY_LEFT))
      lightYaw += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_RIGHT))
      lightYaw -= lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_UP))
      lightPitch += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_DOWN))
      lightPitch -= lightRotateSpeed * deltaTime;

    // Clamp pitch to avoid flipping
    if (lightPitch > 89.0f)
      lightPitch = 89.0f;
    if (lightPitch < -89.0f)
      lightPitch = -89.0f;

    // Recalculate light direction
    lightDir = (Vector3){cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                         sinf(lightPitch * DEG2RAD),
                         cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
    lightDir = Vector3Normalize(lightDir);

    // Make light camera shadow map coverage shift based on view direction
    Vector3 viewDir =
        Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    float alignment = Vector3DotProduct(viewDir, lightDir); // -1 to 1

    // Shift shadow map coverage forward when looking away from light
    // Small multiplier (0.2) to keep most coverage near player
    float lookAheadDistance = (1.0f - alignment) * 0.2f * lightViewDistance;

    Vector3 lookAheadTarget =
        Vector3Add(camera.position, Vector3Scale(viewDir, lookAheadDistance));
    // Update light cameras for each cascade
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      // Calculate coverage area for this cascade based on split distances
      float cascadeDistance =
          cascadeSplits[i + 1]; // End distance of this cascade

      // For orthographic projection, fovy determines the height of the view
      // volume We need to ensure it covers the view frustum slice at this
      // distance A rough approximation is to use the cascade distance itself as
      // the coverage size
      lightCameras[i].fovy =
          cascadeDistance * 2.0f; // Ensure plenty of coverage

      // Position the light camera far enough back to see potential casters
      // (e.g. mountains) The "back" distance needs to scale with the cascade
      // size
      float backDistance = cascadeDistance * 2.0f;

      // For now, all cascades follow the camera (camerea-relative)
      lightCameras[i].target = lookAheadTarget;
      lightCameras[i].position =
          Vector3Add(lookAheadTarget, Vector3Scale(lightDir, -backDistance));
    }
    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

    // ===== PASS 1: Render shadow map from light's perspective =====
    const double shadow_map_start = GetTime();
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      // Set clip planes appropriate for this cascade's range
      float cascadeRange = cascadeSplits[i + 1] - cascadeSplits[i];
      float nearPlane = 0.1f;
      float farPlane = cascadeRange * 4.0f; // 4x range to ensure full coverage
      rlSetClipPlanes(nearPlane, farPlane);

      BeginTextureMode(shadowMaps[i]);
      ClearBackground(WHITE);

      BeginMode3D(lightCameras[i]);
      lightViews[i] = rlGetMatrixModelview();
      lightProjs[i] = rlGetMatrixProjection();
      draw_quadtree_meshes(root_node, shadowMaterial);
      EndMode3D();
      EndTextureMode();

      lightViewProjs[i] = MatrixMultiply(lightViews[i], lightProjs[i]);
    }
    double shadow_map_time = GetTime() - shadow_map_start;

    // ===== PASS 2: Render main scene with shadows =====
    // Set clip planes for Earth-scale rendering
    // Near: 1 km, Far: 100,000 km (to see the whole planet from space)
    rlSetClipPlanes(.1, 100000000.0);

    BeginDrawing();
    ClearBackground(BLACK);

    // Set all cascade light view-projection matrices
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      char locName[32];
      sprintf(locName, "lightVPs[%d]", i);
      int loc = GetShaderLocation(shadowShader, locName);
      SetShaderValueMatrix(shadowShader, loc, lightViewProjs[i]);
    }

    // Bind all cascade shadow maps to texture slots
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      int textureSlot = 10 + i; // Use slots 10, 11, 12, 13
      rlActiveTextureSlot(textureSlot);
      rlEnableTexture(shadowMaps[i].depth.id);

      // Set uniform for each cascade's sampler
      char locName[32];
      sprintf(locName, "shadowMaps[%d]", i);
      int loc = GetShaderLocation(shadowShader, locName);
      rlSetUniform(loc, &textureSlot, SHADER_UNIFORM_INT, 1);
    }

    BeginMode3D(camera);

    draw_quadtree_meshes(root_node, shadowMaterial);

    EndMode3D();

    // ===== Draw UI Text =====
    DrawText(current_mode == MODE_MANUAL ? "Mode: Manual (M)"
                                         : "Mode: Automatic (T)",
             10, 10, 20, WHITE);
    DrawText("WASD: Move | Space/Ctrl: Up/Down | Shift: Speed Boost", 10, 40,
             20, WHITE);
    DrawText("Mouse: Look Around", 10, 70, 20, WHITE);
    DrawText(TextFormat("Camera Pos: (%.1f, %.1f, %.1f)", camera.position.x,
                        camera.position.y, camera.position.z),
             10, 100, 20, YELLOW);

    EndDrawing();
  }

  delete_quadtree_node(root_node, cleanup_mesh_data);
  UnloadMaterial(material);
  CloseWindow();

  return 0;
}
