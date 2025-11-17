#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "rlgl.h"
#include "../include/quadtree.h"

#define MAX_PLANE_CHUNKS 1024

// Forward declarations
typedef float (*HeightGenerator)(Vector3 worldPosition, float radius, void* userData);
typedef Color (*ColorGenerator)(Vector3 worldPosition, float height, void* userData);

// Simple flat chunk structure (not using the spherical chunk from chunk.h)
typedef struct FlatChunk {
    Mesh mesh;
    Model model;
    Vector3 center;      // Center of the chunk in world space
    float size;          // Size of the chunk
    int resolution;      // Mesh resolution
    bool visible;
    bool meshGenerated;
} FlatChunk;

// Chunk map entry for flat plane
typedef struct PlaneChunkEntry {
    char key[64];
    FlatChunk* chunk;
    bool active;
} PlaneChunkEntry;

// Flat plane structure
typedef struct FlatPlane {
    float size;              // Total size of the plane
    float minCellSize;       // Minimum cell size for LOD
    int minCellResolution;   // Resolution of smallest chunks
    float lodDistanceComparisonValue; // LOD distance multiplier
    Vector3 position;        // Plane center position

    PlaneChunkEntry chunkMap[MAX_PLANE_CHUNKS];
    int chunkCount;

    HeightGenerator heightGen;
    ColorGenerator colorGen;
    void* userData;

    QuadTree* quadtree;
} FlatPlane;

// FlatChunk functions
void FlatChunk_Destroy(FlatChunk* chunk) {
    if (!chunk) return;
    if (chunk->meshGenerated) {
        UnloadMesh(chunk->mesh);
        UnloadModel(chunk->model);
    }
    free(chunk);
}

// Generate a flat mesh for a chunk
void FlatChunk_GenerateMesh(FlatChunk* chunk, HeightGenerator heightGen,
                            ColorGenerator colorGen, void* userData) {
    int resolution = chunk->resolution;
    int vertexCount = (resolution + 1) * (resolution + 1);
    int indexCount = resolution * resolution * 6;

    // Allocate arrays
    float* positions = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    float* normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    unsigned char* colors = (unsigned char*)MemAlloc(vertexCount * 4 * sizeof(unsigned char));
    float* uvs = (float*)MemAlloc(vertexCount * 2 * sizeof(float));
    unsigned short* indices = (unsigned short*)MemAlloc(indexCount * sizeof(unsigned short));

    float halfSize = chunk->size / 2.0f;
    float step = chunk->size / resolution;

    // Generate vertices
    int vidx = 0;
    for (int z = 0; z <= resolution; z++) {
        for (int x = 0; x <= resolution; x++) {
            float xPos = -halfSize + x * step;
            float zPos = -halfSize + z * step;

            // World position for this vertex
            Vector3 worldPos = {
                chunk->center.x + xPos,
                0.0f,
                chunk->center.z + zPos
            };

            // Get height from generator
            float height = 0.0f;
            if (heightGen) {
                height = heightGen(worldPos, 0.0f, userData);
            }
            worldPos.y = height;

            // Store position (relative to chunk center)
            positions[vidx * 3 + 0] = xPos;
            positions[vidx * 3 + 1] = height;
            positions[vidx * 3 + 2] = zPos;

            // Get color
            Color color = WHITE;
            if (colorGen) {
                color = colorGen(worldPos, height, userData);
            }
            colors[vidx * 4 + 0] = color.r;
            colors[vidx * 4 + 1] = color.g;
            colors[vidx * 4 + 2] = color.b;
            colors[vidx * 4 + 3] = color.a;

            // Default normal (will calculate properly later)
            normals[vidx * 3 + 0] = 0.0f;
            normals[vidx * 3 + 1] = 1.0f;
            normals[vidx * 3 + 2] = 0.0f;

            // UVs
            uvs[vidx * 2 + 0] = (float)x / resolution;
            uvs[vidx * 2 + 1] = (float)z / resolution;

            vidx++;
        }
    }

    // Generate indices
    int iidx = 0;
    for (int z = 0; z < resolution; z++) {
        for (int x = 0; x < resolution; x++) {
            int topLeft = z * (resolution + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * (resolution + 1) + x;
            int bottomRight = bottomLeft + 1;

            // Triangle 1
            indices[iidx++] = topLeft;
            indices[iidx++] = bottomLeft;
            indices[iidx++] = topRight;

            // Triangle 2
            indices[iidx++] = topRight;
            indices[iidx++] = bottomLeft;
            indices[iidx++] = bottomRight;
        }
    }

    // Calculate normals
    for (int i = 0; i < indexCount; i += 3) {
        int i1 = indices[i];
        int i2 = indices[i + 1];
        int i3 = indices[i + 2];

        Vector3 v1 = {positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2]};
        Vector3 v2 = {positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2]};
        Vector3 v3 = {positions[i3 * 3], positions[i3 * 3 + 1], positions[i3 * 3 + 2]};

        Vector3 edge1 = Vector3Subtract(v2, v1);
        Vector3 edge2 = Vector3Subtract(v3, v1);
        Vector3 normal = Vector3CrossProduct(edge1, edge2);

        // Add to vertex normals (will normalize later)
        normals[i1 * 3 + 0] += normal.x;
        normals[i1 * 3 + 1] += normal.y;
        normals[i1 * 3 + 2] += normal.z;

        normals[i2 * 3 + 0] += normal.x;
        normals[i2 * 3 + 1] += normal.y;
        normals[i2 * 3 + 2] += normal.z;

        normals[i3 * 3 + 0] += normal.x;
        normals[i3 * 3 + 1] += normal.y;
        normals[i3 * 3 + 2] += normal.z;
    }

    // Normalize normals
    for (int i = 0; i < vertexCount; i++) {
        Vector3 n = {normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]};
        n = Vector3Normalize(n);
        normals[i * 3 + 0] = n.x;
        normals[i * 3 + 1] = n.y;
        normals[i * 3 + 2] = n.z;
    }

    // Create mesh
    chunk->mesh = (Mesh){0};
    chunk->mesh.vertexCount = vertexCount;
    chunk->mesh.triangleCount = indexCount / 3;
    chunk->mesh.vertices = positions;
    chunk->mesh.normals = normals;
    chunk->mesh.colors = colors;
    chunk->mesh.texcoords = uvs;
    chunk->mesh.indices = indices;

    UploadMesh(&chunk->mesh, false);

    // Create model
    chunk->model = LoadModelFromMesh(chunk->mesh);
    chunk->meshGenerated = true;
}

