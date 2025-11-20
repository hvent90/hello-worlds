# Implementation Plan: Bidirectional LOD with Ephemeral Quadtree

## Problem Statement

The current C planet renderer implementation has a critical issue: **LOD only increases, never decreases**. When the camera approaches terrain, the quadtree correctly splits nodes to increase detail. However, when the camera moves away, those high-detail nodes persist forever, causing unbounded memory growth and rendering overhead.

### Current Broken Behavior

In `src/quadtree.c:53-96`, the `InsertRecursive` function only handles splitting:

```c
static void InsertRecursive(QuadtreeNode* node, Vector3 cameraPos, ...) {
    float dist = Vector3Distance(node->sphereCenter, cameraPos);

    if (dist < node->size.x * comparatorValue && node->size.x > minNodeSize) {
        // ONLY SPLITS - never collapses
        if (node->isLeaf) {
            // Create 4 children...
        }
        // Recurse into children
    }
    // ⚠️ NO CODE TO COLLAPSE when camera moves away
}
```

### Why the TypeScript Version Works

The TypeScript implementation (`packages/planets/src/planet/Planet.ts:111-147`) uses an **ephemeral quadtree strategy**:

1. Every frame, build a fresh quadtree structure from scratch based on camera position
2. Compare the new structure to the previous frame's chunk map
3. Keep chunks that still exist, create new ones, destroy old ones
4. This automatically handles both splitting (new chunks appear) and collapsing (old chunks disappear)

## Solution Overview

We'll implement the ephemeral quadtree approach in two phases:

**Item 4: Separate Chunk Storage** - Replace quadtree node-stored chunks with a hash map
**Item 3: Ephemeral Quadtree Strategy** - Rebuild quadtree each frame and diff against chunk map

This solves the LOD collapse problem elegantly: the diff naturally removes chunks that are no longer in the new quadtree.

---

## Item 4: Separate Chunk Storage with Hash Map

### What We're Doing

Currently, chunks are stored directly in quadtree nodes via `QuadtreeNode->userData` (see `src/planet.c:42-54`). This couples chunk lifetime to quadtree node lifetime.

We need to:
1. Add a hash map library to the project
2. Create a chunk map data structure with string keys
3. Update `Planet` struct to store chunks in a hash map instead of quadtree nodes
4. Implement key generation for chunk identification
5. Implement dictionary operations (intersection, difference)

### What We're NOT Doing

