# C Planet Renderer - Bug Analysis

## Critical Bugs Found

Comparing the C implementation in `packages/planet-renderer-c/src/chunk.c` with the TypeScript reference implementation, the following critical bugs were identified:

---

## Bug 1: CRITICAL - Vertex/Index Ordering Mismatch
**Location**: `chunk.c:23-25` (vertex generation) and `chunk.c:93` (index generation)

**Current (Wrong)**:

Vertex generation uses y as outer loop (column-major):
```c
for (int y = -1; y <= effectiveResolution + 1; y++) {
    for (int x = -1; x <= effectiveResolution + 1; x++) {
        // vertices stored sequentially with y varying in outer loop
        idx++;
    }
}
```

Index generation assumes row-major (i is row):
```c
(*outIndices)[idx++] = i * gridSize + j;
```

**Explanation**:
The vertex generation loop has `y` as the outer loop and `x` as the inner loop, which creates vertices in **column-major order** (all x values for y=-1, then all x values for y=0, etc.).

However, the index generation uses `i * gridSize + j` which assumes **row-major order** (i is the row coordinate, j is the column).

**Should Be**:

Either change vertex generation to use x as outer loop (row-major):
```c
for (int x = -1; x <= effectiveResolution + 1; x++) {
    for (int y = -1; y <= effectiveResolution + 1; y++) {
        // Now vertices are in row-major order
        idx++;
    }
}
```

OR change index calculation to match column-major:
```c
(*outIndices)[idx++] = j * gridSize + i;  // Swap i and j
```

**TypeScript Reference**:
```typescript
for (let x = -1; x <= effectiveResolution + 1; ++x) {
  for (let y = -1; y <= effectiveResolution + 1; ++y) {
    // x is outer loop (row-major)
  }
}
```

**Impact**: CATASTROPHIC - Completely scrambled mesh geometry, making the entire renderer non-functional

**Priority**: #1 CRITICAL - Must fix before any other bugs

---

## Bug 2: Incorrect Normal Calculation
**Location**: `chunk.c:120-122`

**Current (Wrong)**:
```c
Vector3 edge1 = Vector3Subtract(v1, v0);  // v1 - v0
Vector3 edge2 = Vector3Subtract(v2, v0);  // v2 - v0
Vector3 normal = Vector3CrossProduct(edge1, edge2);
```

**Should Be**:
```c
Vector3 edge1 = Vector3Subtract(v2, v1);  // v2 - v1  (N3 - N2)
Vector3 edge2 = Vector3Subtract(v0, v1);  // v0 - v1  (N1 - N2)
Vector3 normal = Vector3CrossProduct(edge1, edge2);
```

**Explanation**:
The TypeScript version calculates edges from vertex 2 (the middle vertex), not vertex 0:

```typescript
_D1.subVectors(_N3, _N2)  // Third vertex - Second vertex
_D2.subVectors(_N1, _N2)  // First vertex - Second vertex
_D1.cross(_D2)
```

Where:
- `_N1` = `v0` (first vertex of triangle)
- `_N2` = `v1` (second vertex of triangle)
- `_N3` = `v2` (third vertex of triangle)

The edge vectors must originate from the second vertex (v1/N2), not the first.

**Impact**: Incorrect normals leading to wrong lighting, potentially inverted normals causing inside-out appearance

**Priority**: #2 HIGH - Causes visual artifacts

---

## Bug 3: Incorrect Edge Skirt Implementation
**Location**: `chunk.c:146-163`