// Create a flat chunk
FlatChunk* FlatChunk_Create(Vector3 center, float size, int resolution,
                            HeightGenerator heightGen, ColorGenerator colorGen, void* userData) {
    FlatChunk* chunk = (FlatChunk*)malloc(sizeof(FlatChunk));
    chunk->center = center;
    chunk->size = size;
    chunk->resolution = resolution;
    chunk->visible = true;
    chunk->meshGenerated = false;

    FlatChunk_GenerateMesh(chunk, heightGen, colorGen, userData);

    return chunk;
}

// Simple perlin-like noise for height
float PlaneNoise(float x, float z) {
    return sinf(x * 0.05f) * cosf(z * 0.05f) +
           sinf(x * 0.1f) * cosf(z * 0.15f) * 0.5f;
}

// Height generator for flat plane
float PlaneHeightGenerator(Vector3 worldPosition, float radius, void* userData) {
    // Use radius parameter as max height
    float noise = PlaneNoise(worldPosition.x, worldPosition.z);
    float maxHeight = 20.0f; // Max height variation
    return maxHeight * noise;
}

// Color generator based on height
Color PlaneColorGenerator(Vector3 worldPosition, float height, void* userData) {
    // Color based on height
    if (height < -5.0f) {
        return BLUE; // Low valleys
    } else if (height < 0.0f) {
        return DARKBLUE; // Shallow valleys
    } else if (height < 5.0f) {
        return GREEN; // Plains
    } else if (height < 10.0f) {
        return DARKGREEN; // Hills
    } else {
        return GRAY; // Mountains
    }
}

// Generate a chunk key for the plane
void MakePlaneChunkKey(Vector3 position, float size, char* outKey) {
    snprintf(outKey, 64, "%.2f_%.2f_%.2f", position.x, position.z, size);
}

// Find chunk in plane
FlatChunk* FindPlaneChunk(FlatPlane* plane, const char* key) {
    for (int i = 0; i < MAX_PLANE_CHUNKS; i++) {
        if (plane->chunkMap[i].active && strcmp(plane->chunkMap[i].key, key) == 0) {
            return plane->chunkMap[i].chunk;
        }
    }
    return NULL;
}

// Add chunk to plane
void AddPlaneChunk(FlatPlane* plane, const char* key, FlatChunk* chunk) {
    for (int i = 0; i < MAX_PLANE_CHUNKS; i++) {
        if (!plane->chunkMap[i].active) {
            strncpy(plane->chunkMap[i].key, key, 64);
            plane->chunkMap[i].chunk = chunk;
            plane->chunkMap[i].active = true;
            plane->chunkCount++;
            return;
        }
    }
    printf("Warning: Chunk map full!\n");
}

