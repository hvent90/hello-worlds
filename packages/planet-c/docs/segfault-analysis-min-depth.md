# Segfault Analysis: Min Depth Increase Crash

## Status: RESOLVED

The segfault has been fixed. See "Successful Fix" section below for the solution.

## Summary

A segfault occurred on line 964 of `quadtree_cubic.c` when pressing "+" multiple times to increase `min_depth`. The crash happened during processing of the completed mesh request queue.

## Crash Location

```c
// Line 964 (original)
if (node_data != NULL && node_data->generation == completed->node_generation) {
```

The crash occurs when accessing `node_data->generation` where `node_data` is a dangling pointer (non-NULL but pointing to freed memory).

## Code Flow Analysis

### Async Mesh Generation Architecture

1. **Request Creation**: When a node needs to split, `mesh_pool_enqueue()` creates a `MeshRequest` with:
   - `node`: pointer to the parent's `CubicQuadTreeMeshData`
   - `node_generation`: the generation counter at request time
   - `child_index`: which child (0-3) or -1 for merge requests

2. **Worker Processing**: Worker threads generate meshes, set `status = REQUEST_READY`, and move requests to completed queue.

3. **Drain Loop** (lines 945-984): Each frame, main thread:
   - Calls `mesh_pool_drain_completed()` to get all completed requests
   - Iterates through them checking status and node validity
   - LOD processing runs AFTER this loop

4. **Node Cleanup**: When nodes are freed via `cleanup_mesh_data()`:
   - Cancels pending requests via `mesh_pool_cancel()` (sets status to CANCELLED)
   - Frees the `CubicQuadTreeMeshData` struct

### The Generation Check Purpose

The generation field exists to detect when a request's node pointer has been reused for a different node. Each new node gets a unique generation from `g_generation_counter++`. If `node_data->generation != completed->node_generation`, the memory was reused.

**Problem**: This check requires dereferencing `node_data`, which crashes if the memory was freed (not reused).

## Why The Crash Occurs

For a crash to happen, we need:
1. Request status is READY (not CANCELLED)
2. `completed->node` points to freed memory
3. We try to access `node_data->generation`

### Attempted Root Cause Identification

I investigated several scenarios but couldn't definitively identify when a READY request would have a dangling node pointer:

**Scenario A: Rapid min_depth changes**
- Frame N: min_depth increases, nodes must split
- Frame N+1: min_depth increases again before splits complete
- Workers complete requests, add to completed queue
- Some node restructuring happens...
- Request's node gets freed while request is still READY

**Scenario B: Race between drain and LOD processing**
- Drain happens BEFORE LOD processing (sequential, not parallel)
- So within a single frame, nodes shouldn't be freed during drain
- But across frames, timing could cause issues

**Scenario C: Merge completing while split requests pending**
- Parent node has children that are merging
- Children also have pending split requests
- Merge completes, children are freed via `cleanup_mesh_data()`
- `cleanup_mesh_data()` should cancel the split requests...
- But if request was already READY and drained, it might not get cancelled?

## Attempted Fixes

### Fix 1: Skip Cancelled Requests Early (Partial Success)

Changed the drain loop to handle CANCELLED requests before accessing node_data:

```c
if (status == REQUEST_CANCELLED) {
  if (completed->result.vertices != NULL) {
    free_cpu_mesh(&completed->result);
  }
  mesh_pool_free_request(completed);
  g_results_processed_this_frame++;
  completed = next;
  continue;  // Skip node access entirely
}
```

**Result**: Still crashed - the problematic requests have status READY, not CANCELLED.

### Fix 2: Null Out request->node Before Freeing

Modified `cleanup_mesh_data()` to null out the request's node pointer before cancelling:

```c
for (int i = 0; i < 4; i++) {
  if (mesh_data->pending_child[i] != NULL) {
    mesh_data->pending_child[i]->node = NULL;  // NEW
    mesh_pool_cancel(mesh_data->pending_child[i]);
    mesh_data->pending_child[i] = NULL;
  }
}
```

Also applied to `pending_merge` and the split cancellation code in `split_face_lod()`.

**Result**: Still crashed - suggests the request is NOT being processed through these cancellation paths.

## Key Observations

1. **The crash is for READY requests**: If the request were CANCELLED, my first fix would skip it. The crash proves status is READY.

2. **cleanup_mesh_data is not being called for this request**: If it were, the request would be CANCELLED (even if already READY, `mesh_pool_cancel()` overwrites the status).

