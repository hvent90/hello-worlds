#ifndef SHADOW_H
#define SHADOW_H

#include "raylib.h"

#define SHADOWMAP_RESOLUTION 1024
#define CASCADE_COUNT 4

typedef struct {
    RenderTexture2D maps[CASCADE_COUNT];
    Camera3D lightCameras[CASCADE_COUNT];
    float splits[CASCADE_COUNT + 1];
    Matrix lightViewProjs[CASCADE_COUNT];
} ShadowSystem;

RenderTexture2D LoadShadowmapRenderTexture(int width, int height);

// ShadowSystem InitShadowSystem(Shader shader, Vector3 lightDir);
// void UpdateShadowCameras(ShadowSystem *sys, Camera3D camera, Vector3 lightDir, float lightViewDistance);
// void RenderShadowPass(ShadowSystem *sys, void (*drawScene)(Material), Material material);
// void BindShadowsForMainPass(ShadowSystem *sys, Shader shader);
// void UnloadShadowSystem(ShadowSystem sys);

ShadowSystem InitShadowSystem(Shader shader, Vector3 lightDir);
void UpdateShadowCameras(ShadowSystem *sys, Camera3D camera, Vector3 lightDir, float lightViewDistance);


#endif