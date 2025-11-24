#include "noise.h"

#include <math.h>

float evaluate_noise(const float x, const float z, const float frequency, const float amplitude) {
    return sin(x * frequency) * sin(z * frequency) * amplitude;
}
