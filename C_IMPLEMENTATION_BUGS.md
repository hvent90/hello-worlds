# C Planet Renderer - Bug Analysis

## Critical Bugs Found

Comparing the C implementation in `packages/planet-renderer-c/src/chunk.c` with the TypeScript reference implementation, the following critical bugs were identified:

---

## Bug 1: Incorrect Vertex Count Calculation
**Location**: `chunk.c:14`

**Current (Wrong)**:
```c
int vertexCount = resolution * resolution;
```

**Should Be**:
```c
int vertexCount = (resolution + 3) * (resolution + 3);
```

**Explanation**:
- The loops go from `-1` to `effectiveResolution + 1` (where effectiveResolution = resolution)
- This means the loop iterates `(resolution + 3)` times in each dimension
- Total vertices = `(resolution + 3) × (resolution + 3)`

**Impact**: Memory corruption, buffer overruns, crashes

---

## Bug 2: Incorrect Loop Bounds
**Location**: `chunk.c:22-24`

**Current (Wrong)**:
```c
for (int x = -1; x <= effectiveResolution; x++) {
    float xp = (props.width * x) / effectiveResolution;
    for (int y = -1; y <= effectiveResolution; y++) {
```

**Should Be**:
```c
for (int x = -1; x <= effectiveResolution + 1; x++) {
    float xp = (props.width * x) / effectiveResolution;
    for (int y = -1; y <= effectiveResolution + 1; y++) {
```

**Explanation**:
The loops must go from `-1` to `effectiveResolution + 1` (inclusive), not just to `effectiveResolution`.

**Impact**: Missing vertices on the edges, incomplete mesh, visual artifacts

---

## Bug 3: Completely Wrong Edge Skirt Implementation
**Location**: `chunk.c:136-157`

**Current (Wrong)**:
```c
void FixEdgeSkirts(int resolution, float* positions, float* ups, float* normals,
                  float width, float radius, bool inverted) {
    int effectiveResolution = resolution + 2;
    float skirtDepth = width * 0.1f;

    for (int i = 0; i < effectiveResolution + 1; i++) {
        for (int j = 0; j < effectiveResolution + 1; j++) {
            bool isEdge = (i == 0 || i == effectiveResolution || j == 0 || j == effectiveResolution);

            if (isEdge) {
                int idx = i * (effectiveResolution + 1) + j;
                Vector3 up = {ups[idx * 3 + 0], ups[idx * 3 + 1], ups[idx * 3 + 2]};
                Vector3 offset = Vector3Scale(up, -skirtDepth * (inverted ? -1.0f : 1.0f));

                positions[idx * 3 + 0] += offset.x;
                positions[idx * 3 + 1] += offset.y;
                positions[idx * 3 + 2] += offset.z;
            }
        }
    }
}
```

**Should Be** (following TypeScript implementation):
```c
void FixEdgeSkirts(int resolution, float* positions, float* ups, float* normals,
                  float width, float radius, bool inverted) {
    int effectiveResolution = resolution + 2;

    // Helper function to apply fix
    void ApplyFix(int x, int y, int proxyX, int proxyY) {
        int skirtIndex = x * (effectiveResolution + 1) + y;
        int proxyIndex = proxyX * (effectiveResolution + 1) + proxyY;

        // Copy position from proxy vertex
        Vector3 P = {
            positions[proxyIndex * 3 + 0],
            positions[proxyIndex * 3 + 1],
            positions[proxyIndex * 3 + 2]
        };

        // Get up vector from proxy
        Vector3 D = {
            ups[proxyIndex * 3 + 0],
            ups[proxyIndex * 3 + 1],
            ups[proxyIndex * 3 + 2]
        };

        // Clamp skirt size
        float skirtSize = fminf(width, radius / 5.0f);
        if (skirtSize < 0) skirtSize = 0;

        // Offset position
        D = Vector3Scale(D, inverted ? skirtSize : -skirtSize);
        P = Vector3Add(P, D);

        // Update skirt vertex
        positions[skirtIndex * 3 + 0] = P.x;
        positions[skirtIndex * 3 + 1] = P.y;
        positions[skirtIndex * 3 + 2] = P.z;

        // Copy normals from proxy
        normals[skirtIndex * 3 + 0] = normals[proxyIndex * 3 + 0];
        normals[skirtIndex * 3 + 1] = normals[proxyIndex * 3 + 1];
        normals[skirtIndex * 3 + 2] = normals[proxyIndex * 3 + 2];
    }

    // Fix all four edges
    for (int y = 0; y <= effectiveResolution; y++) {
        ApplyFix(0, y, 1, y);  // Left edge
    }
    for (int y = 0; y <= effectiveResolution; y++) {
        ApplyFix(effectiveResolution, y, effectiveResolution - 1, y);  // Right edge
    }
    for (int x = 0; x <= effectiveResolution; x++) {
        ApplyFix(x, 0, x, 1);  // Top edge
    }
    for (int x = 0; x <= effectiveResolution; x++) {
        ApplyFix(x, effectiveResolution, x, effectiveResolution - 1);  // Bottom edge
    }
}
```

