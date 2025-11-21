# Visibility Culling Implementation Plan

## Executive Summary

This document outlines the implementation plan for visibility culling in the planet-renderer-c project to address severe performance degradation when the camera approaches the planet surface. The current implementation renders all chunks (2000+) in both shadow and normal passes without any visibility testing, resulting in 4000+ draw calls per frame. This plan will reduce that to 600-1000 draw calls through frustum and horizon culling.

## Problem Statement

### Current Performance Issue

**Symptoms:**
- FPS drops drastically when camera gets close to planet surface
- Performance is acceptable when far away (orbit view)
- Issue began after shadow mapping implementation

**Root Cause:**
- No visibility culling implemented
- All chunks rendered twice per frame (shadow pass + normal pass)
- Exponential chunk growth with LOD when approaching surface

**Current Rendering Stats:**
| Camera Position | Chunks Generated | Draw Calls (×2 passes) | Performance |
|----------------|------------------|------------------------|-------------|
| Orbit (far) | ~100 | 200 | Good (60+ FPS) |
| Surface (close) | ~2000+ | 4000+ | Poor (<20 FPS) |

### Why Draw Call Batching Won't Work

Before implementing culling, we considered draw call batching but rejected it because:
1. **Mesh merging:** Would require rebuilding entire mesh every frame (slower than current approach)
2. **Instanced rendering:** Doesn't apply - each chunk has unique geometry, resolution, and terrain
3. **Multi-draw indirect:** Requires OpenGL 4.3+ (we're on 3.3), not exposed by Raylib

**Conclusion:** Visibility culling is the correct industry-standard solution for dynamic LOD terrain.

## Goals

### Primary Goals
1. Reduce draw calls by 70-90% through visibility testing
2. Maintain 60 FPS when close to planet surface (2000+ chunks)
3. Preserve visual quality (no popping or missing geometry)

### Performance Targets
| Metric | Current | Target | Expected After Implementation |
|--------|---------|--------|-------------------------------|
| Draw calls (close) | 4000+ | <1000 | 600-800 |
| Visible chunks (close) | 2000 | 300-500 | 300-400 |
| FPS (surface) | <20 | 60+ | 60+ |
| Culling CPU overhead | 0ms | <1ms | <0.5ms |

### Secondary Goals
1. Easy to maintain and extend
2. Debug visualization for culling (optional but helpful)
3. Foundation for future occlusion culling

## Technical Approach

### Two-Stage Culling Pipeline

```
All Chunks (2000+)
    |
    v
[1. Frustum Culling] ──> Removes 50-70% (chunks outside view cone)
    |
    v
Candidates (~600-1000)
    |
    v
[2. Horizon Culling] ──> Removes 20-40% (chunks beyond sphere horizon)
    |
    v
Visible Chunks (~300-500)
    |
    v
Render (600-1000 draw calls)
```

### Stage 1: Frustum Culling

**Purpose:** Remove chunks outside the camera's view cone.

**Algorithm:**
```c
bool IsBoundingBoxInFrustum(BoundingBox box, Frustum frustum) {
    // Test if AABB intersects with 6 frustum planes
    // If box is completely outside any plane → cull
    // Otherwise → keep
}
```

**Removes:**
- Chunks behind the camera
- Chunks to the left/right of screen
- Chunks above/below screen
- Chunks beyond far clipping plane

**Expected reduction:** 50-70% of chunks

### Stage 2: Horizon Culling (Sphere-Based)

**Purpose:** Remove chunks beyond the geometric horizon of a sphere.

**Concept:**
```
     Camera 📷
        |
        |  d (distance to planet center)
        |
        🌍 ← Planet (radius r)
       /   \
      /     \  ← Only front hemisphere visible
     /       \    Back hemisphere geometrically impossible to see
```

**Algorithm:**
```c
bool IsChunkBeyondHorizon(Vector3 chunkCenter, Vector3 cameraPos, Vector3 planetCenter, float planetRadius) {
    Vector3 camToPlanet = Vector3Subtract(planetCenter, cameraPos);
    Vector3 camToChunk = Vector3Subtract(chunkCenter, cameraPos);

    float distToPlanet = Vector3Length(camToPlanet);

    // Calculate horizon angle using law of cosines
    // For a sphere: horizon angle = arccos(radius / distance)
    float horizonAngle = acosf(planetRadius / distToPlanet);

    // Calculate angle between planet center and chunk
    float chunkAngle = Vector3Angle(camToPlanet, camToChunk);

    // If chunk angle > horizon angle, it's beyond the horizon
    return chunkAngle > horizonAngle + HORIZON_TOLERANCE;
}
```

**Removes:**
- Back hemisphere when in orbit
- Chunks "around the curve" when on surface
- Chunks geometrically impossible to see

**Expected reduction:** Additional 20-40% of remaining chunks

### Combined Performance Impact

```
2000 chunks (no culling)
  → 600-1000 after frustum culling (50-70% removed)
  → 300-500 after horizon culling (additional 30-50% removed)

Total reduction: 75-85% of chunks culled
```

## Implementation Details

### File Structure Changes

```
planet-renderer-c/
├── include/
│   ├── culling.h              [NEW] Frustum & horizon culling functions
│   └── math_utils.h           [MODIFY] Add Frustum struct, plane intersection
├── src/
│   ├── culling.c              [NEW] Culling implementation
│   ├── math_utils.c           [MODIFY] Add frustum extraction from camera
│   └── planet.c               [MODIFY] Integrate culling in draw loops
└── docs/
    └── VISIBILITY_CULLING_IMPLEMENTATION.md [THIS FILE]
```

### Data Structures

#### Frustum Representation

```c
// In include/math_utils.h

typedef struct Plane {
    Vector3 normal;   // Plane normal (normalized)
    float distance;   // Distance from origin along normal
} Plane;

typedef struct Frustum {
    Plane planes[6];  // Left, Right, Top, Bottom, Near, Far
} Frustum;

// Extract frustum from Raylib Camera
Frustum GetCameraFrustum(Camera3D camera);

// Test if AABB intersects frustum
bool CheckBoundingBoxFrustum(BoundingBox box, Frustum frustum);
```

#### Culling Context

```c
// In include/culling.h

typedef struct CullingContext {
    Frustum cameraFrustum;
    Vector3 cameraPosition;
    Vector3 planetCenter;
    float planetRadius;

    // Statistics (optional, for debugging)
    int totalChunks;
    int chunksAfterFrustum;
    int chunksAfterHorizon;
    int chunksRendered;
} CullingContext;

// Initialize culling context from camera and planet
CullingContext CreateCullingContext(Camera3D camera, Vector3 planetCenter, float planetRadius);

// Test chunk visibility
bool IsChunkVisible(QuadtreeNode* node, CullingContext* context);
```

### Algorithm Pseudocode

#### Frustum Extraction (math_utils.c)

```c
Frustum GetCameraFrustum(Camera3D camera) {
    // 1. Get projection matrix from Raylib
    Matrix proj = GetCameraProjectionMatrix(&camera, aspect);

    // 2. Get view matrix
    Matrix view = GetCameraViewMatrix(&camera);

    // 3. Combine into view-projection matrix
    Matrix viewProj = MatrixMultiply(view, proj);

    // 4. Extract 6 planes from view-projection matrix
    //    Using Gribb-Hartmann method:
    //    Left:   row4 + row1
    //    Right:  row4 - row1
    //    Bottom: row4 + row2
    //    Top:    row4 - row2
    //    Near:   row4 + row3
    //    Far:    row4 - row3

    Frustum frustum;
    // Extract each plane...

    // 5. Normalize planes
    for (int i = 0; i < 6; i++) {
        NormalizePlane(&frustum.planes[i]);
    }

    return frustum;
}
```

#### AABB-Frustum Intersection (math_utils.c)

```c
bool CheckBoundingBoxFrustum(BoundingBox box, Frustum frustum) {
    // For each of 6 frustum planes
    for (int i = 0; i < 6; i++) {
        Plane plane = frustum.planes[i];

        // Find the "positive vertex" (furthest point in direction of normal)
        Vector3 positiveVertex;
        positiveVertex.x = (plane.normal.x > 0) ? box.max.x : box.min.x;
        positiveVertex.y = (plane.normal.y > 0) ? box.max.y : box.min.y;
        positiveVertex.z = (plane.normal.z > 0) ? box.max.z : box.min.z;

        // Test if positive vertex is outside this plane
        float distance = Vector3DotProduct(plane.normal, positiveVertex) + plane.distance;

        if (distance < 0) {
            // Box is completely outside this plane
            return false;
        }
    }

    // Box intersects or is inside all planes
    return true;
}
```

#### Horizon Culling (culling.c)

```c
bool IsChunkBeyondHorizon(Vector3 chunkCenter, CullingContext* context) {
    Vector3 camToPlanet = Vector3Subtract(context->planetCenter, context->cameraPosition);
    Vector3 camToChunk = Vector3Subtract(chunkCenter, context->cameraPosition);

    float distToPlanet = Vector3Length(camToPlanet);

    // Special case: camera inside planet (shouldn't happen, but be safe)
    if (distToPlanet <= context->planetRadius) {
        return false; // Don't cull anything
    }

    // Calculate geometric horizon angle
    float horizonAngle = acosf(context->planetRadius / distToPlanet);

    // Calculate angle to chunk
    Vector3 dirToPlanet = Vector3Normalize(camToPlanet);
    Vector3 dirToChunk = Vector3Normalize(camToChunk);
    float chunkAngle = acosf(Vector3DotProduct(dirToPlanet, dirToChunk));

    // Add small tolerance to prevent edge artifacts
    const float HORIZON_TOLERANCE = 0.1f; // ~5.7 degrees

    return chunkAngle > (horizonAngle + HORIZON_TOLERANCE);
}
```

#### Combined Visibility Test (culling.c)

```c
bool IsChunkVisible(QuadtreeNode* node, CullingContext* context) {
    // Stage 1: Frustum culling (fast rejection)
    if (!CheckBoundingBoxFrustum(node->bounds, context->cameraFrustum)) {
        return false; // Outside view frustum
    }

    context->chunksAfterFrustum++;

    // Stage 2: Horizon culling (removes back hemisphere)
    if (IsChunkBeyondHorizon(node->sphereCenter, context)) {
        return false; // Beyond geometric horizon
    }

    context->chunksAfterHorizon++;

    return true; // Passed all tests, chunk is visible
}
```

#### Integration in Planet Drawing (planet.c)

```c
// In Planet_Draw()
int Planet_Draw(Planet* planet, Camera3D camera) {
    // Create culling context
    CullingContext cullingCtx = CreateCullingContext(
        camera,
        planet->origin,  // Assuming planet has origin field
        planet->radius   // Assuming planet has radius field
    );

    QuadtreeNode** leafNodes;
    int leafCount;
    CubicQuadTree_GetLeafNodes(planet->quadtree, &leafNodes, &leafCount);

    cullingCtx.totalChunks = leafCount;
    int totalTriangles = 0;

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            // *** ADD VISIBILITY TEST HERE ***
            if (!IsChunkVisible(node, &cullingCtx)) {
                continue; // Skip invisible chunk
            }

            Chunk* chunk = (Chunk*)node->userData;
            Chunk_DrawWithShadow(chunk, planet->surfaceColor, planet->wireframeColor,
                                 planet->lightingShader, planet->shadowMapTexture);

            totalTriangles += (chunk->resolution * chunk->resolution * 2);
            cullingCtx.chunksRendered++;
        }
    }

    free(leafNodes);

    // Optional: Print culling stats
    #ifdef DEBUG_CULLING
    printf("Culling: %d total -> %d after frustum -> %d after horizon -> %d rendered\n",
           cullingCtx.totalChunks,
           cullingCtx.chunksAfterFrustum,
           cullingCtx.chunksAfterHorizon,
           cullingCtx.chunksRendered);
    #endif

    return totalTriangles;
}

// In Planet_DrawWithShader() - same changes for shadow pass
int Planet_DrawWithShader(Planet* planet, Shader shader, Camera3D camera) {
    // Create culling context
    CullingContext cullingCtx = CreateCullingContext(
        camera,
        planet->origin,
        planet->radius
    );

    // ... same culling logic ...

    for (int i = 0; i < leafCount; i++) {
        QuadtreeNode* node = leafNodes[i];
        if (node->userData) {
            // *** ADD VISIBILITY TEST HERE ***
            if (!IsChunkVisible(node, &cullingCtx)) {
                continue;
            }

            Chunk* chunk = (Chunk*)node->userData;
            Chunk_Draw(chunk, BLACK, BLACK, shader);
            // ...
        }
    }
}
```

### API Changes

#### Modified Functions

**planet.c:**
```c
// OLD
int Planet_Draw(Planet* planet);
int Planet_DrawWithShader(Planet* planet, Shader shader);

// NEW (camera parameter added)
int Planet_Draw(Planet* planet, Camera3D camera);
int Planet_DrawWithShader(Planet* planet, Shader shader, Camera3D camera);
```

**simple_planet.c (example):**
```c
// OLD
int triangles = Planet_Draw(planet);
Planet_DrawWithShader(planet, shadowShader);

// NEW
int triangles = Planet_Draw(planet, camera);
Planet_DrawWithShader(planet, shadowShader, camera);
```

#### New Functions

**math_utils.h:**
```c
Frustum GetCameraFrustum(Camera3D camera, float aspectRatio);
bool CheckBoundingBoxFrustum(BoundingBox box, Frustum frustum);
void NormalizePlane(Plane* plane);
```

**culling.h:**
```c
CullingContext CreateCullingContext(Camera3D camera, Vector3 planetCenter, float planetRadius);
bool IsChunkVisible(QuadtreeNode* node, CullingContext* context);
bool IsChunkBeyondHorizon(Vector3 chunkCenter, CullingContext* context);
```

### Constants and Tuning Parameters

```c
// In culling.h

// Horizon tolerance to prevent edge artifacts (radians)
// ~5.7 degrees provides good balance between culling and safety
#define HORIZON_TOLERANCE 0.1f

// Optional: Distance beyond which to skip horizon culling
// (too far away for it to matter - optimization)
#define HORIZON_CULLING_MAX_DISTANCE 100000000.0f  // 100,000 km

// Debug flag
// #define DEBUG_CULLING  // Uncomment to enable culling statistics
```

## Shadow Pass Considerations

### Should Shadow Pass Use Same Culling?

**Question:** Should we cull chunks in the shadow pass the same way as the normal pass?

**Answer:** Mostly yes, with one important difference.

#### Frustum Culling for Shadows

**Use camera frustum, NOT light frustum:**
- Chunks outside the camera view can't cast visible shadows
- Light frustum would be enormous (entire sunlit hemisphere)
- Camera frustum is the limiting factor

#### Horizon Culling for Shadows

**Use same horizon culling:**
- Chunks beyond the horizon can't cast shadows into view
- Same geometric reasoning applies

#### Potential Future Enhancement

For perfect shadows, we'd need:
```c
bool CanChunkCastVisibleShadow(chunk, camera, light) {
    // 1. Is chunk in camera frustum? (or can it cast shadow into frustum?)
    // 2. Is chunk lit by the light source?
    // 3. Can its shadow intersect the camera frustum?
}
```

But this is complex and the simple approach (camera frustum + horizon) gives 95% of the benefit.

### Implementation for Shadow Pass

```c
// In simple_planet.c - shadow pass
BeginTextureMode(shadowMap);
    rlClearScreenBuffers();
    rlViewport(0, 0, 2048, 2048);

    // Use camera for culling, even in shadow pass
    Planet_DrawWithShader(planet, shadowShader, camera);
EndTextureMode();
```

This ensures we only render chunks that could possibly affect the visible shadows.

## Testing Plan

### Phase 1: Unit Testing

**Test frustum extraction:**
```c
void TestFrustumExtraction() {
    Camera3D camera = CreateTestCamera();
    Frustum frustum = GetCameraFrustum(camera, 16.0f/9.0f);

    // Test known points
    assert(CheckBoundingBoxFrustum(boxInView, frustum) == true);
    assert(CheckBoundingBoxFrustum(boxBehindCamera, frustum) == false);
    assert(CheckBoundingBoxFrustum(boxToLeft, frustum) == false);
}
```

**Test horizon culling:**
```c
void TestHorizonCulling() {
    CullingContext ctx;
    ctx.planetCenter = (Vector3){0, 0, 0};
    ctx.planetRadius = 1000.0f;
    ctx.cameraPosition = (Vector3){0, 2000, 0}; // Above planet

    Vector3 frontHemisphere = (Vector3){0, 1000, 0};  // Should be visible
    Vector3 backHemisphere = (Vector3){0, -1000, 0};  // Should be culled

    assert(IsChunkBeyondHorizon(frontHemisphere, &ctx) == false);
    assert(IsChunkBeyondHorizon(backHemisphere, &ctx) == true);
}
```

### Phase 2: Integration Testing

**Visual verification:**
1. Add debug visualization mode that colors chunks based on culling:
   - Green: Passed all tests (rendered)
   - Red: Culled by frustum
   - Blue: Culled by horizon

2. Move camera around planet and verify:
   - No visible chunks are culled (no popping)
   - Back hemisphere is culled when in orbit
   - Chunks behind camera are culled
   - Chunks to sides are culled

**Performance verification:**
```c
// Add timing and statistics
double cullTime = GetTime();
// ... perform culling ...
cullTime = GetTime() - cullTime;

printf("Culling: %.2fms, %d/%d chunks visible\n",
       cullTime * 1000.0,
       chunksRendered,
       totalChunks);
```

### Phase 3: Performance Benchmarking

**Test scenarios:**

| Scenario | Camera Position | Expected Chunks | Expected FPS |
|----------|----------------|-----------------|--------------|
| Orbit view | 10km altitude | 80-150 visible | 60+ |
| High altitude | 1km altitude | 200-400 visible | 60+ |
| Surface | 10m altitude | 300-500 visible | 60+ |
| Surface closeup | 1m altitude | 400-600 visible | 50+ |

**Measurements:**
- FPS before vs after
- Draw calls before vs after
- Culling CPU time
- GPU utilization

### Phase 4: Edge Case Testing

**Test edge cases:**
1. Camera inside planet (shouldn't happen, but be robust)
2. Very close to surface (millimeter altitude)
3. Very far from planet (millions of km)
4. Rapid camera movement
5. Camera spinning in place
6. Transition from orbit to surface

## Potential Issues and Mitigations

### Issue 1: Popping at Frustum Edges

**Problem:** Chunks appearing/disappearing at screen edges.

**Cause:** Bounding box test is binary (in/out).

**Mitigation:**
- Use conservative bounding boxes (slightly larger)
- Add small tolerance to frustum planes
- Already mitigated by HORIZON_TOLERANCE

### Issue 2: Missing Shadows

**Problem:** Shadow-casting chunks culled incorrectly.

**Cause:** Using camera frustum for shadow pass is approximation.

**Mitigation:**
- Accept as reasonable trade-off (95% correct)
- Future: Implement proper shadow frustum if needed
- Shadows for out-of-view objects are rarely noticeable

### Issue 3: Culling Overhead

**Problem:** Culling takes too much CPU time.

**Cause:** Testing 2000+ chunks per frame.

**Mitigation:**
- Frustum test is very fast (~10-20 instructions per chunk)
- Horizon test is fast (~50 instructions per chunk)
- Expected total: <0.5ms for 2000 chunks
- If needed: Hierarchical culling (test parent nodes first)

### Issue 4: Precision Issues at Large Distances

**Problem:** Floating-point precision errors at planet scale.

**Cause:** Vectors with very large coordinates (millions of meters).

**Mitigation:**
- Use double precision for horizon angle calculation if needed
- Already using normalized directions (helps with precision)
- Add epsilon to comparisons

### Issue 5: Chunks at Horizon Flickering

**Problem:** Chunks at horizon edge flickering in/out.

**Cause:** Numerical precision at horizon boundary.

**Mitigation:**
- HORIZON_TOLERANCE already provides hysteresis
- If needed: Use different tolerance for culling vs un-culling (hysteresis)

## Debug Visualization (Optional Enhancement)

### Culling Statistics Overlay

```c
// In simple_planet.c
#ifdef DEBUG_CULLING
DrawText(TextFormat("Total chunks: %d", totalChunks), 10, 70, 20, WHITE);
DrawText(TextFormat("After frustum: %d (%.1f%%)",
         afterFrustum,
         100.0f * afterFrustum / totalChunks), 10, 90, 20, YELLOW);
DrawText(TextFormat("After horizon: %d (%.1f%%)",
         afterHorizon,
         100.0f * afterHorizon / totalChunks), 10, 110, 20, GREEN);
DrawText(TextFormat("Culled: %d (%.1f%%)",
         totalChunks - afterHorizon,
         100.0f * (totalChunks - afterHorizon) / totalChunks), 10, 130, 20, RED);
#endif
```

### Frustum Visualization

```c
void DrawFrustum(Frustum frustum, Color color) {
    // Draw 6 frustum planes as wireframe
    // Useful for debugging frustum extraction
}
```

### Chunk Color Coding

```c
Color GetChunkDebugColor(CullingResult result) {
    switch (result) {
        case CULLED_FRUSTUM: return RED;
        case CULLED_HORIZON: return BLUE;
        case VISIBLE: return GREEN;
        default: return WHITE;
    }
}
```

## Implementation Timeline

### Phase 1: Foundation (1-2 hours)
- [ ] Create `include/culling.h` with data structures
- [ ] Create `src/culling.c` stub
- [ ] Add Frustum and Plane to `math_utils.h`
- [ ] Update CMakeLists.txt

### Phase 2: Math Utilities (2-3 hours)
- [ ] Implement `GetCameraFrustum()` in math_utils.c
- [ ] Implement `CheckBoundingBoxFrustum()` in math_utils.c
- [ ] Implement `NormalizePlane()` helper
- [ ] Unit test frustum extraction

### Phase 3: Culling Logic (2-3 hours)
- [ ] Implement `CreateCullingContext()` in culling.c
- [ ] Implement `IsChunkBeyondHorizon()` in culling.c
- [ ] Implement `IsChunkVisible()` in culling.c
- [ ] Unit test horizon culling

### Phase 4: Integration (1-2 hours)
- [ ] Modify `Planet_Draw()` signature to accept camera
- [ ] Modify `Planet_DrawWithShader()` signature to accept camera
- [ ] Add visibility tests in both functions
- [ ] Update `simple_planet.c` example
- [ ] Update planet.h header

### Phase 5: Testing & Tuning (2-3 hours)
- [ ] Visual testing (walk around planet, verify no popping)
- [ ] Performance benchmarking (measure FPS improvement)
- [ ] Tune HORIZON_TOLERANCE if needed
- [ ] Add debug statistics overlay
- [ ] Test edge cases

### Phase 6: Documentation (1 hour)
- [ ] Update README.md with performance notes
- [ ] Document new API in planet.h
- [ ] Add code comments
- [ ] Update this implementation doc with results

**Total estimated time: 9-14 hours**

## Success Criteria

The implementation will be considered successful when:

1. **Performance targets met:**
   - [ ] 60+ FPS when close to surface (previously <20 FPS)
   - [ ] Draw calls reduced from 4000+ to <1000
   - [ ] Culling overhead <1ms per frame

2. **Visual quality maintained:**
   - [ ] No visible geometry popping
   - [ ] No missing chunks in view
   - [ ] Shadows remain correct (no major artifacts)

3. **Code quality:**
   - [ ] Clean, documented code
   - [ ] No memory leaks
   - [ ] Minimal API changes

4. **Testing complete:**
   - [ ] All edge cases tested
   - [ ] Performance benchmarked
   - [ ] Visual verification passed

## Future Enhancements

After initial implementation, consider:

1. **Hierarchical culling:** Test parent nodes before children (skip entire subtrees)
2. **Occlusion culling:** Cull mountains behind other mountains
3. **Shadow frustum optimization:** Dedicated shadow frustum for perfect shadow culling
4. **Temporal coherence:** Cache visibility results for chunks between frames
5. **LOD factor in culling:** Different horizon tolerance based on chunk LOD level

## References

### Frustum Culling
- "Real-Time Rendering" (4th ed.), Akenine-Möller et al., Chapter 19
- Gribb & Hartmann, "Fast Extraction of Viewing Frustum Planes from the World-View-Projection Matrix" (2001)

### Horizon Culling
- "3D Engine Design for Virtual Globes" (Cozzi & Ring, 2011), Chapter 6
- Geometry-based horizon culling for spherical worlds

### General Optimization
- "GPU Gems 2", Chapter 6: "Hardware Occlusion Queries Made Useful"
- "Game Engine Architecture" (Gregory, 2018), Chapter 10: Visibility Determination

## Appendix A: Code Snippets

### Complete Frustum Structure

```c
// include/math_utils.h

typedef struct Plane {
    Vector3 normal;
    float distance;
} Plane;

typedef struct Frustum {
    Plane planes[6];  // LEFT, RIGHT, TOP, BOTTOM, NEAR, FAR
} Frustum;

typedef enum FrustumPlane {
    FRUSTUM_LEFT = 0,
    FRUSTUM_RIGHT,
    FRUSTUM_TOP,
    FRUSTUM_BOTTOM,
    FRUSTUM_NEAR,
    FRUSTUM_FAR
} FrustumPlane;
```

### Complete Culling Context

```c
// include/culling.h

typedef struct CullingContext {
    Frustum cameraFrustum;
    Vector3 cameraPosition;
    Vector3 planetCenter;
    float planetRadius;

    // Statistics
    int totalChunks;
    int chunksAfterFrustum;
    int chunksAfterHorizon;
    int chunksRendered;
} CullingContext;

typedef enum CullingResult {
    VISIBLE,
    CULLED_FRUSTUM,
    CULLED_HORIZON
} CullingResult;
```

## Appendix B: Performance Analysis

### Expected Performance Improvement

**Before (no culling):**
```
Surface view with 2000 chunks:
- CPU: 8ms (chunk iteration + setup)
- GPU: 25ms (vertex processing + rasterization)
- Total: 33ms → 30 FPS
```

**After (with culling):**
```
Surface view with 2000 chunks (400 visible):
- CPU: 1ms (culling) + 2ms (chunk iteration + setup) = 3ms
- GPU: 5ms (vertex processing + rasterization)
- Total: 8ms → 125 FPS (capped at 60)
```

**Speedup: 4.1x frame time improvement**

### Breakdown by Stage

| Stage | Chunks In | Chunks Out | Time | Percentage |
|-------|-----------|------------|------|------------|
| Start | 2000 | - | - | 100% |
| Frustum cull | 2000 | 600 | 0.3ms | 30% |
| Horizon cull | 600 | 400 | 0.1ms | 20% |
| Render | 400 | - | 2.6ms | - |
| **Total** | - | - | **3.0ms** | **20% rendered** |

---

**Document Version:** 1.0
**Last Updated:** 2025-11-21
**Author:** Implementation Plan for planet-renderer-c visibility culling
**Status:** Ready for Implementation