// Remove chunk from plane
void RemovePlaneChunk(FlatPlane* plane, const char* key) {
    for (int i = 0; i < MAX_PLANE_CHUNKS; i++) {
        if (plane->chunkMap[i].active && strcmp(plane->chunkMap[i].key, key) == 0) {
            FlatChunk_Destroy(plane->chunkMap[i].chunk);
            plane->chunkMap[i].active = false;
            plane->chunkMap[i].chunk = NULL;
            plane->chunkCount--;
            return;
        }
    }
}

// Create flat plane
FlatPlane* FlatPlane_Create(float size, float minCellSize, int minCellResolution,
                             HeightGenerator heightGen, ColorGenerator colorGen, void* userData) {
    FlatPlane* plane = (FlatPlane*)malloc(sizeof(FlatPlane));

    plane->size = size;
    plane->minCellSize = minCellSize;
    plane->minCellResolution = minCellResolution;
    plane->lodDistanceComparisonValue = 2.0f; // Default LOD multiplier
    plane->position = (Vector3){0.0f, 0.0f, 0.0f};
    plane->chunkCount = 0;

    plane->heightGen = heightGen;
    plane->colorGen = colorGen;
    plane->userData = userData;

    // Initialize chunk map
    for (int i = 0; i < MAX_PLANE_CHUNKS; i++) {
        plane->chunkMap[i].active = false;
        plane->chunkMap[i].chunk = NULL;
    }

    // Create quadtree (identity matrix for flat plane)
    Matrix identity = MatrixIdentity();
    plane->quadtree = QuadTree_Create(identity, size, minCellSize,
                                      plane->position, plane->lodDistanceComparisonValue);

    return plane;
}

