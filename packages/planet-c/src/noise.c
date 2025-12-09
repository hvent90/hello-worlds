#include "noise.h"
#include <math.h>
#include <stdlib.h>

// --------------------------------------------------------------------------------
// Simplex Noise Implementation (2D)
// --------------------------------------------------------------------------------

// Permutation table
static const int perm[512] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,
    225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,  190,
    6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117,
    35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136,
    171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158,
    231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,
    245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209,
    76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159, 86,
    164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123, 5,
    202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,
    58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,
    154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,  253,
    19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,
    228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,
    145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184,
    84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,
    222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156,
    180, 151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233,
    7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,
    190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203,
    117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125,
    136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146,
    158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,
    46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,
    209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159,
    86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123,
    5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,
    16,  58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,
    44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,
    253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246,
    97,  228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,
    51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157,
    184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205,
    93,  222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,
    156, 180};

static float grad(int hash, float x, float y) {
  int h = hash & 7;        // Convert low 3 bits of hash code
  float u = h < 4 ? x : y; // into 8 simple gradient directions,
  float v = h < 4 ? y : x; // and compute the dot product with (x,y).
  return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

float noise_simplex(float x, float y) {
  const float F2 = 0.366025403f; // 0.5*(sqrt(3.0)-1.0)
  const float G2 = 0.211324865f; // (3.0-sqrt(3.0))/6.0

  float n0, n1, n2; // Noise contributions from the three corners

  // Skew the input space to determine which simplex cell we're in
  float s = (x + y) * F2; // Hairy factor for 2D
  int i = (int)floorf(x + s);
  int j = (int)floorf(y + s);

  float t = (float)(i + j) * G2;
  float X0 = i - t; // Unskew the cell origin back to (x,y) space
  float Y0 = j - t;
  float x0 = x - X0; // The x,y distances from the cell origin
  float y0 = y - Y0;

  // For the 2D case, the simplex shape is an equilateral triangle.
  // Determine which simplex we are in.
  int i1, j1; // Offsets for second (middle) corner of simplex in (i,j) coords
  if (x0 > y0) {
    i1 = 1;
    j1 = 0;
  } // lower triangle, XY order: (0,0)->(1,0)->(1,1)
  else {
    i1 = 0;
    j1 = 1;
  } // upper triangle, YX order: (0,0)->(0,1)->(1,1)

  // A step of (1,0) in (i,j) means a step of (1-c,-c) in (x,y), and
  // a step of (0,1) in (i,j) means a step of (-c,1-c) in (x,y), where
  // c = (3-sqrt(3))/6

  float x1 = x0 - i1 + G2; // Offsets for middle corner in (x,y) unskewed coords
  float y1 = y0 - j1 + G2;
  float x2 =
      x0 - 1.0f + 2.0f * G2; // Offsets for last corner in (x,y) unskewed coords
  float y2 = y0 - 1.0f + 2.0f * G2;

  // Work out the hashed gradient indices of the three simplex corners
  int ii = i & 255;
  int jj = j & 255;
  int gi0 = perm[ii + perm[jj]];
  int gi1 = perm[ii + i1 + perm[jj + j1]];
  int gi2 = perm[ii + 1 + perm[jj + 1]];

  // Calculate the contribution from the three corners
  float t0 = 0.5f - x0 * x0 - y0 * y0;
  if (t0 < 0)
    n0 = 0.0f;
  else {
    t0 *= t0;
    n0 = t0 * t0 * grad(gi0, x0, y0);
  }

  float t1 = 0.5f - x1 * x1 - y1 * y1;
  if (t1 < 0)
    n1 = 0.0f;
  else {
    t1 *= t1;
    n1 = t1 * t1 * grad(gi1, x1, y1);
  }

  float t2 = 0.5f - x2 * x2 - y2 * y2;
  if (t2 < 0)
    n2 = 0.0f;
  else {
    t2 *= t2;
    n2 = t2 * t2 * grad(gi2, x2, y2);
  }

  // Add contributions from each corner to get the final noise value.
  // The result is scaled to return values in the interval [-1,1].
  return 70.0f * (n0 + n1 + n2);
}

// --------------------------------------------------------------------------------
// Fractal Brownian Motion
// --------------------------------------------------------------------------------

float noise_fbm(float x, float z, int octaves, float lacunarity, float gain) {
  float total = 0.0f;
  float frequency = 1.0f;
  float amplitude = 1.0f;
  float max_value = 0.0f; // Used for normalizing result to 0.0 - 1.0

  for (int i = 0; i < octaves; i++) {
    total += noise_simplex(x * frequency, z * frequency) * amplitude;
    max_value += amplitude;
    amplitude *= gain;
    frequency *= lacunarity;
  }

  return total / max_value;
}

// --------------------------------------------------------------------------------
// Crater Noise
// --------------------------------------------------------------------------------

// Hash function for cellular noise
static float hash2(float x, float y) {
  return noise_simplex(x * 123.45f, y * 678.90f);
}

static void get_random_point(int cx, int cy, float *px, float *py) {
  // Deterministic random point within the cell
  float h = hash2((float)cx, (float)cy);
  *px = (float)cx + 0.5f + 0.4f * sinf(h * 6.2831f);
  *py = (float)cy + 0.5f + 0.4f * cosf(h * 6.2831f);
}

float noise_crater(float x, float z) {
  // Cellular noise (Voronoi) to find crater centers
  int xi = (int)floorf(x);
  int zi = (int)floorf(z);

  float min_dist = 1.0f;
  float crater_radius = 0.0f;

  // Check 3x3 neighbor cells
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      float px, pz;
      get_random_point(xi + i, zi + j, &px, &pz);

      float dx = px - x;
      float dz = pz - z;
      float dist = sqrtf(dx * dx + dz * dz);

      // Vary crater size based on hash
      float h = hash2((float)(xi + i), (float)(zi + j));
      float radius =
          0.3f + 0.2f * (h * 0.5f + 0.5f); // Radius between 0.3 and 0.5

      if (dist < min_dist) {
        min_dist = dist;
        crater_radius = radius;
      }
    }
  }

  // Crater profile function
  // 1. Rim: rises slightly above ground
  // 2. Bowl: sinks deep below ground
  // 3. Floor: flattens out at the bottom

  float d = min_dist /
            crater_radius; // Normalized distance to center (0=center, 1=edge)

  if (d > 1.0f)
    return 0.0f; // Outside crater

  // Simple crater profile
  // x^2 - 1 shape for bowl?
  // Let's try a curve that goes up then down

  float rim_width = 0.2f;
  if (d > 1.0f - rim_width) {
    // Rim: 0 at 1.0, peaks at 1.0-rim_width/2, 0 at 1.0-rim_width
    // Simple parabolic rim
    float rd = (d - (1.0f - rim_width)) / rim_width; // 0 to 1 across rim
    return 0.2f * sinf(rd * 3.14159f);
  } else {
    // Bowl
    // Smoothstep from center to rim start
    float bd = d / (1.0f - rim_width);
    return -0.8f * (1.0f - bd * bd);
  }
}

