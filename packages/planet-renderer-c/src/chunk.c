#include "chunk.h"
#include <stdlib.h>
#include <raymath.h>
#include <stdio.h>

Chunk* Chunk_Create(Vector3 offset, float width, float height, float radius, int resolution, Vector3 origin, Matrix localToWorld) {
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    chunk->offset = offset;
    chunk->width = width;
    chunk->height = height;
    chunk->radius = radius;
    chunk->resolution = resolution;
    chunk->origin = origin;
    chunk->localToWorld = localToWorld;
    
    // Initialize mesh to zero
    chunk->mesh = (Mesh){ 0 };
    chunk->model = (Model){ 0 };
    
    return chunk;
}

void Chunk_Generate(Chunk* chunk) {
    int res = chunk->resolution;
    int numVertices = (res + 1) * (res + 1);
    int numTriangles = res * res * 2;
    
    chunk->mesh.vertexCount = numVertices;
    chunk->mesh.triangleCount = numTriangles;
    
    chunk->mesh.vertices = (float*)malloc(chunk->mesh.vertexCount * 3 * sizeof(float));
    chunk->mesh.normals = (float*)malloc(chunk->mesh.vertexCount * 3 * sizeof(float));
    chunk->mesh.texcoords = (float*)malloc(chunk->mesh.vertexCount * 2 * sizeof(float));
    chunk->mesh.indices = (unsigned short*)malloc(chunk->mesh.triangleCount * 3 * sizeof(unsigned short));
    
    int vIndex = 0;
    int tIndex = 0;
    
    for (int y = 0; y <= res; y++) {
        for (int x = 0; x <= res; x++) {
            float u = (float)x / res;
            float v = (float)y / res;
            
            // Calculate position on the cube face
            // offset is the bottom-left corner of this chunk on the face (in local 2D space of the face)
            // width/height is the size of this chunk
            float px = chunk->offset.x + u * chunk->width;
            float py = chunk->offset.y + v * chunk->height;
            
            // Local position on the face plane (z=0)
            Vector3 localPos = { px, py, 0 };
            
            // Transform to world space (cube face in 3D)
            Vector3 worldPos = Vector3Transform(localPos, chunk->localToWorld);
            
            // Normalize to project onto sphere
            Vector3 normalized = Vector3Normalize(worldPos);
            
            // Scale by radius and add origin
            Vector3 finalPos = Vector3Add(Vector3Scale(normalized, chunk->radius), chunk->origin);
            
            chunk->mesh.vertices[vIndex * 3] = finalPos.x;
            chunk->mesh.vertices[vIndex * 3 + 1] = finalPos.y;
            chunk->mesh.vertices[vIndex * 3 + 2] = finalPos.z;
            
            // Normal is just the normalized vector (for a perfect sphere)
            chunk->mesh.normals[vIndex * 3] = normalized.x;
            chunk->mesh.normals[vIndex * 3 + 1] = normalized.y;
            chunk->mesh.normals[vIndex * 3 + 2] = normalized.z;
            
            chunk->mesh.texcoords[vIndex * 2] = u;
            chunk->mesh.texcoords[vIndex * 2 + 1] = v;
            
            // Indices
            if (x < res && y < res) {
                int topLeft = y * (res + 1) + x;
                int topRight = topLeft + 1;
                int bottomLeft = (y + 1) * (res + 1) + x;
                int bottomRight = bottomLeft + 1;
                
                chunk->mesh.indices[tIndex * 3] = topLeft;
                chunk->mesh.indices[tIndex * 3 + 1] = bottomLeft;
                chunk->mesh.indices[tIndex * 3 + 2] = topRight;
                
                chunk->mesh.indices[tIndex * 3 + 3] = topRight;
                chunk->mesh.indices[tIndex * 3 + 4] = bottomLeft;
                chunk->mesh.indices[tIndex * 3 + 5] = bottomRight;
                
                tIndex += 2;
            }
            
            vIndex++;
        }
    }
    
    UploadMesh(&chunk->mesh, false);
    chunk->model = LoadModelFromMesh(chunk->mesh);
}

void Chunk_Draw(Chunk* chunk) {
    // DrawModel(chunk->model, (Vector3){0,0,0}, 1.0f, WHITE);
    // Since vertices are already in world space (relative to origin), we draw at 0,0,0?
    // Wait, "finalPos = Vector3Add(Vector3Scale(normalized, chunk->radius), chunk->origin);"
    // Yes, vertices are absolute world coordinates.
    // So we draw at 0,0,0 identity.
    
    // However, Raylib's DrawModel applies a transform.
    // If we pass (Vector3){0,0,0}, it translates by 0.
    // So it should be fine.
    
    DrawModel(chunk->model, (Vector3){0,0,0}, 1.0f, WHITE);
    DrawModelWires(chunk->model, (Vector3){0,0,0}, 1.0f, BLACK); // Wireframe for debugging
}

void Chunk_Free(Chunk* chunk) {
    UnloadModel(chunk->model); // Unloads mesh too
    free(chunk);
}
