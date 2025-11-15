# Planet Rendering Algorithm - Language-Agnostic Implementation Guide

## Overview
This algorithm generates a spherical planet mesh using a cubic projection approach with level-of-detail (LOD) support and edge skirts to prevent seams between different LOD levels.

---

## Core Concepts

### 1. Cubic Sphere Projection
The planet is rendered as a cube mapped onto a sphere. Each of the 6 cube faces becomes a separate quadrant of the sphere, with its own transformation matrix.

### 2. Coordinate System
- **Local Space**: Flat 2D grid positions before sphere projection
- **Sphere Space**: Positions projected onto the sphere surface
- **World Space**: Final positions after applying the cube face transformation matrix

### 3. Edge Skirts
Edge vertices are duplicated and pulled downward to prevent gaps when adjacent chunks have different LOD levels.

---

## Algorithm Pipeline

The chunk generation process follows these exact steps:

### Step 1: Generate Initial Heights

#### Constants and Setup
```
effectiveResolution = resolution  // The actual grid resolution
gridResolution = resolution + 2   // Includes 1-cell border on each side (skirt)
vertexCount = (gridResolution + 1) × (gridResolution + 1)

half = width / 2
```

**CRITICAL**: The vertex grid has dimensions `(gridResolution + 1) × (gridResolution + 1)`, NOT `gridResolution × gridResolution`

#### Vertex Generation Loop
Loop from `x = -1` to `x = effectiveResolution + 1` (inclusive):
  Loop from `y = -1` to `y = effectiveResolution + 1` (inclusive):

    // Step 1: Calculate normalized grid position
    xp = (width × x) / effectiveResolution
    yp = (width × y) / effectiveResolution

    // Step 2: Create position on flat plane at radius distance
    P = Vector3(xp - half, yp - half, radius)
    P = P + offset

    // Step 3: Normalize to get direction vector (this is the "up" vector)
    D = normalize(P)

    // Step 4: Scale to sphere surface
    P = D × radius
    P.z = P.z - radius  // Offset Z back to origin

    // Step 5: Transform to world space
    W = P × worldMatrix

    // Step 6: Get height from height generator function
    height = heightGenerator(W, radius, userData)

    // Step 7: Get color from color generator function
    color = colorGenerator(W, height, userData)

    // Step 8: Apply height displacement along direction vector
    H = D × height × (inverted ? -1 : 1)
    P = P + H

    // Step 9: Store results
    positions[vertexIndex] = P      // 3 components: x, y, z
    colors[vertexIndex] = color     // 4 components: r, g, b, a
    coords[vertexIndex] = W + H     // 3 components (used for UVs/texturing)
    up[vertexIndex] = D             // 3 components: normalized direction

**Note**: The loops go from -1 to effectiveResolution + 1, creating a grid that is `(effectiveResolution + 3) × (effectiveResolution + 3)` in iteration count, which produces `(effectiveResolution + 3) × (effectiveResolution + 3)` vertices.

---

### Step 2: Generate Triangle Indices

Creates triangle indices for a grid mesh:

```
effectiveResolution = resolution + 2
indexCount = effectiveResolution × effectiveResolution × 6

for i from 0 to effectiveResolution - 1:
  for j from 0 to effectiveResolution - 1:

    // First triangle of the quad
    indices.push(i × (effectiveResolution + 1) + j)
    indices.push((i + 1) × (effectiveResolution + 1) + j + 1)
    indices.push(i × (effectiveResolution + 1) + j + 1)

    // Second triangle of the quad
    indices.push((i + 1) × (effectiveResolution + 1) + j)
    indices.push((i + 1) × (effectiveResolution + 1) + j + 1)
    indices.push(i × (effectiveResolution + 1) + j)
```

**Key Formula**: Vertex at grid position (i, j) has index: `i × (effectiveResolution + 1) + j`

---

### Step 3: Generate Normals

Calculates vertex normals by accumulating face normals:

