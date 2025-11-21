#ifndef CHUNK_H
#define CHUNK_H

#include <raylib.h>

typedef struct Chunk {
    Mesh mesh;
    Model model;
    Vector3 offset;
    float width;
    float height;
    float radius;
    int resolution;
    Matrix localToWorld;
    Vector3 origin;
    unsigned long long id; // Unique ID matching QuadtreeNode
    bool isUploaded; // Track if VRAM is allocated
    float terrainFrequency;
    float terrainAmplitude;
} Chunk;

Chunk* Chunk_Create(Vector3 offset, float width, float height, float radius, int resolution, Vector3 origin, Matrix localToWorld, float terrainFrequency, float terrainAmplitude);
void Chunk_Generate(Chunk* chunk);
void Chunk_Draw(Chunk* chunk, Color surfaceColor, Color wireframeColor, Shader lightingShader);
void Chunk_DrawWithShadow(Chunk* chunk, Color surfaceColor, Color wireframeColor, Shader lightingShader, Texture2D shadowMap);
void Chunk_Free(Chunk* chunk);

#endif // CHUNK_H
