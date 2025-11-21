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
} Chunk;

Chunk* Chunk_Create(Vector3 offset, float width, float height, float radius, int resolution, Vector3 origin, Matrix localToWorld);
void Chunk_Generate(Chunk* chunk);
void Chunk_Draw(Chunk* chunk, Color surfaceColor, Color wireframeColor);
void Chunk_Free(Chunk* chunk);

#endif // CHUNK_H