// --------------------------------------------------------------------------------
// Moon Surface Evaluator
// --------------------------------------------------------------------------------

float evaluate_moon_noise(float x, float z) {
  // 1. Base terrain (FBM)
  float base_terrain = noise_fbm(x * 0.5f, z * 0.5f, 6, 2.0f, 0.5f);

  // 2. Large Craters
  float craters_large = noise_crater(x * 0.2f, z * 0.2f) * 2.0f;

  // 3. Small Craters
  float craters_small = noise_crater(x * 1.5f, z * 1.5f) * 0.5f;

  // 4. Maria (Dark Plains) - Low frequency noise to mask out rough terrain
  float maria_noise = noise_simplex(x * 0.1f, z * 0.1f);
  float maria_mask = (maria_noise > 0.2f) ? 1.0f : 0.0f;
  // Smooth transition for maria
  maria_mask = fmaxf(0.0f, fminf(1.0f, (maria_noise - 0.1f) * 5.0f));

  // Combine
  // Maria areas should be smoother and lower
  float final_height = 0;
  final_height += base_terrain;

  // Apply craters
  final_height += craters_large;
  final_height += craters_small;

  // Apply Maria flattening
  // If maria_mask is 1, we want flat low terrain
  // If maria_mask is 0, we want rugged terrain

  float maria_height =
      -0.5f + noise_fbm(x * 2.0f, z * 2.0f, 2, 2.0f, 0.5f) * 0.1f;

  return (final_height * (1.0f - maria_mask)) + (maria_height * maria_mask);
}
