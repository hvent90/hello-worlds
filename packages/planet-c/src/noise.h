#ifndef NOISE_H
#define NOISE_H

float noise_simplex(float x, float y);
float noise_fbm(float x, float z, int octaves, float lacunarity, float gain);
float noise_crater(float x, float z);
float evaluate_moon_noise(float x, float z);

#endif
