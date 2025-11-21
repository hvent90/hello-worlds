#version 330

in vec3 fragNormal;
in vec3 fragPosition;
in vec4 fragPosLightSpace;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform sampler2D shadowMap;
out vec4 finalColor;

float ShadowCalculation(vec4 fragPosLight, vec3 normal, vec3 lightDirection) {
    // Perform perspective divide
    vec3 projCoords = fragPosLight.xyz / fragPosLight.w;
    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Outside shadow map bounds = not in shadow
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    // Get depth from shadow map
    float currentDepth = projCoords.z;

    // Bias to prevent shadow acne
    float bias = max(0.0001 * (1.0 - dot(normal, lightDirection)), 0.00005);

    // PCF (Percentage Closer Filtering) for soft shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return 1.0 - shadow;
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(lightDir);

    // Ambient lighting
    float ambient = 0.0;

    // Diffuse lighting
    float diff = max(dot(normal, lightDirection), 0.0);

    // Calculate shadow
    float shadow = ShadowCalculation(fragPosLightSpace, normal, lightDirection);

    // Combine lighting with shadow
    float lighting = ambient + (diff * 0.7) * shadow;

    finalColor = vec4(colDiffuse.rgb * lighting, colDiffuse.a);
}
