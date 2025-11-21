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

// Get pseudo-random value at integer coordinate
static float randomValue(int x, int y) {
    unsigned int h = hash2D(x, y);
    return (float)(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
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

// Crater-like function (inverted ridged noise)
float CraterNoise(float x, float y, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        float n = fabsf(Noise2D(x * frequency, y * frequency));
        n = 1.0f - n; // Invert
        n = sqrtf(n); // Soften for gentle crater bowls
        total += n * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return total / maxValue;
}

// Moon-like terrain combining FBM and craters
float MoonTerrain(float x, float y) {
    // Base rough terrain (dominant feature - gentle rolling hills)
    float base = FBM(x * 0.5f, y * 0.5f, 4, 0.5f, 2.0f);

    // Crater features (subtle, lower frequency)
    float craters = CraterNoise(x * 0.8f, y * 0.8f, 2);

    // Fine detail
    float detail = FBM(x * 4.0f, y * 4.0f, 2, 0.3f, 2.5f) * 0.3f;

    // Combine: gentle rolling terrain is dominant (60%), with subtle craters (25%)
    return base * 0.6f + craters * 0.25f + detail;
}
