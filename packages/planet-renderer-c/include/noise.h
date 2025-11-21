#ifndef NOISE_H
#define NOISE_H

#include <raymath.h>

// Simple 2D value noise returning values in range [-1, 1]
float Noise2D(float x, float y);

// Fractional Brownian Motion - multiple octaves of noise
float FBM(float x, float y, int octaves, float persistence, float lacunarity);

// Crater-like noise pattern
float CraterNoise(float x, float y, int octaves);

// Moon-like terrain combining multiple noise types
float MoonTerrain(float x, float y);

#endif // NOISE_H
