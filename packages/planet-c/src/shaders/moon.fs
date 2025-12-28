#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;      // Albedo texture
uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// Input lighting values
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambient;
uniform vec3 viewPos;

// Input shadowmapping values
uniform mat4 lightVPs[4];
uniform sampler2D shadowMaps[4];
uniform float cascadeSplits[5];
uniform int shadowMapResolution;
uniform int debugCascades;  // Toggle cascade debug colors

// ============================================================================
// SHADOW COMPUTATION
// ============================================================================

float computeCascadeShadow(int cascade, vec3 position, vec3 normal, vec3 lightDirection) {
    // Normal offset bias: push position along normal to avoid self-shadowing
    float cascadeScale = min(cascadeSplits[cascade + 1], 50000.0) / 1000.0;
    float normalOffsetScale = 10.0 * (1.0 - dot(normal, lightDirection));
    vec3 shadowPos = position + (normal * normalOffsetScale * cascadeScale);

    vec4 fragPosLightSpace = lightVPs[cascade] * vec4(shadowPos, 1.0);
    fragPosLightSpace.xyz /= fragPosLightSpace.w;
    fragPosLightSpace.xyz = (fragPosLightSpace.xyz + 1.0) / 2.0;

    vec2 sampleCoords = fragPosLightSpace.xy;
    float curDepth = fragPosLightSpace.z;

    // Slope-scale depth bias
    float bias = max(0.0005 * (1.0 - dot(normal, lightDirection)), 0.00005);
    bias *= pow(3.0, float(cascade));

    // 3x3 PCF sampling
    int shadowCounter = 0;
    vec2 texelSize = vec2(1.0 / float(shadowMapResolution));

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float sampleDepth = 0.0;
            vec2 offset = vec2(x, y) * texelSize;

            if (cascade == 0) sampleDepth = texture(shadowMaps[0], sampleCoords + offset).r;
            else if (cascade == 1) sampleDepth = texture(shadowMaps[1], sampleCoords + offset).r;
            else if (cascade == 2) sampleDepth = texture(shadowMaps[2], sampleCoords + offset).r;
            else if (cascade == 3) sampleDepth = texture(shadowMaps[3], sampleCoords + offset).r;

            if (curDepth - bias > sampleDepth) {
                shadowCounter++;
            }
        }
    }

    // Out-of-bounds check
    if (fragPosLightSpace.z > 1.0 || fragPosLightSpace.z < 0.0 ||
        sampleCoords.x < 0.0 || sampleCoords.x > 1.0 ||
        sampleCoords.y < 0.0 || sampleCoords.y > 1.0) {
        return 1.0;
    }

    return 1.0 - (float(shadowCounter) / 9.0);
}

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // Sample albedo texture using equirectangular UV coordinates
    vec4 texelColor = texture(texture0, fragTexCoord);

    // Use vertex normal (computed from heightmap displacement)
    vec3 normal = normalize(fragNormal);

    // Light direction (inverted for calculations)
    vec3 viewDirection = normalize(viewPos - fragPosition);
    vec3 l = -lightDir;

    // Calculate view-space depth for cascade selection
    float viewDepth = length(viewPos - fragPosition);

    // Cascade selection with blending
    int cascadeIndex = 3;
    int nextCascade = 3;
    float blendFactor = 0.0;
    const float BLEND_ZONE = 0.15;

    for (int i = 0; i < 4; i++) {
        if (viewDepth < cascadeSplits[i + 1]) {
            cascadeIndex = i;

            float cascadeStart = cascadeSplits[i];
            float cascadeEnd = cascadeSplits[i + 1];
            float cascadeRange = cascadeEnd - cascadeStart;
            float blendStart = cascadeEnd - (cascadeRange * BLEND_ZONE);

            if (viewDepth > blendStart && i < 3) {
                nextCascade = i + 1;
                blendFactor = (viewDepth - blendStart) / (cascadeEnd - blendStart);
                blendFactor = smoothstep(0.0, 1.0, blendFactor);
            }
            break;
        }
    }

    // Compute shadow
    float shadow;
    if (blendFactor > 0.0) {
        float shadow1 = computeCascadeShadow(cascadeIndex, fragPosition, normal, l);
        float shadow2 = computeCascadeShadow(nextCascade, fragPosition, normal, l);
        shadow = mix(shadow1, shadow2, blendFactor);
    } else {
        shadow = computeCascadeShadow(cascadeIndex, fragPosition, normal, l);
    }

    // Diffuse lighting
    float NdotL = max(dot(normal, l), 0.0);

    // Specular lighting (Blinn-Phong) - reduced for Moon's matte surface
    vec3 halfDir = normalize(l + viewDirection);
    float specAngle = max(dot(halfDir, normal), 0.0);
    float specular = pow(specAngle, 16.0);  // Lower shininess for rough lunar surface

    // Debug cascade colors
    vec3 cascadeDebugComponent = vec3(0, 0, 0);
    if (debugCascades != 0) {
        if (cascadeIndex == 0) cascadeDebugComponent = vec3(0.3, 0, 0);
        else if (cascadeIndex == 1) cascadeDebugComponent = vec3(0, 0.3, 0);
        else if (cascadeIndex == 2) cascadeDebugComponent = vec3(0, 0, 0.3);
        else if (cascadeIndex == 3) cascadeDebugComponent = vec3(0.3, 0.3, 0);
    }

    // Combine lighting components
    vec3 ambientComponent = ambient.rgb * texelColor.rgb;
    vec3 diffuseComponent = lightColor.rgb * NdotL * texelColor.rgb * shadow;
    vec3 specularComponent = lightColor.rgb * specular * shadow * 0.02;  // Reduced specular for Moon

    vec3 color = ambientComponent + diffuseComponent + specularComponent + cascadeDebugComponent;

    finalColor = vec4(color, 1.0) * colDiffuse;

    // Gamma correction
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
