#ifndef CHUNK_UTILS_H
#define CHUNK_UTILS_H

#include "chunk.h"
#include <stdbool.h>

// --- Chunk Map (Hash Map) ---
// Simple open addressing or chaining hash map to store active chunks by ID

typedef struct ChunkMapEntry {
    unsigned long long key;
    Chunk* value;
    struct ChunkMapEntry* next; // Chaining for collisions
} ChunkMapEntry;

typedef struct ChunkMap {
    ChunkMapEntry** buckets;
    int capacity;
    int count;
} ChunkMap;

ChunkMap* ChunkMap_Create(int capacity);
void ChunkMap_Insert(ChunkMap* map, unsigned long long key, Chunk* chunk);
Chunk* ChunkMap_Get(ChunkMap* map, unsigned long long key);
Chunk* ChunkMap_Remove(ChunkMap* map, unsigned long long key);
void ChunkMap_Clear(ChunkMap* map); // Does not free chunks, just clears map
void ChunkMap_Destroy(ChunkMap* map); // Frees map structure, not chunks

// --- Chunk Pool ---
// Pool to recycle chunks

typedef struct ChunkPool {
    Chunk** chunks;
    int capacity;
    int count;
} ChunkPool;

ChunkPool* ChunkPool_Create(int initialCapacity);
void ChunkPool_Release(ChunkPool* pool, Chunk* chunk);
Chunk* ChunkPool_Acquire(ChunkPool* pool);
void ChunkPool_Destroy(ChunkPool* pool); // Frees all pooled chunks

#endif // CHUNK_UTILS_H
