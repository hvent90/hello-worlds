#include "planet.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define DEFAULT_LOD_COMPARISON_VALUE 2.0f

void Planet_MakeChunkKey(int faceIndex, Vector3 position, float size, char* outKey) {
    snprintf(outKey, 64, "f%d_x%.2f_y%.2f_z%.2f_s%.2f",
             faceIndex, position.x, position.y, position.z, size);
}

Chunk* Planet_FindChunk(Planet* planet, const char* key) {
    for (int i = 0; i < planet->chunkCount; i++) {
        if (planet->chunkMap[i].active && strcmp(planet->chunkMap[i].key, key) == 0) {
            return planet->chunkMap[i].chunk;
        }
    }
    return NULL;
}

void Planet_AddChunk(Planet* planet, const char* key, Chunk* chunk) {
    if (planet->chunkCount >= MAX_CHUNKS) {
        printf("WARNING: Max chunks reached (%d)\n", MAX_CHUNKS);
        return;
    }

    for (int i = 0; i < MAX_CHUNKS; i++) {
        if (!planet->chunkMap[i].active) {
            strncpy(planet->chunkMap[i].key, key, 63);
            planet->chunkMap[i].key[63] = '\0';
            planet->chunkMap[i].chunk = chunk;
            planet->chunkMap[i].active = true;
            planet->chunkCount++;
            return;
        }
    }
}

void Planet_RemoveChunk(Planet* planet, const char* key) {
    for (int i = 0; i < MAX_CHUNKS; i++) {
        if (planet->chunkMap[i].active && strcmp(planet->chunkMap[i].key, key) == 0) {
            Chunk_Destroy(planet->chunkMap[i].chunk);
            planet->chunkMap[i].active = false;
            planet->chunkMap[i].chunk = NULL;
            planet->chunkCount--;
            return;
        }
    }
}

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution,
                      HeightGenerator heightGen, ColorGenerator colorGen, void* userData) {
    Planet* planet = (Planet*)malloc(sizeof(Planet));

    planet->radius = radius;
    planet->minCellSize = minCellSize;
    planet->minCellResolution = minCellResolution;
    planet->lodDistanceComparisonValue = DEFAULT_LOD_COMPARISON_VALUE;
    planet->inverted = false;
    planet->position = (Vector3){0, 0, 0};
    planet->chunkCount = 0;

    planet->heightGen = heightGen;
    planet->colorGen = colorGen;
    planet->userData = userData;

    // Initialize chunk map
    for (int i = 0; i < MAX_CHUNKS; i++) {
        planet->chunkMap[i].active = false;
        planet->chunkMap[i].chunk = NULL;
    }

    // Initialize cube face transforms (same as CubicQuadTree)
    planet->cubeFaceTransforms[0] = Matrix_Multiply(
        Matrix_CreateTranslation(0, radius, 0),
        Matrix_CreateRotationX(-PI / 2.0f)
    );
    planet->cubeFaceTransforms[1] = Matrix_Multiply(
        Matrix_CreateTranslation(0, -radius, 0),
        Matrix_CreateRotationX(PI / 2.0f)
    );
    planet->cubeFaceTransforms[2] = Matrix_Multiply(
        Matrix_CreateTranslation(radius, 0, 0),
        Matrix_CreateRotationY(PI / 2.0f)
    );
    planet->cubeFaceTransforms[3] = Matrix_Multiply(
        Matrix_CreateTranslation(-radius, 0, 0),
        Matrix_CreateRotationY(-PI / 2.0f)
    );
    planet->cubeFaceTransforms[4] = Matrix_CreateTranslation(0, 0, radius);
    planet->cubeFaceTransforms[5] = Matrix_Multiply(
        Matrix_CreateTranslation(0, 0, -radius),
        Matrix_CreateRotationY(PI)
    );

    // Load default shader
    planet->shader = LoadShaderFromMemory(NULL, NULL);

    return planet;
}

void Planet_Update(Planet* planet, Vector3 lodOrigin) {
    // Create cubic quadtree for this frame
    CubicQuadTree* quadtree = CubicQuadTree_Create(
        planet->radius,
        planet->minCellSize,
        planet->position,
        planet->lodDistanceComparisonValue
    );

    // Insert LOD origin to subdivide
    CubicQuadTree_Insert(quadtree, lodOrigin);

    // Get all cube face sides
    CubicQuadTreeSide* sides = NULL;
    CubicQuadTree_GetSides(quadtree, &sides);

    // Build new chunk map
    char newKeys[MAX_CHUNKS][64];
    int newKeyCount = 0;

    for (int faceIdx = 0; faceIdx < CUBE_FACES; faceIdx++) {
        QuadTreeNode** nodes = NULL;
        int nodeCount = 0;

        QuadTree_GetChildren(sides[faceIdx].quadtree, &nodes, &nodeCount);

        for (int i = 0; i < nodeCount; i++) {
            QuadTreeNode* node = nodes[i];
            Vector3 center = node->center;
            float size = node->size.x;

            char key[64];
            Planet_MakeChunkKey(faceIdx, center, size, key);

            if (newKeyCount < MAX_CHUNKS) {
                strncpy(newKeys[newKeyCount], key, 63);
                newKeys[newKeyCount][63] = '\0';
                newKeyCount++;
            }

            // Check if chunk already exists
            Chunk* existingChunk = Planet_FindChunk(planet, key);
            if (!existingChunk) {
                // Create new chunk
                ChunkProps props;
                props.offset = center;
                props.origin = planet->position;
                props.worldMatrix = planet->cubeFaceTransforms[faceIdx];
                props.width = size;
                props.height = size;
                props.radius = planet->radius;
                props.resolution = planet->minCellResolution;
                props.minCellSize = planet->minCellSize;
                props.inverted = planet->inverted;
                props.heightGen = planet->heightGen;
                props.colorGen = planet->colorGen;
                props.userData = planet->userData;

                Chunk* chunk = Chunk_Create(props);
                Chunk_GenerateMesh(chunk, props);
                Chunk_Show(chunk);

                Planet_AddChunk(planet, key, chunk);
            }
        }

        free(nodes);
    }

    // Remove old chunks that are no longer needed
    for (int i = 0; i < MAX_CHUNKS; i++) {
        if (!planet->chunkMap[i].active) continue;

        bool found = false;
        for (int j = 0; j < newKeyCount; j++) {
            if (strcmp(planet->chunkMap[i].key, newKeys[j]) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            char keyToRemove[64];
            strncpy(keyToRemove, planet->chunkMap[i].key, 64);
            Planet_RemoveChunk(planet, keyToRemove);
        }
    }

    CubicQuadTree_Destroy(quadtree);
}

void Planet_Render(Planet* planet) {
    for (int i = 0; i < MAX_CHUNKS; i++) {
        if (planet->chunkMap[i].active) {
            Chunk_Render(planet->chunkMap[i].chunk);
        }
    }
}

void Planet_Destroy(Planet* planet) {
    if (!planet) return;

    // Destroy all chunks
    for (int i = 0; i < MAX_CHUNKS; i++) {
        if (planet->chunkMap[i].active) {
            Chunk_Destroy(planet->chunkMap[i].chunk);
        }
    }

    UnloadShader(planet->shader);
    free(planet);
}
