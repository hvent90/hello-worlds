#include "shadow.h"
#include <stdlib.h>
#include <math.h>
#include <float.h>

CascadedShadowMap* CSM_Create(Vector3 lightDir, int resolution) {
    CascadedShadowMap* csm = malloc(sizeof(CascadedShadowMap));

    csm->lightDirection = Vector3Normalize(lightDir);
    csm->shadowMapResolution = resolution;
    csm->cascadeSplitLambda = 0.75f; // Favor logarithmic
    csm->nearPlane = 1.0f;
    csm->farPlane = 100000.0f; // 100km

    // Create shadow map textures
    for (int i = 0; i < CASCADE_COUNT; i++) {
        csm->cascades[i].shadowMap = LoadRenderTexture(resolution, resolution);
        csm->cascades[i].splitDistance = 0.0f;
    }

    return csm;
}

void CSM_Destroy(CascadedShadowMap* csm) {
    for (int i = 0; i < CASCADE_COUNT; i++) {
        UnloadRenderTexture(csm->cascades[i].shadowMap);
    }
    free(csm);
}


void CSM_UpdateCascades(CascadedShadowMap* csm, Camera camera, float planetRadius, float terrainAmplitude) {
    // Calculate camera altitude
    Vector3 planetCenter = {0, 0, 0};
    float distanceFromCenter = Vector3Length(camera.position);
    float altitude = distanceFromCenter - planetRadius;

    // Calculate maximum terrain displacement
    // terrainAmplitude is a fraction of radius (e.g., 0.003 = 0.3%)
    float maxTerrainHeight = terrainAmplitude * planetRadius;

    // Define cascade coverage sizes based on altitude
    // Each cascade covers progressively larger areas centered on camera
    float baseSize = 5000.0f;  // 5km at surface
    
    // Calculate altitude factor, but clamp to minimum 1.0
    // This ensures that if the user goes below "sea level" (into a crater),
    // the cascades don't shrink to tiny sizes, which would cause fallback 
    // to lower-res cascades for nearby geometry.
    float altitudeFactor = fmaxf(1.0f, 1.0f + (altitude / planetRadius) * 3.0f);
    
    // Calculate cascade sizes with a wider spread
    // Cascade 0 needs to be small for high detail (e.g. 1000m)
    // With 4096 texture, 1000m = ~24cm/texel which is excellent for lunar surface
    // Cascade 3 needs to be large for mountains (e.g. 40km)
    float cascadeSizes[CASCADE_COUNT] = {
        baseSize * altitudeFactor * 0.2f,   // Cascade 0: ~1000m (High detail)
        baseSize * altitudeFactor * 0.8f,   // Cascade 1: ~4km
        baseSize * altitudeFactor * 3.0f,   // Cascade 2: ~15km
        baseSize * altitudeFactor * 10.0f   // Cascade 3: ~50km
    };

    for (int i = 0; i < CASCADE_COUNT; i++) {
        float orthoSize = cascadeSizes[i];
        
        // Add small padding for terrain displacement (10% of max terrain height)
        float terrainPadding = maxTerrainHeight * 0.1f;
        orthoSize += terrainPadding;
        
        // Clamp to reasonable bounds
        // Allow down to 200m for sharp local shadows
        orthoSize = fmaxf(orthoSize, 200.0f); 
        orthoSize = fminf(orthoSize, planetRadius * 0.8f);
        
        // Store the split distance (max distance this cascade covers from camera)
        // This is used by the shader to select the cascade
        csm->cascades[i].splitDistance = orthoSize;
        
        // Center the shadow map on camera position (not frustum slice)
        Vector3 shadowCenter = camera.position;
        
        // Position light relative to shadow center
        // Add extra distance to account for terrain displacement in depth direction
        float lightDistance = orthoSize * 2.0f + maxTerrainHeight * 2.0f;
        Vector3 lightPos = Vector3Add(shadowCenter, 
                                     Vector3Scale(Vector3Negate(csm->lightDirection), 
                                                 lightDistance));
        
        // Light view matrix - looking at the shadow center
        Vector3 up = fabsf(csm->lightDirection.y) > 0.99f ? 
                     (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
        Matrix lightView = MatrixLookAt(lightPos, shadowCenter, up);
        
        // Orthographic projection centered on camera-focused region
        // Far plane needs to be extended to cover terrain displacement
        float farPlane = orthoSize * 4.0f + maxTerrainHeight * 4.0f;
        Matrix lightProjection = MatrixOrtho(-orthoSize, orthoSize, 
                                            -orthoSize, orthoSize,
                                            0.1f, farPlane);
        
        csm->cascades[i].lightSpaceMatrix = MatrixMultiply(lightProjection, lightView);
        
        // Store bounds (simple box around camera)
        csm->cascades[i].bounds.min = (Vector3){
            shadowCenter.x - orthoSize,
            shadowCenter.y - orthoSize,
            shadowCenter.z - orthoSize
        };
        csm->cascades[i].bounds.max = (Vector3){
            shadowCenter.x + orthoSize,
            shadowCenter.y + orthoSize,
            shadowCenter.z + orthoSize
        };
    }
}