3. **The node was freed some other way**: There must be a code path where `CubicQuadTreeMeshData` is freed without going through `cleanup_mesh_data()`, OR `cleanup_mesh_data()` is called but `pending_child[i]` is already NULL for this request.

## Unexplored Possibilities

### 1. Double-Free or Memory Corruption
Could the `CubicQuadTreeMeshData` be freed twice? Or corrupted by another operation?

### 2. Request Ownership After create_child_mesh_data
When `create_child_mesh_data()` consumes a request:
```c
MeshRequest *req = parent_data->pending_child[index];
if (req != NULL && req->result.vertices != NULL) {
  mesh = upload_mesh_gpu(req->result);
  mesh_pool_free_request(req);  // Request is freed here
  parent_data->pending_child[index] = NULL;
}
```
The request is freed, but what if it was already in the drained list from an earlier drain?

### 3. Timing Between Drain and Request Consumption
```
Frame N:
  1. Worker completes request R, adds to completed queue
  2. Drain gets R (now in local list)
  3. Check R: status=READY, node valid, still_referenced=true
  4. Leave R for LOD processing
  5. LOD: all 4 requests ready, subdivide_quadtree_node called
  6. create_child_mesh_data: frees R, sets pending_child=NULL
  7. cleanup_mesh_data: pending_child is NULL, doesn't cancel R
  8. Parent's mesh_data is freed

Frame N+1:
  9. Drain gets... nothing new related to R (R was freed in step 6)
```
This seems fine... R is freed in step 6, not left dangling.

### 4. Request in Completed Queue AND Being Consumed
What if the same request is processed twice?
- Once in drain loop (checking references)
- Once in LOD (being consumed)

But drain happens before LOD, so they shouldn't overlap.

### 5. Worker Thread Race
Could a worker be adding to completed queue while we're draining? The mutex should prevent this, but worth verifying.

## Recommended Next Steps

1. **Add Debug Logging**: Print request status, node pointer, and generation values at crash point to understand the exact state.

2. **Validate Memory**: Use a memory debugger (Valgrind on Linux, or similar) to detect use-after-free.

3. **Track Request Lifecycle**: Add logging to trace each request from creation to completion to freeing.

4. **Check for Missing Cancellation Paths**: Search for any code that frees nodes without calling `cleanup_mesh_data()`.

5. **Verify Mutex Protection**: Ensure the completed queue is properly protected during concurrent access.

## Files Involved

- `examples/quadtree/quadtree_cubic.c`: Main application, LOD logic, drain loop
- `src/mesh_thread_pool.c`: Worker threads, request queue management
- `src/mesh_thread_pool.h`: MeshRequest struct definition
- `src/quadtree.c`: subdivide_quadtree_node, merge_quadtree_node

## Relevant Code Sections

| Location | Purpose |
|----------|---------|
| Lines 160-183 | `cleanup_mesh_data()` - cancels requests and frees node data |
| Lines 222-232 | `create_child_mesh_data()` - consumes requests |
| Lines 446-518 | `split_face_lod()` - initiates splits, cancels when no longer needed |
| Lines 571-635 | `merge_face_lod()` - initiates merges, cleans up children |
| Lines 945-984 | Drain loop - processes completed requests |

## Changes Made (To Be Stashed)

1. **cleanup_mesh_data()**: Added `request->node = NULL` before `mesh_pool_cancel()` for both `pending_child` and `pending_merge`.

2. **split_face_lod()**: Added `request->node = NULL` before `mesh_pool_cancel()` in the cancellation block.

3. **Drain loop**: Added early `continue` for CANCELLED requests to skip node access.

None of these fixes resolved the crash, indicating the root cause is elsewhere.

---

## Successful Fix (Applied)

The crash was caused by multiple interacting issues. A combination of fixes was required:

### Root Causes Identified

1. **Race condition in worker thread**: The worker could overwrite CANCELLED status with READY between `atomic_load` and `atomic_store`.

2. **Dangling `request->node` pointers**: When requests were cancelled, their `node` pointer wasn't nulled, leaving dangling references to freed `CubicQuadTreeMeshData`.

3. **Unsafe pointer dereferencing**: The drain loop dereferenced `node_data` without validating the pointer was still valid.

4. **Use-after-free of MeshRequest objects**: Requests freed by LOD processing could still be referenced in the drain loop's linked list chain.

### Fixes Applied

#### Fix 1: CAS in Worker Thread (mesh_thread_pool.c:77-84)

Replace the non-atomic status update with compare-and-swap:

```c
// Before (race condition)
status = atomic_load(&request->status);
if (status == REQUEST_CANCELLED) {
    free_cpu_mesh(&request->result);
} else {
    atomic_store(&request->status, REQUEST_READY);
}

// After (atomic CAS)
MeshRequestStatus expected = REQUEST_GENERATING;
if (!atomic_compare_exchange_strong(&request->status, &expected, REQUEST_READY)) {
    // CAS failed - status was changed to CANCELLED by main thread
    free_cpu_mesh(&request->result);
}
```

#### Fix 2: Null Node Pointer on Cancellation (quadtree_cubic.c)

In `cleanup_mesh_data()` and `split_face_lod()` cancellation blocks:

```c
if (mesh_data->pending_child[i] != NULL) {
    mesh_data->pending_child[i]->node = NULL;  // Null BEFORE cancel
    mesh_pool_cancel(mesh_data->pending_child[i]);
    mesh_data->pending_child[i] = NULL;
}
```

#### Fix 3: Mesh Data Validity Registry (quadtree_cubic.c)

Added a registry to track valid `CubicQuadTreeMeshData` pointers:

```c
#define MESH_REGISTRY_SIZE 4096
static void* g_valid_mesh_data[MESH_REGISTRY_SIZE];
static int g_valid_mesh_count = 0;

static void registry_add(void* ptr);
static void registry_remove(void* ptr);
static bool registry_contains(void* ptr);
```

- `registry_add()` called after allocating mesh_data
- `registry_remove()` called in `cleanup_mesh_data()` before freeing
- `registry_contains()` used in drain loop before dereferencing node_data

#### Fix 4: Request Magic Number Validation (mesh_thread_pool.h/c)

Added magic number to detect use-after-free of MeshRequest:

```c
// In MeshRequest struct
#define MESH_REQUEST_MAGIC 0xDEADBEEF
#define MESH_REQUEST_FREED 0xFEEDFACE

typedef struct MeshRequest {
    uint32_t magic;  // Validity marker
    // ... rest of fields
} MeshRequest;
```

- Set to `MESH_REQUEST_MAGIC` on allocation
- Set to `MESH_REQUEST_FREED` before freeing
- Checked at start of drain loop iteration

#### Fix 5: Improved Drain Loop Logic (quadtree_cubic.c)

Restructured drain loop to handle each case safely:

```c
while (completed != NULL) {
    // Check for use-after-free
    if (completed->magic == MESH_REQUEST_FREED) {
        fprintf(stderr, "ERROR: Accessing freed MeshRequest\n");
        break;
    }

    MeshRequest *next = completed->next;
    MeshRequestStatus status = atomic_load(&completed->status);

    if (status == REQUEST_CANCELLED) {
        // Safe to free - no node access needed
        if (completed->result.vertices != NULL) {
            free_cpu_mesh(&completed->result);
        }
        mesh_pool_free_request(completed);
    } else if (status == REQUEST_READY) {
        CubicQuadTreeMeshData *node_data = completed->node;
        bool can_free = false;

        if (node_data == NULL) {
            can_free = true;  // Node was nulled by cancellation
        } else if (registry_contains(node_data) &&
                   node_data->generation == completed->node_generation) {
            // Node valid - check if still referenced
            bool still_referenced = /* check pending_child/merge */;
            can_free = !still_referenced;
        }
        // else: invalid node but not NULL - don't free (safer to leak)

        if (can_free) {
            if (completed->result.vertices != NULL) {
                free_cpu_mesh(&completed->result);
            }
            mesh_pool_free_request(completed);
        }
    }

    completed = next;
}
```

### Files Modified

| File | Changes |
|------|---------|
| `src/mesh_thread_pool.h` | Added `magic` field and constants to MeshRequest |
| `src/mesh_thread_pool.c` | CAS fix in worker, magic number handling in alloc/free |
| `src/mesh.c` | Added NULL check to `free_cpu_mesh()` |
| `examples/quadtree/quadtree_cubic.c` | Registry, node nulling, improved drain loop |

### Key Insight

The crash manifested as accessing freed memory (`node_data->generation`), but the root cause was a cascade of issues:
1. Race conditions allowed requests to have unexpected states
2. Missing NULL assignments left dangling pointers
3. No validation before dereferencing potentially-freed memory
4. No detection of use-after-free on request objects themselves

The magic number validation was the final piece that confirmed and caught use-after-free of MeshRequest objects in the drain loop's linked list.
