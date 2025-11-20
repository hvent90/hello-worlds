#include "chunk_map.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

ChunkMap* ChunkMap_Create(void) {
    ChunkMap* map = (ChunkMap*)malloc(sizeof(ChunkMap));
    map->entries = NULL;  // uthash requires NULL initialization
    map->count = 0;
    return map;
}

void ChunkMap_Free(ChunkMap* map, bool freeChunks) {
    if (!map) return;

    ChunkMapEntry *entry, *tmp;
    HASH_ITER(hh, map->entries, entry, tmp) {
        HASH_DEL(map->entries, entry);
        if (freeChunks && entry->chunk) {
            Chunk_Free(entry->chunk);
        }
        free(entry);
    }
    free(map);
}

void ChunkMap_MakeKey(char* outKey, Vector3 position, float size, int faceIndex) {
    // Format: "x/y [size] [faceIndex]"
    snprintf(outKey, 128, "%.2f/%.2f [%.2f] [%d]",
             position.x, position.y, size, faceIndex);
}

void ChunkMap_Put(ChunkMap* map, const char* key, Chunk* chunk) {
    ChunkMapEntry* entry = NULL;
    HASH_FIND_STR(map->entries, key, entry);

    if (entry) {
        // Replace existing
        entry->chunk = chunk;
    } else {
        // Add new
        entry = (ChunkMapEntry*)malloc(sizeof(ChunkMapEntry));
        strncpy(entry->key, key, 128);
        entry->chunk = chunk;
        HASH_ADD_STR(map->entries, key, entry);
        map->count++;
    }
}

Chunk* ChunkMap_Get(ChunkMap* map, const char* key) {
    ChunkMapEntry* entry = NULL;
    HASH_FIND_STR(map->entries, key, entry);
    return entry ? entry->chunk : NULL;
}

bool ChunkMap_Contains(ChunkMap* map, const char* key) {
    ChunkMapEntry* entry = NULL;
    HASH_FIND_STR(map->entries, key, entry);
    return entry != NULL;
}

void ChunkMap_Remove(ChunkMap* map, const char* key, bool freeChunk) {
    ChunkMapEntry* entry = NULL;
    HASH_FIND_STR(map->entries, key, entry);

    if (entry) {
        HASH_DEL(map->entries, entry);
        if (freeChunk && entry->chunk) {
            Chunk_Free(entry->chunk);
        }
        free(entry);
        map->count--;
    }
}

ChunkMap* ChunkMap_Intersection(ChunkMap* a, ChunkMap* b) {
    // Return new map with keys that exist in BOTH a and b
    // Takes chunks from map a
    ChunkMap* result = ChunkMap_Create();

    ChunkMapEntry *entry, *tmp;
    HASH_ITER(hh, a->entries, entry, tmp) {
        if (ChunkMap_Contains(b, entry->key)) {
            ChunkMap_Put(result, entry->key, entry->chunk);
        }
    }

    return result;
}

ChunkMap* ChunkMap_Difference(ChunkMap* a, ChunkMap* b) {
    // Return new map with keys in A but NOT in B
    // Takes chunks from map a
    ChunkMap* result = ChunkMap_Create();

    ChunkMapEntry *entry, *tmp;
    HASH_ITER(hh, a->entries, entry, tmp) {
        if (!ChunkMap_Contains(b, entry->key)) {
            ChunkMap_Put(result, entry->key, entry->chunk);
        }
    }

    return result;
}
