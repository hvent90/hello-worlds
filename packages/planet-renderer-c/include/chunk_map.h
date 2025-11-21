#ifndef CHUNK_MAP_H
#define CHUNK_MAP_H

#include "chunk.h"
#include "uthash.h"
#include <stdbool.h>

// Chunk map entry - wraps a chunk with hash map metadata
typedef struct ChunkMapEntry {
    char key[128];           // Hash key: "x/y [size] [faceIndex]"
    Chunk* chunk;            // The actual chunk
    UT_hash_handle hh;       // uthash handle (required)
} ChunkMapEntry;

// Chunk map - hash map of chunk entries
typedef struct ChunkMap {
    ChunkMapEntry* entries;  // Hash map head (uthash convention)
    int count;               // Number of chunks
} ChunkMap;

// Create/destroy
ChunkMap* ChunkMap_Create(void);
void ChunkMap_Free(ChunkMap* map, bool freeChunks);

// Operations
void ChunkMap_Put(ChunkMap* map, const char* key, Chunk* chunk);
Chunk* ChunkMap_Get(ChunkMap* map, const char* key);
bool ChunkMap_Contains(ChunkMap* map, const char* key);
void ChunkMap_Remove(ChunkMap* map, const char* key, bool freeChunk);

// Dictionary operations (for diffing)
ChunkMap* ChunkMap_Intersection(ChunkMap* a, ChunkMap* b);
ChunkMap* ChunkMap_Difference(ChunkMap* a, ChunkMap* b);

// Utility
void ChunkMap_MakeKey(char* outKey, Vector3 position, float size, int faceIndex);

#endif // CHUNK_MAP_H
