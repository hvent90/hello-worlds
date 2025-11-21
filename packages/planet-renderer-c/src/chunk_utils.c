#include "chunk_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// --- Chunk Map ---

ChunkMap* ChunkMap_Create(int capacity) {
    ChunkMap* map = (ChunkMap*)malloc(sizeof(ChunkMap));
    map->capacity = capacity;
    map->count = 0;
    map->buckets = (ChunkMapEntry**)calloc(capacity, sizeof(ChunkMapEntry*));
    return map;
}

static int Hash(unsigned long long key, int capacity) {
    return key % capacity;
}

void ChunkMap_Insert(ChunkMap* map, unsigned long long key, Chunk* chunk) {
    int index = Hash(key, map->capacity);
    
    ChunkMapEntry* entry = map->buckets[index];
    while (entry) {
        if (entry->key == key) {
            // Replace existing
            entry->value = chunk;
            return;
        }
        entry = entry->next;
    }
    
    // New entry
    ChunkMapEntry* newEntry = (ChunkMapEntry*)malloc(sizeof(ChunkMapEntry));
    newEntry->key = key;
    newEntry->value = chunk;
    newEntry->next = map->buckets[index];
    map->buckets[index] = newEntry;
    map->count++;
}

Chunk* ChunkMap_Get(ChunkMap* map, unsigned long long key) {
    int index = Hash(key, map->capacity);
    ChunkMapEntry* entry = map->buckets[index];
    while (entry) {
        if (entry->key == key) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

Chunk* ChunkMap_Remove(ChunkMap* map, unsigned long long key) {
    int index = Hash(key, map->capacity);
    ChunkMapEntry* entry = map->buckets[index];
    ChunkMapEntry* prev = NULL;
    
    while (entry) {
        if (entry->key == key) {
            if (prev) {
                prev->next = entry->next;
            } else {
                map->buckets[index] = entry->next;
            }
            Chunk* chunk = entry->value;
            free(entry);
            map->count--;
            return chunk;
        }
        prev = entry;
        entry = entry->next;
    }
    return NULL;
}

void ChunkMap_Clear(ChunkMap* map) {
    for (int i = 0; i < map->capacity; i++) {
        ChunkMapEntry* entry = map->buckets[i];
        while (entry) {
            ChunkMapEntry* next = entry->next;
            free(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->count = 0;
}

void ChunkMap_Destroy(ChunkMap* map) {
    ChunkMap_Clear(map);
    free(map->buckets);
    free(map);
}

// --- Chunk Pool ---

ChunkPool* ChunkPool_Create(int initialCapacity) {
    ChunkPool* pool = (ChunkPool*)malloc(sizeof(ChunkPool));
    pool->capacity = initialCapacity;
    pool->count = 0;
    pool->chunks = (Chunk**)malloc(sizeof(Chunk*) * initialCapacity);
    return pool;
}

void ChunkPool_Release(ChunkPool* pool, Chunk* chunk) {
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        pool->chunks = (Chunk**)realloc(pool->chunks, sizeof(Chunk*) * pool->capacity);
    }
    pool->chunks[pool->count++] = chunk;
}

Chunk* ChunkPool_Acquire(ChunkPool* pool) {
    if (pool->count > 0) {
        return pool->chunks[--pool->count];
    }
    return NULL;
}

void ChunkPool_Destroy(ChunkPool* pool) {
    for (int i = 0; i < pool->count; i++) {
        Chunk_Free(pool->chunks[i]);
    }
    free(pool->chunks);
    free(pool);
}
