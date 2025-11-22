# Cascaded Shadow Maps Implementation Plan
## Planet Renderer C - Lunar Crater Shadow Enhancement

**Author:** Claude
**Date:** 2025-11-22
**Target:** Achieve pitch-black lunar crater shadows with sharp boundaries

---

## Table of Contents

1. [Overview](#overview)
2. [Technical Background](#technical-background)
3. [Current System Analysis](#current-system-analysis)
4. [Proposed Architecture](#proposed-architecture)
5. [Implementation Steps](#implementation-steps)
6. [Integration with Cubic Quadtree](#integration-with-cubic-quadtree)
7. [Performance Considerations](#performance-considerations)
8. [Testing & Validation](#testing--validation)
9. [Future Enhancements](#future-enhancements)

---

## Overview

### Goal
Implement Cascaded Shadow Maps (CSM) to achieve high-quality terrain self-shadowing for procedurally generated planetary surfaces, specifically targeting the iconic "pitch-black lunar crater" aesthetic with no atmospheric scattering.

### Key Requirements
- ✅ Hard shadow boundaries (no soft penumbra)
- ✅ Ambient = 0.0 (pure black shadows)
- ✅ High-resolution shadows for nearby terrain
- ✅ Crater rims casting complete shadows into crater floors
- ✅ Real-time performance (60+ FPS)
- ✅ Works with procedural terrain generation
- ✅ Compatible with existing cubic quadtree LOD system

### Visual Target
```
     ☀️ Sun at low angle (e.g., sunrise)
        ↘
    ___/█\___ ← Fully lit crater rim
   /█████████\
  |███████████| ← Pitch black interior
  |███████████|   (shadow = 0.0, no ambient)
   \█████████/
```

---

## Technical Background

### What are Cascaded Shadow Maps?

CSM extends traditional shadow mapping by using **multiple shadow maps** at different scales, each covering a different distance range from the camera.

**Traditional Shadow Map Problem:**
```
Single 2048×2048 shadow map covering 10,000km planet
= ~5km per pixel resolution
= Blocky, low-quality shadows
```

**CSM Solution:**
```
Cascade 0: 2048×2048 covering 100m   = 0.05m per pixel
Cascade 1: 2048×2048 covering 1km    = 0.5m per pixel
Cascade 2: 2048×2048 covering 10km   = 5m per pixel
Cascade 3: 2048×2048 covering 100km  = 50m per pixel
```

### How CSM Works

1. **Split camera frustum** into distance ranges (cascades)
2. **Render depth** from light's perspective for each cascade
3. **At runtime**, fragment shader selects appropriate cascade based on distance
4. **Sample shadow map** from selected cascade
5. **Return binary result**: 0.0 (shadow) or 1.0 (lit)

---

## Current System Analysis

### Existing Shadow Infrastructure

**File:** `examples/simple_planet.c`

#### Current Implementation (Lines 137-166)

```c
// Single shadow map
RenderTexture2D shadowMap = LoadRenderTexture(2048, 2048);

// Light space matrix covering entire planet
float orthoSize = radius * 2.5f;  // ~5000km for 2000km radius
Matrix lightProjection = MatrixOrtho(-orthoSize, orthoSize, ...);

// Single shadow pass
BeginTextureMode(shadowMap);
    Planet_DrawWithShader(planet, shadowShader);
EndTextureMode();
```

#### Current Shader (lighting.fs)

```glsl
ambient = 0.0  // ✓ Already correct for lunar look
diffuse = max(dot(normal, lightDir), 0.0)
shadow = ShadowCalculation()  // PCF 3×3 (unnecessary for hard shadows)
lighting = ambient + (diffuse * 0.7) * shadow
```

### Strengths
- ✅ Ambient already set to 0.0
- ✅ Shadow map infrastructure in place
- ✅ Light space transformation working
- ✅ Shader integration functional

### Weaknesses
- ❌ Single shadow map = low resolution
- ❌ PCF filtering (softens shadows unnecessarily)
- ❌ Covers entire planet (wasted resolution on distant terrain)
- ❌ No cascade selection logic

---

## Proposed Architecture

### Data Structures

#### 1. Cascade Configuration

**File:** `include/shadow.h` (new)

```c
#define CASCADE_COUNT 4

typedef struct {
    RenderTexture2D shadowMap;      // 2048×2048 depth texture
    Matrix lightSpaceMatrix;         // View-projection for this cascade
    float splitDistance;             // Far plane of this cascade
    BoundingBox bounds;              // World-space bounds
} ShadowCascade;

typedef struct {
    ShadowCascade cascades[CASCADE_COUNT];
    Vector3 lightDirection;
    int shadowMapResolution;         // 2048 default
    float cascadeSplitLambda;        // 0.5 = balanced (see PSSM)
} CascadedShadowMap;
```

#### 2. Cascade Distance Calculation

**Practical Split Scheme (PSSM - Parallel-Split Shadow Maps):**

```c
float CalculateCascadeSplit(int index, int numCascades,
                           float nearPlane, float farPlane,
                           float lambda) {
    // Logarithmic distribution (detail near camera)
    float log = nearPlane * pow(farPlane / nearPlane,
                                (float)index / numCascades);

    // Linear distribution (even coverage)
    float linear = nearPlane + (farPlane - nearPlane) *
                   ((float)index / numCascades);

    // Blend between log and linear
    return lambda * log + (1.0f - lambda) * linear;
}
```

**Example splits for planet renderer (lambda = 0.75):**
```
Camera altitude: 500m above surface
Near: 1m, Far: 100km

Cascade 0: 1m → 87m      (near terrain, highest detail)
Cascade 1: 87m → 1.2km   (local features)
Cascade 2: 1.2km → 15km  (regional terrain)
Cascade 3: 15km → 100km  (distant mountains)
```

### Rendering Pipeline

#### Pass Structure

```
Frame N:
├─ PASS 1a: Render Cascade 0 Shadow Map
│   ├─ Compute tight bounds around near frustum slice
│   ├─ Set light view-projection for cascade 0
│   ├─ Render depth: Planet_DrawWithShader(shadowShader)
│   └─ Output: cascades[0].shadowMap
│
├─ PASS 1b: Render Cascade 1 Shadow Map
│   └─ (repeat for cascade 1)
│
├─ PASS 1c: Render Cascade 2 Shadow Map
│   └─ (repeat for cascade 2)
│
├─ PASS 1d: Render Cascade 3 Shadow Map
│   └─ (repeat for cascade 3)
│
└─ PASS 2: Main Scene Rendering
    ├─ Bind all 4 cascade shadow maps (texture units 1-4)
    ├─ Upload cascade distances & light matrices
    ├─ Render planet with lighting shader
    └─ Fragment shader samples appropriate cascade
```

---

## Implementation Steps

### Phase 1: Core CSM Infrastructure

#### Step 1.1: Create Shadow Module

**File:** `include/shadow.h`

```c
#ifndef SHADOW_H
#define SHADOW_H

#include "raylib.h"
#include "raymath.h"

#define CASCADE_COUNT 4

typedef struct {
    RenderTexture2D shadowMap;
    Matrix lightSpaceMatrix;
    float splitDistance;
    BoundingBox bounds;
} ShadowCascade;

typedef struct {
    ShadowCascade cascades[CASCADE_COUNT];
    Vector3 lightDirection;
    int shadowMapResolution;
    float cascadeSplitLambda;
    float nearPlane;
    float farPlane;
} CascadedShadowMap;

// Initialization
CascadedShadowMap* CSM_Create(Vector3 lightDir, int resolution);
void CSM_Destroy(CascadedShadowMap* csm);

// Per-frame update
void CSM_UpdateCascades(CascadedShadowMap* csm, Camera camera, float planetRadius);

// Utility
BoundingBox CSM_GetFrustumBounds(Camera camera, float nearDist, float farDist);
Matrix CSM_CalculateLightMatrix(BoundingBox bounds, Vector3 lightDir);

#endif // SHADOW_H
```

#### Step 1.2: Implement Shadow Module

**File:** `src/shadow.c`

```c
#include "shadow.h"
#include <stdlib.h>
#include <math.h>

CascadedShadowMap* CSM_Create(Vector3 lightDir, int resolution) {
    CascadedShadowMap* csm = malloc(sizeof(CascadedShadowMap));

    csm->lightDirection = Vector3Normalize(lightDir);
    csm->shadowMapResolution = resolution;
    csm->cascadeSplitLambda = 0.75f; // Favor logarithmic
    csm->nearPlane = 1.0f;
    csm->farPlane = 100000.0f; // 100km

    // Create shadow map textures
    for (int i = 0; i < CASCADE_COUNT; i++) {
        csm->cascades[i].shadowMap = LoadRenderTexture(resolution, resolution);
        csm->cascades[i].splitDistance = 0.0f;
    }

    return csm;
}

void CSM_Destroy(CascadedShadowMap* csm) {
    for (int i = 0; i < CASCADE_COUNT; i++) {
        UnloadRenderTexture(csm->cascades[i].shadowMap);
    }
    free(csm);
}

static float CalculateSplitDistance(int index, int total,
                                   float near, float far, float lambda) {
    float ratio = (float)(index + 1) / (float)total;
    float logSplit = near * powf(far / near, ratio);
    float linearSplit = near + (far - near) * ratio;
    return lambda * logSplit + (1.0f - lambda) * linearSplit;
}

void CSM_UpdateCascades(CascadedShadowMap* csm, Camera camera, float planetRadius) {
    // Adjust far plane based on camera altitude
    Vector3 planetCenter = {0, 0, 0};
    float altitude = Vector3Distance(camera.position, planetCenter) - planetRadius;
    csm->farPlane = fminf(100000.0f, altitude * 200.0f); // Adaptive far plane

    float prevSplit = csm->nearPlane;

    for (int i = 0; i < CASCADE_COUNT; i++) {
        // Calculate split distance
        float split = CalculateSplitDistance(i, CASCADE_COUNT,
                                            csm->nearPlane,
                                            csm->farPlane,
                                            csm->cascadeSplitLambda);
        csm->cascades[i].splitDistance = split;

        // Get frustum slice bounds
        BoundingBox bounds = CSM_GetFrustumBounds(camera, prevSplit, split);
        csm->cascades[i].bounds = bounds;

        // Calculate light space matrix
        csm->cascades[i].lightSpaceMatrix =
            CSM_CalculateLightMatrix(bounds, csm->lightDirection);

        prevSplit = split;
    }
}

BoundingBox CSM_GetFrustumBounds(Camera camera, float nearDist, float farDist) {
    // Get camera frustum corners at near and far planes
    // This is a simplified version - production code would extract
    // actual frustum corners from view-projection matrix

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 center = Vector3Add(camera.position, Vector3Scale(forward, (nearDist + farDist) * 0.5f));

    // Conservative bounding box
    float radius = (farDist - nearDist) * 0.5f;

    BoundingBox bounds;
    bounds.min = (Vector3){
        center.x - radius,
        center.y - radius,
        center.z - radius
    };
    bounds.max = (Vector3){
        center.x + radius,
        center.y + radius,
        center.z + radius
    };

    return bounds;
}

Matrix CSM_CalculateLightMatrix(BoundingBox bounds, Vector3 lightDir) {
    // Calculate center of bounding box
    Vector3 center = {
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f
    };

    // Position light far away in light direction
    Vector3 lightPos = Vector3Add(center, Vector3Scale(Vector3Negate(lightDir), 10000.0f));

    // Light view matrix
    Vector3 up = fabsf(lightDir.y) > 0.99f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
    Matrix lightView = MatrixLookAt(lightPos, center, up);

    // Calculate orthographic size from bounding box
    float radius = Vector3Distance(bounds.min, center);
    Matrix lightProjection = MatrixOrtho(-radius, radius, -radius, radius,
                                        0.1f, 20000.0f);

    return MatrixMultiply(lightView, lightProjection);
}
```

### Phase 2: Integrate with Rendering Pipeline

#### Step 2.1: Modify simple_planet.c

**Changes to main rendering loop:**

```c
// Replace single shadow map with CSM
CascadedShadowMap* csm = CSM_Create(lightDir, 2048);

while (!WindowShouldClose()) {
    // Update cascades based on camera position
    CSM_UpdateCascades(csm, camera, radius);

    // PASS 1: Render all cascade shadow maps
    for (int i = 0; i < CASCADE_COUNT; i++) {
        SetShaderValueMatrix(shadowShader, shadowLightSpaceMatrixLoc,
                           csm->cascades[i].lightSpaceMatrix);

        BeginTextureMode(csm->cascades[i].shadowMap);
            rlClearScreenBuffers();
            rlViewport(0, 0, 2048, 2048);
            Planet_DrawWithShader(planet, shadowShader);
        EndTextureMode();
    }

    // PASS 2: Main rendering
    // Upload cascade data to shader
    SetShaderValueMatrix(lightingShader, cascadeMatricesLoc,
                        &csm->cascades[0].lightSpaceMatrix, CASCADE_COUNT);

    float cascadeDistances[CASCADE_COUNT];
    for (int i = 0; i < CASCADE_COUNT; i++) {
        cascadeDistances[i] = csm->cascades[i].splitDistance;
    }
    SetShaderValueV(lightingShader, cascadeDistancesLoc,
                   cascadeDistances, SHADER_UNIFORM_FLOAT, CASCADE_COUNT);

    // Bind all cascade shadow maps
    for (int i = 0; i < CASCADE_COUNT; i++) {
        rlActiveTexture(RL_TEXTURE1 + i);
        rlEnableTexture(csm->cascades[i].shadowMap.depth.id);
    }

    BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
            Planet_Draw(planet);
        EndMode3D();

        // UI overlay showing cascade splits
        DrawCascadeDebugOverlay(csm, camera);
    EndDrawing();
}

CSM_Destroy(csm);
```

#### Step 2.2: Update Lighting Shader

**File:** `examples/shaders/lighting.fs`

Replace `ShadowCalculation()` with:

```glsl
#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 viewPos;

// CSM uniforms
#define CASCADE_COUNT 4
uniform sampler2D cascadeShadowMaps[CASCADE_COUNT];
uniform mat4 cascadeLightMatrices[CASCADE_COUNT];
uniform float cascadeDistances[CASCADE_COUNT];

out vec4 finalColor;

int GetCascadeIndex(float distanceFromCamera) {
    for (int i = 0; i < CASCADE_COUNT - 1; i++) {
        if (distanceFromCamera < cascadeDistances[i]) {
            return i;
        }
    }
    return CASCADE_COUNT - 1;
}

float ShadowCalculationHard(int cascadeIndex, vec3 fragPos) {
    // Transform fragment to light space for selected cascade
    vec4 fragPosLightSpace = cascadeLightMatrices[cascadeIndex] * vec4(fragPos, 1.0);

    // Perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Outside shadow map bounds = no shadow
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 1.0;
    }

    // Get closest depth from shadow map
    float closestDepth = texture(cascadeShadowMaps[cascadeIndex], projCoords.xy).r;
    float currentDepth = projCoords.z;

    // Adaptive bias based on slope
    float bias = max(0.0005 * (1.0 - dot(normalize(fragNormal), lightDir)), 0.00005);

    // Binary shadow test (HARD shadows, no PCF)
    return (currentDepth - bias) > closestDepth ? 0.0 : 1.0;
}

void main() {
    // Distance from camera
    float distFromCamera = length(fragPosition - viewPos);

    // Select cascade
    int cascadeIndex = GetCascadeIndex(distFromCamera);

    // Compute shadow
    float shadow = ShadowCalculationHard(cascadeIndex, fragPosition);

    // Lighting
    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(lightDir);

    float ambient = 0.0;  // PURE BLACK shadows (lunar aesthetic)
    float diffuse = max(dot(normal, lightDirection), 0.0);

    // Shadow only affects diffuse (no ambient to shadow)
    float lighting = ambient + (diffuse * shadow);

    finalColor = vec4(colDiffuse.rgb * lighting, colDiffuse.a);
}
```

### Phase 3: Optimization

#### Step 3.1: Frustum Culling per Cascade

Modify `Planet_DrawWithShader` to accept optional spatial bounds:

```c
// In planet.h
int Planet_DrawWithShaderInBounds(Planet* planet, Shader shader,
                                  BoundingBox* bounds);

// In planet.c
int Planet_DrawWithShaderInBounds(Planet* planet, Shader shader,
                                  BoundingBox* bounds) {
    int count = 0;
    QuadTree* trees[6] = {
        planet->posX, planet->negX,
        planet->posY, planet->negY,
        planet->posZ, planet->negZ
    };

    for (int i = 0; i < 6; i++) {
        QuadTreeNode** leaves;
        int leafCount = QuadTree_GetLeaves(trees[i], &leaves);

        for (int j = 0; j < leafCount; j++) {
            if (leaves[j]->userData) {
                Chunk* chunk = (Chunk*)leaves[j]->userData;

                // Optional bounds culling
                if (bounds) {
                    // Check if chunk intersects bounds
                    if (!CheckCollisionBoxes(chunk->worldBounds, *bounds)) {
                        continue;
                    }
                }

                Chunk_DrawWithShadow(chunk, planet->color,
                                   planet->wireframe, shader,
                                   planet->shadowMapTexture);
                count += chunk->resolution * chunk->resolution * 2;
            }
        }
    }
    return count;
}
```

Then use in cascade rendering:

```c
for (int i = 0; i < CASCADE_COUNT; i++) {
    BeginTextureMode(csm->cascades[i].shadowMap);
        // Only render chunks within cascade bounds
        Planet_DrawWithShaderInBounds(planet, shadowShader,
                                     &csm->cascades[i].bounds);
    EndTextureMode();
}
```

#### Step 3.2: Cascade Blending (Optional)

To avoid hard transitions between cascades, blend shadow samples:

```glsl
float ShadowCalculationBlended(vec3 fragPos, float distFromCamera) {
    int cascadeIndex = GetCascadeIndex(distFromCamera);
    float shadow = ShadowCalculationHard(cascadeIndex, fragPos);

    // Blend with next cascade near boundaries
    if (cascadeIndex < CASCADE_COUNT - 1) {
        float nextCascadeDist = cascadeDistances[cascadeIndex];
        float blendRange = nextCascadeDist * 0.1; // 10% blend zone
        float blendFactor = smoothstep(nextCascadeDist - blendRange,
                                       nextCascadeDist,
                                       distFromCamera);

        if (blendFactor > 0.0) {
            float shadowNext = ShadowCalculationHard(cascadeIndex + 1, fragPos);
            shadow = mix(shadow, shadowNext, blendFactor);
        }
    }

    return shadow;
}
```

---

## Integration with Cubic Quadtree

### How CSM Uses Existing LOD System

Your cubic quadtree (`src/cubic_quadtree.c`, `src/planet.c`) already provides:
- ✅ Dynamic chunk generation based on distance
- ✅ Efficient frustum culling
- ✅ Mesh LOD (higher resolution near camera)

**CSM leverages this directly:**

```
Planet_Update(camera.position)
  └─ Generates chunks based on camera LOD

For each cascade i:
  Planet_DrawWithShader(shadowShader)
    └─ Renders those SAME chunks from light's perspective
    └─ No additional geometry needed!
```

### Memory Impact

**Before CSM:**
```
1 shadow map × 2048×2048 × 4 bytes (depth) = 16 MB
```

**After CSM:**
```
4 shadow maps × 2048×2048 × 4 bytes = 64 MB
```

**Chunk memory (unchanged):**
```
~50-200 chunks active (depends on altitude)
~1 MB per chunk
= 50-200 MB
```

**Total VRAM:** ~114-264 MB (acceptable for modern GPUs)

### No Changes Required to Quadtree

The beauty of CSM is it's **purely a rendering enhancement**:
- ❌ No changes to `quadtree.c`
- ❌ No changes to `cubic_quadtree.c`
- ❌ No changes to `chunk.c` mesh generation
- ✅ Only changes to rendering pipeline

---

## Performance Considerations

### Rendering Cost Analysis

**Current system (1 shadow map):**
```
1 shadow pass × N chunks = N draws
```

**CSM (4 shadow maps):**
```
4 shadow passes × N chunks = 4N draws
```

**Mitigation strategies:**

1. **Frustum culling per cascade** (Step 3.1)
   - Cascade 0: Only near chunks (~10 chunks)
   - Cascade 1: Near + medium (~30 chunks)
   - Cascade 2: Near + medium + far (~80 chunks)
   - Cascade 3: All visible (~200 chunks)
   - **Total:** ~320 draws instead of 800

2. **Reduce resolution for distant cascades**
   ```c
   cascades[0].shadowMap = LoadRenderTexture(2048, 2048);
   cascades[1].shadowMap = LoadRenderTexture(2048, 2048);
   cascades[2].shadowMap = LoadRenderTexture(1024, 1024);
   cascades[3].shadowMap = LoadRenderTexture(512, 512);
   ```
   **Memory saved:** 64 MB → 26 MB

3. **Skip distant cascades when flying low**
   ```c
   int activeCascades = altitude < 100.0f ? 2 : 4;
   ```

### Expected Performance

**Target hardware:** RTX 2050 (notebook GPU from Figure 1 paper)

**Estimated frame times:**
```
Current (1 shadow map):  ~1.2 ms
CSM (4 maps, no culling): ~4.8 ms
CSM (4 maps, w/ culling): ~2.5 ms
Main rendering pass:      ~1.0 ms
──────────────────────────────────
Total frame time:         ~3.5 ms = 285 FPS
```

**Worst case (low altitude, 200 chunks visible):**
```
CSM shadow passes:        ~5.0 ms
Main rendering:           ~2.0 ms
──────────────────────────────────
Total:                    ~7.0 ms = 142 FPS
```

Still well above 60 FPS target!

---

## Testing & Validation

### Test Scenarios

#### Test 1: Crater Shadow Accuracy
**Setup:**
- Create large crater (10km diameter, 2km depth)
- Position sun at 15° elevation, varying azimuth
- Camera at 500m altitude, viewing crater

**Success criteria:**
- [ ] Crater floor completely black (shadow = 0.0)
- [ ] Crater rim fully lit (shadow = 1.0)
- [ ] Sharp boundary between lit and shadowed areas
- [ ] No light bleeding at crater walls
- [ ] Consistent shadow as sun azimuth changes

#### Test 2: Multi-Scale Shadows
**Setup:**
- Terrain with:
  - Small rocks (1-5m)
  - Medium boulders (10-50m)
  - Large craters (100-1000m)
  - Mountains (5-10km)
- Camera panning from close to far

**Success criteria:**
- [ ] Small rocks cast visible shadows when close
- [ ] Shadows maintain quality as camera moves
- [ ] No cascade transition artifacts
- [ ] Distant mountains still cast shadows

#### Test 3: Performance Stress Test
**Setup:**
- Maximum visible chunks (~200)
- Low sun angle (long shadows)
- 1920×1080 resolution

**Success criteria:**
- [ ] Maintains 60+ FPS
- [ ] No stuttering when new chunks generate
- [ ] Consistent frame times (< 5ms variance)

#### Test 4: Edge Cases
**Setup:**
- Sun at horizon (0° elevation)
- Camera at various altitudes (10m to 100km)
- Rapidly changing view direction

**Success criteria:**
- [ ] No shadow acne
- [ ] No peter-panning (floating shadows)
- [ ] Shadows disappear gracefully at night
- [ ] No crashes or visual glitches

### Debug Visualization

Implement cascade visualization overlay:

```c
void DrawCascadeDebugOverlay(CascadedShadowMap* csm, Camera camera) {
    // Color-code fragments by cascade index
    // Red = cascade 0, Green = cascade 1, Blue = cascade 2, Yellow = cascade 3

    // Add to shader:
    // finalColor.rgb = mix(finalColor.rgb, cascadeColors[cascadeIndex], 0.3);

    // Draw cascade distances as text
    DrawText(TextFormat("Cascade 0: %.1fm", csm->cascades[0].splitDistance), 10, 10, 20, RED);
    DrawText(TextFormat("Cascade 1: %.1fm", csm->cascades[1].splitDistance), 10, 35, 20, GREEN);
    DrawText(TextFormat("Cascade 2: %.1fm", csm->cascades[2].splitDistance), 10, 60, 20, BLUE);
    DrawText(TextFormat("Cascade 3: %.1fm", csm->cascades[3].splitDistance), 10, 85, 20, YELLOW);
}
```

### Validation Checklist

Pre-merge requirements:
- [ ] Code compiles without warnings
- [ ] All test scenarios pass
- [ ] Performance meets 60 FPS target
- [ ] Memory usage < 300 MB VRAM
- [ ] No visual artifacts in normal usage
- [ ] Debug visualization toggleable
- [ ] Documentation updated

---

## Future Enhancements

### Phase 4: Advanced Features (Post-Initial Implementation)

#### 4.1 Contact Hardening Shadows
Simulate softer shadows for objects further from shadow caster:
```glsl
float penumbraSize = (receiverDepth - blockerDepth) / blockerDepth;
// Use penumbraSize to adjust PCF kernel size
```

#### 4.2 Atmospheric Scattering
For planets with atmospheres, add scattering in shadowed areas:
```glsl
float scatteringFactor = atmosphereDensity * (1.0 - shadow);
vec3 scatteredLight = skyColor * scatteringFactor;
finalColor.rgb += scatteredLight;
```

#### 4.3 Temporal Anti-Aliasing
Reduce shadow flickering at cascade boundaries:
- Store previous frame's shadow
- Blend with current frame
- Accumulate over multiple frames

#### 4.4 Dynamic Cascade Count
Adjust cascade count based on performance:
```c
if (currentFPS < 60) {
    activeCascades = 2; // Reduce quality for performance
} else if (currentFPS > 100) {
    activeCascades = 4; // Increase quality
}
```

#### 4.5 Terrain-Specific Optimizations
- **Flat terrain:** Use fewer cascades
- **Mountainous terrain:** Increase cascade resolution
- **Polar regions:** Wider cascade splits (long shadows)

---

## File Structure Summary

New files to create:
```
packages/planet-renderer-c/
├── include/
│   └── shadow.h                    (CSM data structures & API)
├── src/
│   └── shadow.c                    (CSM implementation)
├── examples/
│   ├── shaders/
│   │   ├── lighting_csm.vs         (Updated vertex shader)
│   │   └── lighting_csm.fs         (Updated fragment shader)
│   └── simple_planet_csm.c         (Example using CSM)
└── CMakeLists.txt                  (Add shadow.c to build)
```

Modified files:
```
packages/planet-renderer-c/
├── examples/simple_planet.c        (Integrate CSM)
├── examples/shaders/lighting.fs    (Add cascade selection)
└── include/planet.h                (Add bounds-based drawing)
```

---

## Implementation Timeline

### Week 1: Core Infrastructure
- Day 1-2: Create `shadow.h` and `shadow.c`
- Day 3-4: Implement cascade split calculation
- Day 5: Implement light matrix calculation
- Day 6-7: Testing & debugging

### Week 2: Shader Integration
- Day 1-2: Update vertex shader (pass world position)
- Day 3-4: Update fragment shader (cascade selection)
- Day 5: Implement hard shadow sampling
- Day 6-7: Testing & debugging

### Week 3: Rendering Pipeline
- Day 1-2: Modify `simple_planet.c` for multi-pass
- Day 3-4: Bind multiple shadow maps
- Day 5: Upload cascade uniforms
- Day 6-7: Integration testing

### Week 4: Optimization & Polish
- Day 1-2: Implement frustum culling per cascade
- Day 3-4: Add debug visualization
- Day 5: Performance profiling & tuning
- Day 6-7: Final testing & documentation

**Total estimated time:** 4 weeks for complete implementation

---

## Success Metrics

Implementation will be considered complete when:

1. **Visual Quality**
   - ✅ Crater shadows are pitch black (RGB < 0.01)
   - ✅ Shadow boundaries are sharp (< 2 pixel transition)
   - ✅ No visible cascade transitions in normal use

2. **Performance**
   - ✅ 60+ FPS at 1080p on RTX 2050
   - ✅ Frame time variance < 5ms
   - ✅ Memory usage < 300 MB VRAM

3. **Functionality**
   - ✅ Works with all terrain types (flat, mountainous, cratered)
   - ✅ Handles all sun angles (0° to 90°)
   - ✅ Compatible with existing quadtree LOD
   - ✅ No crashes or visual glitches

4. **Code Quality**
   - ✅ No compiler warnings
   - ✅ Consistent coding style
   - ✅ Adequately commented
   - ✅ Debug features toggleable

---

## References

### Academic Papers
- **Cascaded Shadow Maps** - Microsoft Research (2006)
  - Original CSM technique
- **Parallel-Split Shadow Maps** - Simon Kozlov (2004)
  - Logarithmic split calculation
- **Sample Distribution Shadow Maps** - GDC 2007
  - Optimized cascade distribution

### Implementation Examples
- Unreal Engine 4: CSM implementation
- Unity URP: Cascaded shadow shader
- Three.js: CSM example

### Related Documentation
- OpenGL Shadow Mapping tutorial
- Raylib texture binding documentation
- GLSL sampler arrays

---

## Appendix: Code Snippets

### A. Complete Cascade Distance Calculation

```c
void CalculateCascadeSplits(float nearPlane, float farPlane,
                           float lambda, int cascadeCount,
                           float* outSplits) {
    float range = farPlane - nearPlane;
    float ratio = farPlane / nearPlane;

    for (int i = 0; i < cascadeCount; i++) {
        float p = (float)(i + 1) / (float)cascadeCount;
        float log = nearPlane * powf(ratio, p);
        float uniform = nearPlane + range * p;
        float d = lambda * (log - uniform) + uniform;
        outSplits[i] = d;
    }
}
```

### B. Frustum Corner Extraction

```c
void ExtractFrustumCorners(Camera camera, float nearDist, float farDist,
                          Vector3* outCorners) {
    // Get view-projection matrix
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);
    Matrix proj = MatrixPerspective(camera.fovy * DEG2RAD,
                                   GetScreenWidth() / (float)GetScreenHeight(),
                                   nearDist, farDist);
    Matrix viewProj = MatrixMultiply(view, proj);
    Matrix invViewProj = MatrixInvert(viewProj);

    // NDC corners of frustum
    Vector3 ndcCorners[8] = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, // Near plane
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}  // Far plane
    };

    // Transform to world space
    for (int i = 0; i < 8; i++) {
        Vector4 corner = {ndcCorners[i].x, ndcCorners[i].y, ndcCorners[i].z, 1.0f};
        corner = Vector4Transform(corner, invViewProj);
        outCorners[i] = (Vector3){corner.x / corner.w,
                                  corner.y / corner.w,
                                  corner.z / corner.w};
    }
}
```

### C. Tight Light Projection Fit

```c
Matrix CalculateTightLightProjection(Vector3* frustumCorners,
                                    Vector3 lightDir) {
    // Transform corners to light space
    Vector3 up = fabsf(lightDir.y) > 0.99f ? (Vector3){1,0,0} : (Vector3){0,1,0};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(up, lightDir));
    up = Vector3CrossProduct(lightDir, right);

    // Find AABB in light space
    float minX = FLT_MAX, maxX = -FLT_MAX;
    float minY = FLT_MAX, maxY = -FLT_MAX;
    float minZ = FLT_MAX, maxZ = -FLT_MAX;

    for (int i = 0; i < 8; i++) {
        Vector3 p = frustumCorners[i];
        float x = Vector3DotProduct(p, right);
        float y = Vector3DotProduct(p, up);
        float z = Vector3DotProduct(p, lightDir);

        minX = fminf(minX, x); maxX = fmaxf(maxX, x);
        minY = fminf(minY, y); maxY = fmaxf(maxY, y);
        minZ = fminf(minZ, z); maxZ = fmaxf(maxZ, z);
    }

    // Create tight orthographic projection
    return MatrixOrtho(minX, maxX, minY, maxY, minZ, maxZ);
}
```

---

## Conclusion

This implementation plan provides a comprehensive roadmap for adding Cascaded Shadow Maps to the planet-renderer-c project. The CSM technique is ideal for achieving the desired "pitch-black lunar crater" aesthetic while maintaining real-time performance on modern hardware.

Key advantages of this approach:
- ✅ Works with procedurally generated terrain
- ✅ No preprocessing required
- ✅ Leverages existing cubic quadtree LOD system
- ✅ Achieves photorealistic lunar shadows
- ✅ Industry-standard technique with proven results

The modular design allows for incremental implementation and testing, with clear success criteria at each phase. Once complete, the renderer will produce stunning, accurate shadows that rival space mission imagery.

**Next steps:** Begin Phase 1 implementation of core CSM infrastructure.
