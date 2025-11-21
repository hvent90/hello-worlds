#include "noise.h"
#include <math.h>

// Hash function for pseudo-random values
static unsigned int hash(unsigned int x) {
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

static unsigned int hash2D(int x, int y) {
    return hash(x ^ hash(y));
}

// Smooth interpolation (smoothstep)
static float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Linear interpolation
static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Clamp value
static float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// Get pseudo-random value at integer coordinate
static float randomValue(int x, int y) {
    unsigned int h = hash2D(x, y);
    return (float)(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
}

// Get pseudo-random value in [0, 1] range
static float randomValue01(int x, int y) {
    unsigned int h = hash2D(x, y);
    return (float)(h & 0xFFFF) / 65535.0f;
}

// Value noise implementation
float Noise2D(float x, float y) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = x - x0;
    float fy = y - y0;

    // Smooth interpolation
    float sx = smoothstep(fx);
    float sy = smoothstep(fy);

    // Get corner values
    float v00 = randomValue(x0, y0);
    float v10 = randomValue(x1, y0);
    float v01 = randomValue(x0, y1);
    float v11 = randomValue(x1, y1);

    // Interpolate
    float v0 = lerp(v00, v10, sx);
    float v1 = lerp(v01, v11, sx);

    return lerp(v0, v1, sy);
}

// Fractional Brownian Motion - multiple octaves of noise
float FBM(float x, float y, int octaves, float persistence, float lacunarity) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += Noise2D(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

// Worley/Cellular noise for crater placement
// Returns (distance to nearest point, distance to second nearest)
static void WorleyNoise(float x, float y, float* f1, float* f2) {
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);

    float minDist1 = 10000.0f;
    float minDist2 = 10000.0f;

    // Check neighboring cells (3x3 grid)
    for (int yOffset = -1; yOffset <= 1; yOffset++) {
        for (int xOffset = -1; xOffset <= 1; xOffset++) {
            int cellX = xi + xOffset;
            int cellY = yi + yOffset;

            // Get random point position within this cell
            float pointX = cellX + randomValue01(cellX, cellY);
            float pointY = cellY + randomValue01(cellX, cellY + 1000);

            // Calculate distance
            float dx = x - pointX;
            float dy = y - pointY;
            float dist = sqrtf(dx * dx + dy * dy);

            // Update minimum distances
            if (dist < minDist1) {
                minDist2 = minDist1;
                minDist1 = dist;
            } else if (dist < minDist2) {
                minDist2 = dist;
            }
        }
    }

    *f1 = minDist1;
    *f2 = minDist2;
}

// Realistic crater profile: bowl-shaped with raised rim
// distance: normalized distance from crater center (0 to 1+)
// Returns height multiplier
static float CraterProfile(float distance) {
    if (distance > 1.2f) return 0.0f; // Beyond ejecta blanket

    if (distance < 0.95f) {
        // Bowl interior: parabolic shape
        // Depth is maximum at center, rises toward rim
        float normalized = distance / 0.95f;
        float bowlDepth = 1.0f - (normalized * normalized); // Parabolic
        return -bowlDepth; // Negative = depression
    } else if (distance < 1.05f) {
        // Raised rim (2-5% of crater diameter in height)
        // Peak at distance = 1.0
        float rimPos = (distance - 0.95f) / 0.1f; // 0 to 1 across rim
        float rimHeight = sinf(rimPos * 3.14159f); // Smooth peak
        return rimHeight * 0.3f; // Rim is 30% height of bowl depth
    } else {
        // Ejecta blanket: gentle falloff
        float ejectaDist = (distance - 1.05f) / 0.15f; // 0 to 1 across ejecta
        return (1.0f - ejectaDist) * 0.1f; // Gentle slope down
    }
}

// Multi-scale crater system
float CraterField(float x, float y, float scale, float intensity) {
    float f1, f2;
    WorleyNoise(x * scale, y * scale, &f1, &f2);

    // Get crater size from cell (varies per crater)
    int cellX = (int)floorf(x * scale);
    int cellY = (int)floorf(y * scale);
    float craterSize = 0.3f + randomValue01(cellX, cellY) * 0.4f; // 0.3 to 0.7

    // Calculate distance from crater center as ratio of crater size
    float normalizedDist = f1 / craterSize;

    // Apply crater profile
    float craterHeight = CraterProfile(normalizedDist);

    // Depth scales with crater size (larger craters are proportionally shallower)
    // Simple craters: ~12-20% depth-to-diameter ratio
    float depthRatio = 0.18f - craterSize * 0.05f; // Larger = shallower ratio

    return craterHeight * depthRatio * intensity;
}

// Wrinkle ridges (compressional features in maria)
float WrinkleRidges(float x, float y) {
    // Use ridged noise for linear ridge patterns
    float n1 = fabsf(Noise2D(x * 0.3f, y * 0.3f));
    float n2 = fabsf(Noise2D(x * 0.5f + 100.0f, y * 0.5f + 100.0f));

    // Invert to create ridges
    float ridges = (1.0f - n1) * 0.6f + (1.0f - n2) * 0.4f;

    // Sharpen ridges
    ridges = powf(ridges, 2.5f);

    return ridges * 0.15f; // Height: 50-300m range (scaled later)
}

// Highlands vs Maria determination
float MariaPattern(float x, float y) {
    // Large-scale pattern to define maria (basalt plains)
    // Maria are lower, smoother regions
    float largeScale = FBM(x * 0.08f, y * 0.08f, 3, 0.5f, 2.0f);

    // Threshold to create distinct maria regions
    // Returns 0 for highlands, 1 for maria
    float maria = smoothstep(clamp((largeScale + 0.3f) / 0.6f, 0.0f, 1.0f));

    return maria;
}

// Moon-like terrain with realistic lunar features
float MoonTerrain(float x, float y) {
    // 1. Determine if this is maria or highlands
    float mariaAmount = MariaPattern(x, y);
    float highlandAmount = 1.0f - mariaAmount;

    // 2. Base elevation difference (maria are 1-3 km lower than highlands)
    // Normalized to [-1, 1] range, this is a major component
    float baseElevation = highlandAmount * 0.3f - mariaAmount * 0.3f;

    // 3. Large impact basins (rare, very large features)
    float largeCraters = CraterField(x, y, 0.15f, 1.8f); // Huge basins (>200km scale)

    // 4. Complex craters (20-200 km)
    float complexCraters = CraterField(x, y, 0.5f, 1.2f);

    // 5. Simple craters (1-20 km) - more numerous in highlands
    float simpleCraters = CraterField(x, y, 2.0f, 0.8f) * (0.5f + highlandAmount * 0.5f);

    // 6. Small craters (<1 km) - dense in highlands
    float smallCraters = CraterField(x, y, 8.0f, 0.4f) * (0.3f + highlandAmount * 0.7f);

    // 7. Wrinkle ridges (only in maria)
    float ridges = WrinkleRidges(x, y) * mariaAmount;

    // 8. Local roughness (different for maria vs highlands)
    // Highlands: 100-500m rough
    float highlandRoughness = FBM(x * 5.0f, y * 5.0f, 3, 0.6f, 2.0f) * 0.2f * highlandAmount;

    // Maria: 3-10m smooth with occasional texture
    float mariaRoughness = FBM(x * 15.0f, y * 15.0f, 2, 0.3f, 2.0f) * 0.02f * mariaAmount;

    // 9. Regolith texture (very fine, everywhere)
    float regolith = FBM(x * 30.0f, y * 30.0f, 2, 0.2f, 2.5f) * 0.01f;

    // Combine all features
    float terrain = baseElevation
                  + largeCraters * 1.0f
                  + complexCraters * 0.8f
                  + simpleCraters * 0.6f
                  + smallCraters * 0.3f
                  + ridges
                  + highlandRoughness
                  + mariaRoughness
                  + regolith;

    return terrain;
}