**Explanation**:
The current implementation simply offsets ALL edge vertices, which is incorrect. The correct approach:
1. Copy the position from the adjacent inner vertex (the "proxy")
2. THEN pull that position down/up by the skirt size
3. Also copy the normal from the proxy vertex

This ensures the skirt maintains the correct topology and prevents gaps between LOD levels.

**Impact**: Visible gaps between chunks at different LOD levels, the primary visual bug

---

## Bug 4: Incorrect Normal Calculation
**Location**: `chunk.c:116-118`

**Current (Wrong)**:
```c
Vector3 edge1 = Vector3Subtract(v1, v0);  // v1 - v0
Vector3 edge2 = Vector3Subtract(v2, v0);  // v2 - v0
Vector3 normal = Vector3CrossProduct(edge1, edge2);
```

**Should Be**:
```c
Vector3 edge1 = Vector3Subtract(v2, v1);  // v2 - v1  (corresponds to N3 - N2)
Vector3 edge2 = Vector3Subtract(v0, v1);  // v0 - v1  (corresponds to N1 - N2)
Vector3 normal = Vector3CrossProduct(edge1, edge2);
```

**Explanation**:
The TypeScript version calculates edges from vertex 2 (the middle vertex in the triangle), not vertex 0:
```typescript
_D1.subVectors(_N3, _N2)  // Third vertex - Second vertex
_D2.subVectors(_N1, _N2)  // First vertex - Second vertex
_D1.cross(_D2)
```

**Impact**: Incorrect lighting/shading, normals may point in wrong direction

---

## Bug 5: Missing Skirt Size Clamping
**Location**: `chunk.c:139`

**Current (Wrong)**:
```c
float skirtDepth = width * 0.1f;
```

**Should Be**:
```c
float skirtSize = fminf(width, radius / 5.0f);
if (skirtSize < 0) skirtSize = 0;
```

**Explanation**:
The TypeScript version uses:
```typescript
const skirtSize = MathUtils.clamp(width, 0, radius / 5)
```

This prevents the skirt from being too large when chunks are big relative to planet radius, which causes "crazy spikes" (as noted in the TypeScript comment).

**Impact**: Visual artifacts (spikes) on large chunks

---

## Priority of Fixes

1. **CRITICAL**: Bug 1 (Vertex Count) - Causes crashes
2. **CRITICAL**: Bug 2 (Loop Bounds) - Causes incomplete mesh
3. **CRITICAL**: Bug 3 (Edge Skirts) - Causes the visible gaps (main issue)
4. **HIGH**: Bug 4 (Normals) - Causes incorrect lighting
5. **MEDIUM**: Bug 5 (Skirt Clamping) - Causes visual artifacts on large chunks

---

## Testing After Fixes

After applying these fixes:
1. Verify no crashes with various resolution values (8, 16, 32, 64)
2. Check that no gaps appear between chunks at different LOD levels
3. Verify smooth lighting across chunk boundaries
4. Test with both `inverted = true` and `inverted = false`
5. Test with various planet radii and chunk widths
