#version 330

in vec3 fragPosition;
in vec3 fragNormal;
// in vec4 fragPosLightSpace; // Unused in CSM

uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 viewPos;

// CSM uniforms
#define CASCADE_COUNT 4
uniform sampler2D cascadeShadowMaps[CASCADE_COUNT];
uniform mat4 cascadeLightMatrices[CASCADE_COUNT];
uniform float cascadeDistances[CASCADE_COUNT];

out vec4 finalColor;

int GetCascadeIndex(float distanceFromCamera) {
    for (int i = 0; i < CASCADE_COUNT - 1; i++) {
        if (distanceFromCamera < cascadeDistances[i]) {
            return i;
        }
    }
    return CASCADE_COUNT - 1;
}

float ShadowCalculationHard(int cascadeIndex, vec3 fragPos) {
    // Transform fragment to light space for selected cascade
    vec4 fragPosLightSpace = cascadeLightMatrices[cascadeIndex] * vec4(fragPos, 1.0);

    // Perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Outside shadow map bounds = no shadow
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0 || projCoords.z < 0.0) {
        return 1.0;
    }

    // Get closest depth from shadow map
    float closestDepth = texture(cascadeShadowMaps[cascadeIndex], projCoords.xy).r;
    float currentDepth = projCoords.z;

    // Adaptive bias based on slope
    float bias = max(0.0005 * (1.0 - dot(normalize(fragNormal), lightDir)), 0.00005);

    // Binary shadow test (HARD shadows, no PCF)
    return (currentDepth - bias) > closestDepth ? 0.0 : 1.0;
}

void main() {
    // Distance from camera
    float distFromCamera = length(fragPosition - viewPos);

    // Select cascade
    int cascadeIndex = GetCascadeIndex(distFromCamera);

    // Compute shadow
    float shadow = ShadowCalculationHard(cascadeIndex, fragPosition);

    // Lighting
    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(lightDir);

    float ambient = 0.0;  // PURE BLACK shadows (lunar aesthetic)
    float diffuse = max(dot(normal, lightDirection), 0.0);

    // Shadow only affects diffuse (no ambient to shadow)
    float lighting = ambient + (diffuse * shadow);

    finalColor = vec4(colDiffuse.rgb * lighting, colDiffuse.a);
}