- ❌ Writing our own hash map from scratch (use proven library)
- ❌ Changing chunk generation logic (Chunk_Create/Generate stays the same)
- ❌ Modifying quadtree splitting logic yet (that's Item 3)
- ❌ Adding chunk pooling (future optimization, not required for correctness)
- ❌ Changing rendering code (Chunk_Draw stays the same)

### Implementation Details

#### 4.1: Add uthash Library

**File**: `packages/planet-renderer-c/include/uthash.h` (new file)

Download uthash single-header library from https://github.com/troydhanson/uthash

```bash
cd packages/planet-renderer-c/include
curl -O https://raw.githubusercontent.com/troydhanson/uthash/master/src/uthash.h
```

**Why uthash?**
- Single header, no compilation needed
- Mature (20+ years), widely used
- Macro-based API designed for C structs
- Zero dependencies

#### 4.2: Create Chunk Map Data Structure

**File**: `packages/planet-renderer-c/include/chunk_map.h` (new file)

```c
#ifndef CHUNK_MAP_H
#define CHUNK_MAP_H

#include "chunk.h"
#include "uthash.h"

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
```

**Key Format**: Following TypeScript convention from `packages/planets/src/chunk/Chunk.helpers.ts:4-16`:
```
"x/y [size] [faceIndex]"
Example: "150.50/200.75 [100.00] [2]"
```

#### 4.3: Implement Chunk Map

**File**: `packages/planet-renderer-c/src/chunk_map.c` (new file)

```c
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
```

**Reference**: TypeScript utils in `packages/planets/src/utils/index.ts:3-19`

#### 4.4: Update Planet Structure

**File**: `packages/planet-renderer-c/include/planet.h`

**Current** (line 8-15):
```c
typedef struct Planet {
    CubicQuadTree* quadtree;
    float radius;
    float minCellSize;
    int minCellResolution;
    Vector3 origin;
} Planet;
```

**New**:
```c
#include "chunk_map.h"

typedef struct Planet {
    CubicQuadTree* quadtree;      // Now ephemeral, rebuilt each frame
    ChunkMap* chunkMap;            // NEW: Persistent chunk storage
    float radius;
    float minCellSize;
    int minCellResolution;
    Vector3 origin;
    float lodDistanceComparisonValue;  // NEW: Store comparator
} Planet;
```

#### 4.5: Update Planet_Create

**File**: `packages/planet-renderer-c/src/planet.c`

**Current** (line 13-28):
```c
Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin) {
    Planet* planet = (Planet*)malloc(sizeof(Planet));
    planet->radius = radius;
    planet->minCellSize = minCellSize;
    planet->minCellResolution = minCellResolution;
    planet->origin = origin;

    float comparatorValue = 1.5f;
    planet->quadtree = CubicQuadTree_Create(radius, minCellSize, comparatorValue, origin);

    return planet;
}
```

**New**:
```c
Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin) {
    Planet* planet = (Planet*)malloc(sizeof(Planet));
    planet->radius = radius;
    planet->minCellSize = minCellSize;
    planet->minCellResolution = minCellResolution;
    planet->origin = origin;
    planet->lodDistanceComparisonValue = 1.5f;

    // Create persistent chunk storage
    planet->chunkMap = ChunkMap_Create();

    // Quadtree will be created/destroyed each frame in Planet_Update
    planet->quadtree = NULL;

    return planet;
}
```

#### 4.6: Update CMakeLists.txt

**File**: `packages/planet-renderer-c/CMakeLists.txt`

Add new source files to the build:

```cmake
# Find existing source files section and add:
src/chunk_map.c
```

### Item 4 Testing

After Item 4, the code won't work yet (Planet_Update still uses old approach), but you should be able to:

```c
// Test chunk map operations
ChunkMap* map = ChunkMap_Create();

char key[128];
ChunkMap_MakeKey(key, (Vector3){10.0f, 20.0f, 0.0f}, 100.0f, 2);
// key should be: "10.00/20.00 [100.00] [2]"

Chunk* chunk = Chunk_Create(...);
ChunkMap_Put(map, key, chunk);

assert(ChunkMap_Contains(map, key));
assert(ChunkMap_Get(map, key) == chunk);

ChunkMap_Free(map, true);
```

---

## Item 3: Ephemeral Quadtree Strategy

### What We're Doing

Replace the persistent quadtree update logic with an ephemeral approach:

1. Each frame in `Planet_Update`, build a fresh quadtree from scratch
2. Extract all leaf nodes from the new quadtree as a "desired chunks" map
3. Diff against the previous frame's `planet->chunkMap`
4. Create new chunks, destroy old chunks, keep unchanged chunks
5. Destroy the ephemeral quadtree (keep only the chunk map)

### What We're NOT Doing

- ❌ Changing quadtree node structure or split logic (reuse existing code)
- ❌ Adding quadtree node pooling (optimization for later)
- ❌ Modifying chunk generation (still synchronous for now)
- ❌ Optimizing to only rebuild when camera moves significantly (add later if needed)
- ❌ Changing the rendering loop (Chunk_Draw stays the same)

### Implementation Details

#### 3.1: Track Face Index in Quadtree Nodes

**Problem**: When converting quadtree leaves to chunk map keys, we need to know which cube face (0-5) each node belongs to.

**File**: `packages/planet-renderer-c/include/quadtree.h`

**Current** (line 7-16):
```c
typedef struct QuadtreeNode {
    BoundingBox3 bounds;
    struct QuadtreeNode* children[4];
    Vector3 center;
    Vector3 sphereCenter;
    Vector3 size;
    bool isLeaf;
    void* userData;
    Matrix localToWorld;
} QuadtreeNode;
```

**New**:
```c
typedef struct QuadtreeNode {
    BoundingBox3 bounds;
    struct QuadtreeNode* children[4];
    Vector3 center;
    Vector3 sphereCenter;
    Vector3 size;
    bool isLeaf;
    void* userData;  // No longer used for chunks
    Matrix localToWorld;
    int faceIndex;   // NEW: Which cube face (0-5) this node belongs to
} QuadtreeNode;
```

**File**: `packages/planet-renderer-c/src/quadtree.c`

Update `CreateNode` (line 7-25) to accept and store face index:

```c
static QuadtreeNode* CreateNode(BoundingBox3 bounds, Matrix localToWorld,
                                 float planetRadius, Vector3 planetOrigin,
                                 int faceIndex) {
    QuadtreeNode* node = (QuadtreeNode*)malloc(sizeof(QuadtreeNode));
    node->bounds = bounds;
    for (int i = 0; i < 4; i++) node->children[i] = NULL;
    node->isLeaf = true;
    node->userData = NULL;
    node->localToWorld = localToWorld;
    node->faceIndex = faceIndex;  // NEW

    // ... rest of initialization ...
}
```

Update all `CreateNode` calls to pass face index through recursion.

**File**: `packages/planet-renderer-c/include/cubic_quadtree.h`

No changes to header needed.

**File**: `packages/planet-renderer-c/src/cubic_quadtree.c`

Update `CubicQuadTree_Create` (line 5-33) to pass face index when creating each face's quadtree:

```c
CubicQuadTree* CubicQuadTree_Create(float radius, float minNodeSize,
                                     float comparatorValue, Vector3 origin) {
    CubicQuadTree* tree = (CubicQuadTree*)malloc(sizeof(CubicQuadTree));
    Matrix transforms[6];

    // ... existing transform setup ...

    for (int i = 0; i < 6; i++) {
        tree->faces[i] = Quadtree_CreateWithFace(radius, minNodeSize, comparatorValue,
                                                   origin, transforms[i], i);  // Pass face index
    }

    return tree;
}
```

Add `Quadtree_CreateWithFace` variant to `src/quadtree.c`:

```c
Quadtree* Quadtree_CreateWithFace(float size, float minNodeSize, float comparatorValue,
                                   Vector3 origin, Matrix localToWorld, int faceIndex) {
    Quadtree* tree = (Quadtree*)malloc(sizeof(Quadtree));
    tree->size = size;
    tree->minNodeSize = minNodeSize;
    tree->comparatorValue = comparatorValue;
    tree->origin = origin;
    tree->localToWorld = localToWorld;
    tree->faceIndex = faceIndex;  // NEW: Store on tree

    BoundingBox3 bounds = {
        (Vector3){ -size, -size, 0 },
        (Vector3){ size, size, 0 }
    };

    tree->root = CreateNode(bounds, localToWorld, size, origin, faceIndex);
    return tree;
}
```

#### 3.2: Remove OnNodeSplit Callback System

Since chunks are no longer stored in nodes, we don't need the split callback anymore.

**File**: `packages/planet-renderer-c/src/planet.c`

**Delete** (line 5-11):
```c
static void OnNodeSplit(QuadtreeNode* node) {
    if (node->userData) {
        Chunk* chunk = (Chunk*)node->userData;
        Chunk_Free(chunk);
        node->userData = NULL;
    }
}
```

**File**: `packages/planet-renderer-c/include/quadtree.h`

**Delete** (line 27):
```c
typedef void (*QuadtreeSplitCallback)(QuadtreeNode* node);
```

Update function signature (line 30):
```c
// OLD:
void Quadtree_Insert(Quadtree* tree, Vector3 cameraPosition, QuadtreeSplitCallback onSplit);

// NEW:
void Quadtree_Insert(Quadtree* tree, Vector3 cameraPosition);
```

**File**: `packages/planet-renderer-c/src/quadtree.c`

Remove callback parameter from `InsertRecursive` (line 53) and `Quadtree_Insert` (line 98).

Remove callback invocation (line 60-62):
```c
// DELETE:
if (onSplit) {
    onSplit(node);
}
```

**File**: `packages/planet-renderer-c/src/cubic_quadtree.c`

Update `CubicQuadTree_Insert` (line 35-39):
```c
// OLD:
void CubicQuadTree_Insert(CubicQuadTree* tree, Vector3 cameraPosition,
                          QuadtreeSplitCallback onSplit) {
    for (int i = 0; i < 6; i++) {
        Quadtree_Insert(tree->faces[i], cameraPosition, onSplit);
    }
}

// NEW:
void CubicQuadTree_Insert(CubicQuadTree* tree, Vector3 cameraPosition) {
    for (int i = 0; i < 6; i++) {
        Quadtree_Insert(tree->faces[i], cameraPosition);
    }
}
```

#### 3.3: Implement Quadtree-to-ChunkMap Conversion

**File**: `packages/planet-renderer-c/src/planet.c`

Add helper function to convert quadtree leaf nodes into a chunk map of desired chunks:

```c
// Convert ephemeral quadtree into a map of desired chunk keys
// Note: This creates a map with NULL chunk pointers - just the keys we want
static ChunkMap* QuadtreeToDesiredChunks(CubicQuadTree* tree) {
    ChunkMap* desiredMap = ChunkMap_Create();

    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(tree, &leafNodes, &leafCount);

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];

        // Generate key for this node
        char key[128];
        ChunkMap_MakeKey(key, node->center, node->size.x, node->faceIndex);

        // Put NULL chunk (we just want the key for diffing)
        ChunkMap_Put(desiredMap, key, NULL);
    }

    free(leafNodes);
    return desiredMap;
}
```

#### 3.4: Rewrite Planet_Update with Ephemeral Strategy

**File**: `packages/planet-renderer-c/src/planet.c`

**Current** (line 30-59):
```c
void Planet_Update(Planet* planet, Vector3 cameraPosition) {
    // Insert/Update Quadtree
    CubicQuadTree_Insert(planet->quadtree, cameraPosition, OnNodeSplit);

    // Get all leaf nodes
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);

    // Create chunks for new leaves
    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (!node->userData) {
            Chunk* chunk = Chunk_Create(...);
            Chunk_Generate(chunk);
            node->userData = chunk;
        }
    }

    free(leafNodes);
}
```

**New** (complete replacement, reference `packages/planets/src/planet/Planet.ts:102-208`):
```c
void Planet_Update(Planet* planet, Vector3 cameraPosition) {
    // 1. Build ephemeral quadtree from scratch based on camera position
    CubicQuadTree* newTree = CubicQuadTree_Create(
        planet->radius,
        planet->minCellSize,
        planet->lodDistanceComparisonValue,
        planet->origin
    );

    // Subdivide quadtree based on camera position
    CubicQuadTree_Insert(newTree, cameraPosition);

    // 2. Convert quadtree leaves to desired chunks map
    ChunkMap* desiredChunks = QuadtreeToDesiredChunks(newTree);

    // 3. Diff against current chunks
    // Keep: chunks that exist in both current and desired
    // Create: chunks in desired but not in current
    // Destroy: chunks in current but not in desired

    ChunkMap* toKeep = ChunkMap_Intersection(planet->chunkMap, desiredChunks);
    ChunkMap* toCreate = ChunkMap_Difference(desiredChunks, planet->chunkMap);
    ChunkMap* toDestroy = ChunkMap_Difference(planet->chunkMap, desiredChunks);

    // 4. Destroy old chunks (camera moved away)
    ChunkMapEntry *entry, *tmp;
    HASH_ITER(hh, toDestroy->entries, entry, tmp) {
        if (entry->chunk) {
            Chunk_Free(entry->chunk);
        }
        ChunkMap_Remove(planet->chunkMap, entry->key, false);  // Already freed
    }

    // 5. Create new chunks (camera moved closer)
    HASH_ITER(hh, toCreate->entries, entry, tmp) {
        // Extract parameters from key and quadtree node
        // We need to find the corresponding quadtree node to get bounds/transform

        QuadtreeNode** leafNodes;
        int leafCount;
        CubicQuadTree_GetLeafNodes(newTree, &leafNodes, &leafCount);

        for (int i = 0; i < leafCount; i++) {
            QuadtreeNode* node = leafNodes[i];
            char nodeKey[128];
            ChunkMap_MakeKey(nodeKey, node->center, node->size.x, node->faceIndex);

            if (strcmp(nodeKey, entry->key) == 0) {
                // Found matching node - create chunk
                Chunk* chunk = Chunk_Create(
                    node->bounds.min,
                    node->size.x,
                    node->size.y,
                    planet->radius,
                    planet->minCellResolution,
                    planet->origin,
                    node->localToWorld
                );
                Chunk_Generate(chunk);
                ChunkMap_Put(planet->chunkMap, entry->key, chunk);
                break;
            }
        }

        free(leafNodes);
    }

    // 6. Update internal chunk map (toKeep already has the right chunks)
    // planet->chunkMap already updated via Remove/Put above

    // 7. Cleanup temporary maps and ephemeral quadtree
    ChunkMap_Free(toKeep, false);      // Don't free chunks (still in planet->chunkMap)
    ChunkMap_Free(toCreate, false);    // Don't free chunks (just created)
    ChunkMap_Free(toDestroy, false);   // Don't free chunks (already freed above)
    ChunkMap_Free(desiredChunks, false);  // No chunks to free (all NULL)
    CubicQuadTree_Free(newTree);       // Free ephemeral quadtree structure
}
```

**Optimization Note**: The nested loop for finding nodes is O(n²) but acceptable for now. In the future, we could build a temporary node map during `QuadtreeToDesiredChunks` to make this O(n).

#### 3.5: Update Planet_Draw

**File**: `packages/planet-renderer-c/src/planet.c`

**Current** (line 61-78):
```c
void Planet_Draw(Planet* planet) {
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            Chunk* chunk = (Chunk*)node->userData;
            Chunk_Draw(chunk);
        }
    }

    free(leafNodes);
}
```

**New** (draw from chunk map instead of quadtree):
```c
void Planet_Draw(Planet* planet) {
    ChunkMapEntry *entry, *tmp;
    HASH_ITER(hh, planet->chunkMap->entries, entry, tmp) {
        if (entry->chunk) {
            Chunk_Draw(entry->chunk);
        }
    }
}
```

#### 3.6: Update Planet_Free

**File**: `packages/planet-renderer-c/src/planet.c`

**Current** (line 80-97):
```c
void Planet_Free(Planet* planet) {
    // Free all chunks from quadtree nodes
    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            Chunk_Free((Chunk*)node->userData);
            node->userData = NULL;
        }
    }
    free(leafNodes);

    CubicQuadTree_Free(planet->quadtree);
    free(planet);
}
```

**New** (free chunks from map):
```c
void Planet_Free(Planet* planet) {
    if (!planet) return;

    // Free all chunks from chunk map
    ChunkMap_Free(planet->chunkMap, true);  // true = also free chunks

    // Free ephemeral quadtree if it exists (shouldn't, but be safe)
    if (planet->quadtree) {
        CubicQuadTree_Free(planet->quadtree);
    }

    free(planet);
}
```

### Item 3 Testing

After Item 3, test bidirectional LOD:

```c
Planet* planet = Planet_Create(1000.0f, 10.0f, 64, (Vector3){0, 0, 0});

// Camera close - should create high detail chunks
Vector3 closePos = {0, 0, 1050};  // Just above surface
Planet_Update(planet, closePos);
int closeCount = planet->chunkMap->count;
printf("Close chunks: %d\n", closeCount);

// Camera far - should collapse to low detail
Vector3 farPos = {0, 0, 5000};  // Far away
Planet_Update(planet, farPos);
int farCount = planet->chunkMap->count;
printf("Far chunks: %d\n", farCount);

// Should have many more chunks when close
assert(closeCount > farCount * 4);

Planet_Free(planet);
```

---

## Performance Considerations

### Expected Costs

**Ephemeral quadtree allocation** (every frame):
- For a typical planet with ~500 visible leaf nodes across 6 faces = ~700 total nodes (including intermediate)
- Each node: ~80 bytes
- Total: ~56KB per frame at 60fps = ~3.3MB/s allocation/deallocation

This is acceptable for modern systems but watch for:
- Allocator fragmentation (mitigate: use arena allocator for quadtree nodes)
- Cache misses during tree building

**Hash map operations**:
- uthash is very efficient (typically 10-20ns per lookup on modern CPUs)
- Diffing operations are O(n) where n = chunk count (typically 100-1000)
- Should be <0.1ms per frame

### Future Optimizations (NOT in this plan)

1. **Quadtree node pooling**: Keep a pool of pre-allocated nodes to reduce malloc/free overhead
2. **Cached quadtree**: Only rebuild when camera moves >threshold distance
3. **Incremental updates**: Track which cube faces changed and only rebuild those
4. **Chunk pooling**: Reuse chunk memory instead of free/malloc (Item 1 from original analysis)
5. **Spatial hashing**: Replace nested loop in chunk creation with temporary node map

---

## Testing Strategy

### Unit Tests

Create `tests/test_chunk_map.c`:
```c
void test_chunk_map_basic() {
    ChunkMap* map = ChunkMap_Create();

    // Test put/get
    Chunk dummy = {0};
    ChunkMap_Put(map, "test_key", &dummy);
    assert(ChunkMap_Get(map, "test_key") == &dummy);
    assert(ChunkMap_Contains(map, "test_key"));

    // Test remove
    ChunkMap_Remove(map, "test_key", false);
    assert(!ChunkMap_Contains(map, "test_key"));

    ChunkMap_Free(map, false);
}

void test_chunk_map_intersection() {
    ChunkMap* a = ChunkMap_Create();
    ChunkMap* b = ChunkMap_Create();

    Chunk dummy1 = {0}, dummy2 = {0}, dummy3 = {0};

    ChunkMap_Put(a, "key1", &dummy1);
    ChunkMap_Put(a, "key2", &dummy2);
    ChunkMap_Put(b, "key2", &dummy2);
    ChunkMap_Put(b, "key3", &dummy3);

    ChunkMap* intersection = ChunkMap_Intersection(a, b);
    assert(intersection->count == 1);
    assert(ChunkMap_Contains(intersection, "key2"));

    ChunkMap_Free(a, false);
    ChunkMap_Free(b, false);
    ChunkMap_Free(intersection, false);
}
```

### Integration Tests

In `examples/simple_planet.c`, add camera movement:

```c
Vector3 cameraPos = {0, 0, 2000};
float t = 0;

while (!WindowShouldClose()) {
    // Animate camera moving in/out
    t += GetFrameTime();
    cameraPos.z = 1500 + 1000 * sinf(t * 0.5f);

    Planet_Update(planet, cameraPos);

    BeginDrawing();
    ClearBackground(BLACK);
    DrawText(TextFormat("Chunks: %d", planet->chunkMap->count), 10, 10, 20, WHITE);
    DrawText(TextFormat("Camera Z: %.1f", cameraPos.z), 10, 40, 20, WHITE);
    Planet_Draw(planet);
    EndDrawing();
}
```

Expected behavior:
- Chunk count increases as camera approaches
- Chunk count decreases as camera moves away
- No memory leaks (run with valgrind)

---

## Implementation Checklist

### Item 4: Separate Chunk Storage
- [ ] Download and add `include/uthash.h`
- [ ] Create `include/chunk_map.h` with ChunkMap types and API
- [ ] Implement `src/chunk_map.c` with all operations
- [ ] Update `include/planet.h` to add `ChunkMap* chunkMap` field
- [ ] Update `src/planet.c:Planet_Create` to initialize chunk map
- [ ] Update `CMakeLists.txt` to include new source files
- [ ] Write unit tests for chunk map operations
- [ ] Verify tests pass

### Item 3: Ephemeral Quadtree
- [ ] Add `int faceIndex` to `QuadtreeNode` struct
- [ ] Update `CreateNode` to accept and store face index
- [ ] Add `Quadtree_CreateWithFace` function
- [ ] Update `CubicQuadTree_Create` to pass face indices
- [ ] Remove `QuadtreeSplitCallback` typedef and all callback parameters
- [ ] Implement `QuadtreeToDesiredChunks` helper function
- [ ] Rewrite `Planet_Update` with ephemeral strategy
- [ ] Update `Planet_Draw` to iterate chunk map instead of quadtree
- [ ] Update `Planet_Free` to free chunk map
- [ ] Test bidirectional LOD (camera in/out)
- [ ] Run valgrind to check for memory leaks
- [ ] Test with example programs

### Documentation
- [ ] Update README with new architecture explanation
- [ ] Add comments explaining ephemeral quadtree concept
- [ ] Document performance characteristics

---

## File Summary

### New Files
- `include/uthash.h` - Hash map library (download)
- `include/chunk_map.h` - Chunk map API
- `src/chunk_map.c` - Chunk map implementation
- `tests/test_chunk_map.c` - Unit tests

### Modified Files
- `include/planet.h` - Add chunkMap field, lodDistanceComparisonValue
- `include/quadtree.h` - Add faceIndex to QuadtreeNode, remove callback typedef
- `include/cubic_quadtree.h` - Update function signatures
- `src/planet.c` - Complete rewrite of Planet_Update, Planet_Draw, Planet_Free
- `src/quadtree.c` - Remove callback parameters, add faceIndex tracking
- `src/cubic_quadtree.c` - Remove callback parameters, pass faceIndices
- `CMakeLists.txt` - Add chunk_map.c to build

### Unchanged Files
- `include/chunk.h` - No changes to chunk interface
- `src/chunk.c` - No changes to chunk implementation
- All math utilities - No changes needed
- Example programs - May need minor updates to test new behavior

---

## References

### TypeScript Implementation
- **Ephemeral quadtree**: `packages/planets/src/planet/Planet.ts:111-147`
- **Chunk map diffing**: `packages/planets/src/planet/Planet.ts:149-156`
- **Key generation**: `packages/planets/src/chunk/Chunk.helpers.ts:4-16`
- **Dictionary operations**: `packages/planets/src/utils/index.ts:3-19`
- **CubicQuadTree**: `packages/planets/src/quadtree/CubicQuadTree.ts`
- **Quadtree insert**: `packages/planets/src/quadtree/Quadtree.ts:68-85`

### C Implementation (Current)
- **Planet structure**: `include/planet.h:8-15`
- **Planet update (broken)**: `src/planet.c:30-59`
- **Quadtree insert**: `src/quadtree.c:53-96`
- **Cubic quadtree**: `src/cubic_quadtree.c`

---

## Questions / Future Work

### Not Addressed in This Plan
- **Async chunk generation**: Still synchronous (blocks in Planet_Update)
- **Chunk pooling**: Memory optimization for later
- **Event system**: No ChunkGenerated/ChunkDestroyed events yet
- **Worker threads**: Single-threaded for simplicity
- **Scene graph integration**: No Three.js-style object hierarchy

These are all valid optimizations but not required for correctness. The bidirectional LOD issue will be fully solved by this plan.
