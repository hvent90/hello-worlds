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
    chunk->isUploaded = false;
    chunk->id = 0;
    
    // Initialize mesh to zero
    chunk->mesh = (Mesh){ 0 };
    chunk->model = (Model){ 0 };
    
    return chunk;
}

void Chunk_Generate(Chunk* chunk) {
    int res = chunk->resolution;
    int numVertices = (res + 1) * (res + 1);
    int numTriangles = res * res * 2;
    
    // Check if we need to reallocate
    if (chunk->mesh.vertexCount != numVertices) {
        // Free old if exists (Raylib UnloadMesh frees VRAM, but we also have CPU pointers)
        // If we are reusing a chunk, we might have old data.
        // For simplicity in this phase, let's assume if vertexCount matches, we reuse.
        
        if (chunk->mesh.vertices) free(chunk->mesh.vertices);
        if (chunk->mesh.normals) free(chunk->mesh.normals);
        if (chunk->mesh.texcoords) free(chunk->mesh.texcoords);
        if (chunk->mesh.indices) free(chunk->mesh.indices);
        
        chunk->mesh.vertexCount = numVertices;
        chunk->mesh.triangleCount = numTriangles;
        
        chunk->mesh.vertices = (float*)malloc(chunk->mesh.vertexCount * 3 * sizeof(float));
        chunk->mesh.normals = (float*)malloc(chunk->mesh.vertexCount * 3 * sizeof(float));
        chunk->mesh.texcoords = (float*)malloc(chunk->mesh.vertexCount * 2 * sizeof(float));
        chunk->mesh.indices = (unsigned short*)malloc(chunk->mesh.triangleCount * 3 * sizeof(unsigned short));
    }
    
    int vIndex = 0;
    int tIndex = 0;
    
    for (int y = 0; y <= res; y++) {
        for (int x = 0; x <= res; x++) {
            float u = (float)x / res;
            float v = (float)y / res;
            
            // Calculate position on the cube face
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
                chunk->mesh.indices[tIndex * 3 + 1] = topRight;
                chunk->mesh.indices[tIndex * 3 + 2] = bottomLeft;
                
                chunk->mesh.indices[tIndex * 3 + 3] = topRight;
                chunk->mesh.indices[tIndex * 3 + 4] = bottomRight;
                chunk->mesh.indices[tIndex * 3 + 5] = bottomLeft;
                
                tIndex += 2;
            }
            
            vIndex++;
        }
    }
    
    // Upload to GPU
    if (chunk->isUploaded) {
        // Prevent Raylib from freeing our CPU buffers which we want to reuse
        // LoadModelFromMesh creates a copy of the mesh struct in model.meshes[0]
        if (chunk->model.meshes) {
            chunk->model.meshes[0].vertices = NULL;
            chunk->model.meshes[0].normals = NULL;
            chunk->model.meshes[0].texcoords = NULL;
            chunk->model.meshes[0].indices = NULL;
        }
        
        UnloadModel(chunk->model); 
        
        // Reset IDs so UploadMesh creates new VAO/VBOs
        chunk->mesh.vaoId = 0;
        chunk->mesh.vboId = 0;
    }
    
    UploadMesh(&chunk->mesh, false);
    chunk->model = LoadModelFromMesh(chunk->mesh);
    chunk->isUploaded = true;
}

void Chunk_Draw(Chunk* chunk, Color surfaceColor, Color wireframeColor) {
    if (chunk->isUploaded) {
        DrawModel(chunk->model, (Vector3){0,0,0}, 1.0f, surfaceColor);
        DrawModelWires(chunk->model, (Vector3){0,0,0}, 1.0f, wireframeColor); // Wireframe for debugging
    }
}

void Chunk_Free(Chunk* chunk) {
    if (chunk->isUploaded) {
        UnloadModel(chunk->model); // Unloads GPU data
    }
    
    // Free CPU data
    if (chunk->mesh.vertices) free(chunk->mesh.vertices);
    if (chunk->mesh.normals) free(chunk->mesh.normals);
    if (chunk->mesh.texcoords) free(chunk->mesh.texcoords);
    if (chunk->mesh.indices) free(chunk->mesh.indices);
    
    free(chunk);
}
