#ifndef NOISE_H
#define NOISE_H

float noise_simplex(float x, float y);
float noise_fbm(float x, float z, int octaves, float lacunarity, float gain);
float noise_crater(float x, float z);
float evaluate_moon_noise(float x, float z);

// 3D noise functions for seamless sphere noise (uses normalized direction as input)
float noise_simplex_3d(float x, float y, float z);
float noise_fbm_3d(float x, float y, float z, int octaves, float lacunarity, float gain);
float evaluate_moon_noise_3d(float nx, float ny, float nz);

#endif