```
// Initialize normals array to all zeros
normals = array of size (vertexCount × 3) filled with 0.0

// Process each triangle
for i from 0 to indexCount - 1, step by 3:
  i1 = indices[i] × 3      // First vertex index (times 3 for xyz)
  i2 = indices[i + 1] × 3  // Second vertex index
  i3 = indices[i + 2] × 3  // Third vertex index

  // Get the three vertex positions
  N1 = Vector3(positions[i1], positions[i1 + 1], positions[i1 + 2])
  N2 = Vector3(positions[i2], positions[i2 + 1], positions[i2 + 2])
  N3 = Vector3(positions[i3], positions[i3 + 1], positions[i3 + 2])

  // Calculate edge vectors FROM vertex 2
  D1 = N3 - N2
  D2 = N1 - N2

  // Calculate face normal (cross product)
  faceNormal = cross(D1, D2)

  // Accumulate this face normal to all three vertices
  normals[i1]     += faceNormal.x
  normals[i2]     += faceNormal.x
  normals[i3]     += faceNormal.x

  normals[i1 + 1] += faceNormal.y
  normals[i2 + 1] += faceNormal.y
  normals[i3 + 1] += faceNormal.y

  normals[i1 + 2] += faceNormal.z
  normals[i2 + 2] += faceNormal.z
  normals[i3 + 2] += faceNormal.z
```

**CRITICAL**: The edge vectors are calculated from N2 (the second vertex), not N1. The order matters for correct normal direction.

---

### Step 4: Fix Edge Skirts

This is the most complex step. Edge vertices are repositioned to create a "skirt" that hangs down below the surface.

```
effectiveResolution = resolution + 2

// Helper function to apply skirt fix
ApplyFix(x, y, proxyX, proxyY):
  skirtIndex = x × (effectiveResolution + 1) + y
  proxyIndex = proxyX × (effectiveResolution + 1) + proxyY

  // Get the position of the proxy (inner) vertex
  P = Vector3(positions[proxyIndex × 3],
              positions[proxyIndex × 3 + 1],
              positions[proxyIndex × 3 + 2])

  // Get the up direction from the proxy vertex
  D = Vector3(up[proxyIndex × 3],
              up[proxyIndex × 3 + 1],
              up[proxyIndex × 3 + 2])

  // Calculate skirt size (clamped to prevent extreme values)
  skirtSize = clamp(width, 0, radius / 5)

  // Pull the vertex down (or up if inverted)
  D = D × (inverted ? skirtSize : -skirtSize)
  P = P + D

  // Update the skirt vertex position
  positions[skirtIndex × 3]     = P.x
  positions[skirtIndex × 3 + 1] = P.y
  positions[skirtIndex × 3 + 2] = P.z

  // Copy the normal from the proxy vertex
  normals[skirtIndex × 3]     = normals[proxyIndex × 3]
  normals[skirtIndex × 3 + 1] = normals[proxyIndex × 3 + 1]
  normals[skirtIndex × 3 + 2] = normals[proxyIndex × 3 + 2]

// Apply fix to all four edges
// Top edge (y = 0): copy from y = 1
for y from 0 to effectiveResolution:
  ApplyFix(0, y, 1, y)

// Bottom edge (y = effectiveResolution): copy from y = effectiveResolution - 1
for y from 0 to effectiveResolution:
  ApplyFix(effectiveResolution, y, effectiveResolution - 1, y)

// Left edge (x = 0): copy from x = 1
for x from 0 to effectiveResolution:
  ApplyFix(x, 0, x, 1)

// Right edge (x = effectiveResolution): copy from x = effectiveResolution - 1
for x from 0 to effectiveResolution:
  ApplyFix(x, effectiveResolution, x, effectiveResolution - 1)
```

**Key Insight**: Edge vertices copy their position from the adjacent inner vertex, then offset downward. This creates a vertical "skirt" that hangs below the chunk edges.

**Why this works**: When two adjacent chunks at different LOD levels meet, the skirt hangs down far enough to hide any gaps.

---

### Step 5: Normalize Normals

Finally, normalize all the accumulated normals:

```
for i from 0 to vertexCount - 1:
  N = Vector3(normals[i × 3], normals[i × 3 + 1], normals[i × 3 + 2])
  N = normalize(N)
  normals[i × 3]     = N.x
  normals[i × 3 + 1] = N.y
  normals[i × 3 + 2] = N.z
```

---

## Common Implementation Pitfalls

### 1. Off-by-One Errors in Loop Bounds
❌ **Wrong**: `for x from -1 to effectiveResolution`
✅ **Correct**: `for x from -1 to effectiveResolution + 1`

The loop must include the extra row/column at the end for proper grid coverage.