// Update plane LOD based on camera position
void FlatPlane_Update(FlatPlane* plane, Vector3 cameraPosition) {
    // Recreate quadtree for this frame
    if (plane->quadtree) {
        QuadTree_Destroy(plane->quadtree);
    }

    Matrix identity = MatrixIdentity();
    plane->quadtree = QuadTree_Create(identity, plane->size, plane->minCellSize,
                                      plane->position, plane->lodDistanceComparisonValue);

    // Insert camera position to subdivide the quadtree
    QuadTree_Insert(plane->quadtree, cameraPosition);

    // Get all leaf nodes (chunks to render)
    QuadTreeNode** nodes = NULL;
    int nodeCount = 0;
    QuadTree_GetChildren(plane->quadtree, &nodes, &nodeCount);

    // Create a set of keys for chunks that should exist
    char** activeKeys = (char**)malloc(sizeof(char*) * nodeCount);
    for (int i = 0; i < nodeCount; i++) {
        activeKeys[i] = (char*)malloc(64);
        MakePlaneChunkKey(nodes[i]->center, nodes[i]->size.x, activeKeys[i]);
    }

    // Remove chunks that are no longer needed
    for (int i = 0; i < MAX_PLANE_CHUNKS; i++) {
        if (!plane->chunkMap[i].active) continue;

        bool found = false;
        for (int j = 0; j < nodeCount; j++) {
            if (strcmp(plane->chunkMap[i].key, activeKeys[j]) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            char key[64];
            strncpy(key, plane->chunkMap[i].key, 64);
            RemovePlaneChunk(plane, key);
        }
    }

    // Create new chunks as needed
    for (int i = 0; i < nodeCount; i++) {
        QuadTreeNode* node = nodes[i];

        FlatChunk* existing = FindPlaneChunk(plane, activeKeys[i]);
        if (existing) {
            // Chunk already exists, it's visible
            continue;
        }

        // Create new chunk
        // The node center is in XY space, but we need it in XZ space for world position
        Vector3 chunkCenter = {node->center.x, 0.0f, node->center.y};
        float chunkSize = node->size.x;  // node->size.x is already the full width

        FlatChunk* chunk = FlatChunk_Create(
            chunkCenter,
            chunkSize,
            plane->minCellResolution,
            plane->heightGen,
            plane->colorGen,
            plane->userData
        );

        AddPlaneChunk(plane, activeKeys[i], chunk);
    }

    // Cleanup
    for (int i = 0; i < nodeCount; i++) {
        free(activeKeys[i]);
    }
    free(activeKeys);
    free(nodes);
}

// Render the plane
void FlatPlane_Render(FlatPlane* plane) {
    for (int i = 0; i < MAX_PLANE_CHUNKS; i++) {
        if (plane->chunkMap[i].active && plane->chunkMap[i].chunk) {
            FlatChunk* chunk = plane->chunkMap[i].chunk;
            if (chunk->visible && chunk->meshGenerated) {
                // Render at chunk center
                DrawModel(chunk->model, chunk->center, 1.0f, WHITE);
            }
        }
    }
}

// Destroy plane
void FlatPlane_Destroy(FlatPlane* plane) {
    if (!plane) return;

    // Destroy all chunks
    for (int i = 0; i < MAX_PLANE_CHUNKS; i++) {
        if (plane->chunkMap[i].active) {
            FlatChunk_Destroy(plane->chunkMap[i].chunk);
        }
    }

    // Destroy quadtree
    if (plane->quadtree) {
        QuadTree_Destroy(plane->quadtree);
    }

    free(plane);
}

int main(void) {
    // Initialization
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Flat Plane LOD - Quadtree Mesh Generation");

    // Define the camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 100.0f, 80.0f, 100.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Create flat plane
    float planeSize = 200.0f;     // 400x400 total plane size
    float minCellSize = 10.0f;    // Minimum cell size for LOD
    int minCellResolution = 32;   // Resolution of chunks

    FlatPlane* plane = FlatPlane_Create(
        planeSize,
        minCellSize,
        minCellResolution,
        PlaneHeightGenerator,
        PlaneColorGenerator,
        NULL
    );

    SetTargetFPS(60);

    bool showWireframe = false;
    bool showInfo = true;

    // Main game loop
    while (!WindowShouldClose()) {
        // Update
        UpdateCamera(&camera, CAMERA_FREE);

        // Toggle wireframe with F key
        if (IsKeyPressed(KEY_F)) {
            showWireframe = !showWireframe;
        }

        // Toggle info with I key
        if (IsKeyPressed(KEY_I)) {
            showInfo = !showInfo;
        }

        // Update plane LOD based on camera position
        FlatPlane_Update(plane, camera.position);

        // Draw
        BeginDrawing();
            ClearBackground(SKYBLUE);

            BeginMode3D(camera);
                // Draw plane
                if (showWireframe) {
                    rlEnableWireMode();
                }

                FlatPlane_Render(plane);

                if (showWireframe) {
                    rlDisableWireMode();
                }

                // Draw grid for reference
                DrawGrid(40, 10.0f);

                // Draw coordinate axes
                DrawLine3D((Vector3){0, 0, 0}, (Vector3){50, 0, 0}, RED);
                DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 50, 0}, GREEN);
                DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 0, 50}, BLUE);

            EndMode3D();

            // Draw UI
            if (showInfo) {
                DrawText("Flat Plane LOD Demo", 10, 10, 20, WHITE);
                DrawText("Demonstrates Quadtree/Dynamic LOD Mesh Generation", 10, 35, 16, LIGHTGRAY);
                DrawText(TextFormat("FPS: %d", GetFPS()), 10, 60, 20, LIME);
                DrawText(TextFormat("Active Chunks: %d", plane->chunkCount), 10, 90, 20, YELLOW);
                DrawText(TextFormat("Camera: (%.1f, %.1f, %.1f)",
                         camera.position.x, camera.position.y, camera.position.z), 10, 120, 16, WHITE);
                DrawText(TextFormat("Min Cell Size: %.1f", plane->minCellSize), 10, 145, 16, WHITE);

                DrawText("Controls:", 10, 180, 16, LIGHTGRAY);
                DrawText("  WASD + Mouse: Move camera", 10, 200, 14, LIGHTGRAY);
                DrawText("  F: Toggle wireframe", 10, 220, 14, LIGHTGRAY);
                DrawText("  I: Toggle info", 10, 240, 14, LIGHTGRAY);
                DrawText("  ESC: Exit", 10, 260, 14, LIGHTGRAY);

                DrawText("How it works:", 10, 290, 16, YELLOW);
                DrawText("  - Quadtree subdivides based on camera distance", 10, 310, 14, LIGHTGRAY);
                DrawText("  - Closer areas have higher detail (smaller chunks)", 10, 330, 14, LIGHTGRAY);
                DrawText("  - Farther areas have lower detail (larger chunks)", 10, 350, 14, LIGHTGRAY);
                DrawText("  - This is the foundation for the planet renderer", 10, 370, 14, LIGHTGRAY);
            }

        EndDrawing();
    }

    // Cleanup
    FlatPlane_Destroy(plane);
    CloseWindow();

    return 0;
}
