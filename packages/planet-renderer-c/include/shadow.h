#ifndef SHADOW_H
#define SHADOW_H

#include "raylib.h"
#include "raymath.h"

#define CASCADE_COUNT 4

typedef struct {
    RenderTexture2D shadowMap;
    Matrix lightSpaceMatrix;
    float splitDistance;
    BoundingBox bounds;
} ShadowCascade;

typedef struct {
    ShadowCascade cascades[CASCADE_COUNT];
    Vector3 lightDirection;
    int shadowMapResolution;
    float cascadeSplitLambda;
    float nearPlane;
    float farPlane;
} CascadedShadowMap;

// Initialization
CascadedShadowMap* CSM_Create(Vector3 lightDir, int resolution);
void CSM_Destroy(CascadedShadowMap* csm);

// Per-frame update
void CSM_UpdateCascades(CascadedShadowMap* csm, Camera camera, float planetRadius, float terrainAmplitude);

// Utility

#endif // SHADOW_H