### 2. Incorrect Vertex Count
❌ **Wrong**: `vertexCount = resolution × resolution`
❌ **Wrong**: `vertexCount = (resolution + 2) × (resolution + 2)`
✅ **Correct**: `vertexCount = (resolution + 3) × (resolution + 3)`

Because effectiveResolution = resolution, and the loop goes from -1 to effectiveResolution + 1, you get (effectiveResolution + 3) iterations, not (effectiveResolution + 2).

### 3. Incorrect Skirt Implementation
❌ **Wrong**: Simply offsetting all edge vertices downward
✅ **Correct**: Copy position from inner proxy vertex, THEN offset downward

The proxy-based approach ensures the skirt starts at the correct height before being pulled down.

### 4. Wrong Normal Calculation Order
❌ **Wrong**: `D1 = N2 - N1; D2 = N3 - N1; cross(D1, D2)`
✅ **Correct**: `D1 = N3 - N2; D2 = N1 - N2; cross(D1, D2)`

The edge vectors must originate from vertex 2 (N2), not vertex 1.

### 5. Index Calculation Errors
The formula for accessing a vertex at grid position (x, y) is:
```
index = x × (effectiveResolution + 1) + y
```
NOT `x × effectiveResolution + y`

---

## Verification Checklist

Use this checklist to verify your implementation:

- [ ] Vertex count = `(effectiveResolution + 3) × (effectiveResolution + 3)`
- [ ] Position loop goes from -1 to `effectiveResolution + 1` (inclusive on both ends)
- [ ] Both x AND y loops use the same bounds
- [ ] Sphere projection: normalize → scale by radius → offset Z
- [ ] Height applied along normalized direction vector
- [ ] Normal calculation: edges from vertex 2, cross product of (N3-N2) × (N1-N2)
- [ ] Edge skirt: copy from proxy, THEN offset by skirt size
- [ ] Skirt size clamped to `min(width, radius / 5)`
- [ ] All four edges processed in skirt fix
- [ ] Normals normalized after skirt fix
- [ ] Index formula uses `(effectiveResolution + 1)` not `effectiveResolution`

---

## Example Values

For `resolution = 32`:
- `effectiveResolution = 32`
- `gridResolution = 34` (adds 2 for skirt)
- `vertexCount = 35 × 35 = 1,225`
- Loop ranges: `x ∈ [-1, 33]`, `y ∈ [-1, 33]`
- Index for vertex at (x, y): `x × 35 + y`

---

## Cubic Quadtree LOD System

The planet uses 6 quadtrees (one per cube face) with the following transforms:

1. **+Y face**: Rotate -90° around X, translate (0, radius, 0)
2. **-Y face**: Rotate +90° around X, translate (0, -radius, 0)
3. **+X face**: Rotate +90° around Y, translate (radius, 0, 0)
4. **-X face**: Rotate -90° around Y, translate (-radius, 0, 0)
5. **+Z face**: No rotation, translate (0, 0, radius)
6. **-Z face**: Rotate 180° around Y, translate (0, 0, -radius)

Each quadtree recursively subdivides based on distance to the camera, with minimum node size = `minCellSize`.

---

## Memory Layout

Arrays are organized as follows:

**Positions** (3 floats per vertex):
```
[x0, y0, z0, x1, y1, z1, x2, y2, z2, ...]
```

**Normals** (3 floats per vertex):
```
[nx0, ny0, nz0, nx1, ny1, nz1, nx2, ny2, nz2, ...]
```

**Colors** (4 floats per vertex):
```
[r0, g0, b0, a0, r1, g1, b1, a1, ...]
```

**Indices** (unsigned integers):
```
[i0, i1, i2, i3, i4, i5, ...]  // Groups of 3 form triangles
```

---

## Summary

The algorithm transforms a 2D grid into a spherical chunk through these transformations:
1. Flat grid → Projected onto sphere surface
2. Height displacement → Applied along radial direction
3. Transformed to world space → Using cube face matrix
4. Normals calculated → From triangle faces
5. Edge skirts added → To prevent LOD seams
6. Normals normalized → For correct lighting

The key to correct implementation is paying careful attention to:
- Loop bounds (inclusive on both ends)
- Vertex count calculation (include the +1)
- Skirt implementation (proxy-based)
- Normal calculation order (edges from vertex 2)