**Current (Wrong)**:
```c
void FixEdgeSkirts(int resolution, float* positions, float* ups, float* normals,
                  float width, float radius, bool inverted) {
    int effectiveResolution = resolution + 2;
    float skirtDepth = width * 0.1f;

    for (int i = 0; i <= effectiveResolution; i++) {
        for (int j = 0; j <= effectiveResolution; j++) {
            bool isEdge = (i == 0 || i == effectiveResolution ||
                          j == 0 || j == effectiveResolution);

            if (isEdge) {
                int idx = (i + 1) * gridSize + (j + 1);
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

**Problems**:
1. Simply offsets existing edge vertices instead of using proxy-based approach
2. Uses incorrect index calculation: `(i + 1) * gridSize + (j + 1)`
3. Doesn't copy normals from proxy vertices
4. Missing skirt size clamping (see Bug #4)

**Should Be** (following TypeScript implementation):
```c
void FixEdgeSkirts(int resolution, float* positions, float* ups, float* normals,
                  float width, float radius, bool inverted) {
    int effectiveResolution = resolution + 2;

    // Clamp skirt size to prevent spikes
    float skirtSize = fminf(width, radius / 5.0f);
    if (skirtSize < 0) skirtSize = 0;

    // Helper function to apply fix (can be inline or separate)
    auto ApplyFix = [&](int x, int y, int proxyX, int proxyY) {
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

        // Offset position along up/down direction
        D = Vector3Scale(D, inverted ? skirtSize : -skirtSize);
        P = Vector3Add(P, D);

        // Update skirt vertex position
        positions[skirtIndex * 3 + 0] = P.x;
        positions[skirtIndex * 3 + 1] = P.y;
        positions[skirtIndex * 3 + 2] = P.z;

        // Copy normals from proxy
        normals[skirtIndex * 3 + 0] = normals[proxyIndex * 3 + 0];
        normals[skirtIndex * 3 + 1] = normals[proxyIndex * 3 + 1];
        normals[skirtIndex * 3 + 2] = normals[proxyIndex * 3 + 2];
    };

    // Fix all four edges
    // Left edge (x = 0): copy from x = 1
    for (int y = 0; y <= effectiveResolution; y++) {
        ApplyFix(0, y, 1, y);
    }

    // Right edge (x = effectiveResolution): copy from x = effectiveResolution - 1
    for (int y = 0; y <= effectiveResolution; y++) {
        ApplyFix(effectiveResolution, y, effectiveResolution - 1, y);
    }

    // Top edge (y = 0): copy from y = 1
    for (int x = 0; x <= effectiveResolution; x++) {
        ApplyFix(x, 0, x, 1);
    }

    // Bottom edge (y = effectiveResolution): copy from y = effectiveResolution - 1
    for (int x = 0; x <= effectiveResolution; x++) {
        ApplyFix(x, effectiveResolution, x, effectiveResolution - 1);
    }
}
```

**Note**: If C doesn't support lambdas, convert `ApplyFix` to a separate function or use a macro.

**Explanation**:
The correct approach:
1. For each edge vertex, identify its adjacent inner vertex (the "proxy")
2. Copy the position from the proxy vertex
3. THEN pull that position down/up by the clamped skirt size
4. Copy the normal from the proxy vertex

This ensures the skirt maintains correct topology and prevents gaps between LOD levels.

**TypeScript Reference**:
```typescript
const _ApplyFix = (x: number, y: number, proxyX: number, proxyY: number) => {
  const skirtIndex = x * (effectiveResolution + 1) + y
  const proxyIndex = proxyX * (effectiveResolution + 1) + proxyY

  // Copy from proxy position
  _P.fromArray(positions, proxyIndex * 3)
  _D.fromArray(up, proxyIndex * 3)

  const skirtSize = MathUtils.clamp(width, 0, radius / 5)
  _D.multiplyScalar(inverted ? skirtSize : -skirtSize)
  _P.add(_D)

  _P.toArray(positions, skirtIndex * 3)

  // Copy normal from proxy
  normals[skirtIndex * 3 + 0] = normals[proxyIndex * 3 + 0]
  normals[skirtIndex * 3 + 1] = normals[proxyIndex * 3 + 1]
  normals[skirtIndex * 3 + 2] = normals[proxyIndex * 3 + 2]
}
```

**Impact**: Visible gaps and seams between chunks at different LOD levels - the primary visual bug

**Priority**: #3 HIGH - Defeats the purpose of edge skirts

---

## Bug 4: Missing Skirt Size Clamping
**Location**: `chunk.c:148`

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

Without clamping, large chunks relative to planet radius can create excessively long skirts that appear as "crazy spikes" (as noted in the TypeScript comment).

**Impact**: Visual artifacts (spikes/distortion) on large chunks or small planet radii

**Priority**: #4 MEDIUM - Causes artifacts in specific edge cases

---

## Previously Suspected Bugs - Actually Correct

### Vertex Count - NO BUG FOUND
The C code (line 15) uses:
```c
int vertexCount = resolution * resolution;
```

Where `resolution = props.resolution + 4` (line 12).

This equals `(props.resolution + 4)²`, which is **CORRECT** because the loops go from `-1` to `effectiveResolution + 1` where `effectiveResolution = props.resolution + 1`, creating `props.resolution + 4` iterations per dimension.

### Loop Bounds - NO BUG FOUND
The C code (lines 23-25) correctly uses:
```c
for (int y = -1; y <= effectiveResolution + 1; y++)
    for (int x = -1; x <= effectiveResolution + 1; x++)
```

The `<= effectiveResolution + 1` is correct and matches the TypeScript implementation.

---

## Priority of Fixes

1. **CRITICAL**: Bug 1 (Vertex/Index Ordering) - Causes complete mesh scrambling
2. **HIGH**: Bug 2 (Normals) - Causes incorrect lighting/inverted appearance
3. **HIGH**: Bug 3 (Edge Skirts) - Causes visible gaps (main LOD issue)
4. **MEDIUM**: Bug 4 (Skirt Clamping) - Causes visual artifacts on large chunks

**Fix Order**: MUST fix Bug #1 first, as it prevents the renderer from working at all. The other bugs can be fixed in any order after that.

---

## Testing After Fixes

After applying these fixes:

1. **Verify mesh topology**: Visual inspection should show proper sphere shape
2. **Check vertex ordering**: Ensure triangles are wound correctly and not scrambled
3. **Test LOD transitions**: No gaps should appear between chunks at different LOD levels
4. **Verify lighting**: Smooth shading across chunk boundaries, no sudden dark/light patches
5. **Test both modes**: Test with both `inverted = true` and `inverted = false`
6. **Test edge cases**: Various planet radii (small and large) and chunk widths
7. **Test resolutions**: Try resolution values 8, 16, 32, 64 to ensure no crashes

---

## Verification Commands

To verify the vertex/index ordering fix, you can add debug output:

```c
// After vertex generation, print first few vertices
printf("First 5 vertices:\n");
for (int i = 0; i < 5; i++) {
    printf("  v%d: (%.2f, %.2f, %.2f)\n", i,
           positions[i*3], positions[i*3+1], positions[i*3+2]);
}

// After index generation, print first triangle
printf("First triangle indices: %d, %d, %d\n",
       (*outIndices)[0], (*outIndices)[1], (*outIndices)[2]);
```

The first 5 vertices should progress along one dimension (either x or y depending on loop order), not jump around randomly.
