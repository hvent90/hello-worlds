#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
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

void main()
{
    // Texel color fetching from texture sampler
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 normal = normalize(fragNormal);

    // Diffuse lighting
    vec3 viewDirection = normalize(viewPos - fragPosition);
    vec3 l = -lightDir;

    // Calculate view-space depth for cascade selection
    float viewDepth = length(viewPos - fragPosition);

    // Select cascade based on view depth
    int cascadeIndex = 4;
    for (int i = 0; i < 4; i++) {
        if (viewDepth < cascadeSplits[i + 1]) {
            cascadeIndex = i;
            break;
        }
    }

    // Diffuse lighting
    float NdotL = max(dot(normal, l), 0.0);
    
    // Specular lighting (Blinn-Phong)
    vec3 halfDir = normalize(l + viewDirection);
    float specAngle = max(dot(halfDir, normal), 0.0);
    float specular = pow(specAngle, 32.0); // Lower shininess for smoother transitions

    // Shadow calculations
    // Normal Offset Bias: Push position along normal to avoid self-shadowing on slopes
    // This is more effective than simple depth bias for terrain.
    float normalOffsetScale = 10.0 * (1.0 - dot(normal, l)); // Scale based on slope
    vec3 shadowPos = fragPosition + (normal * normalOffsetScale * (cascadeSplits[cascadeIndex+1] / 1000.0));

    vec4 fragPosLightSpace = lightVPs[cascadeIndex] * vec4(shadowPos, 1);
    fragPosLightSpace.xyz /= fragPosLightSpace.w;
    fragPosLightSpace.xyz = (fragPosLightSpace.xyz + 1.0) / 2.0;
    vec2 sampleCoords = fragPosLightSpace.xy;
    float curDepth = fragPosLightSpace.z;

    // Slope-scale depth bias (increased to eliminate shadow acne)
    float bias = max(0.0005 * (1.0 - dot(normal, l)), 0.00005);
    
    // Scale bias based on cascade index (gentler scaling than distance)
    // Precision drops with larger cascades, so we increase bias.
    // 1.0, 3.0, 9.0, 27.0
    bias *= pow(3.0, float(cascadeIndex));
    int shadowCounter = 0;
    const int numSamples = 9;
    
    vec2 texelSize = vec2(1.0 / float(shadowMapResolution));
    // Manual loop unrolling for GLSL 330 sampler indexing
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float sampleDepth = 0.0;
            vec2 offset = vec2(x, y) * texelSize;
            
            if (cascadeIndex == 0) sampleDepth = texture(shadowMaps[0], sampleCoords + offset).r;
            else if (cascadeIndex == 1) sampleDepth = texture(shadowMaps[1], sampleCoords + offset).r;
            else if (cascadeIndex == 2) sampleDepth = texture(shadowMaps[2], sampleCoords + offset).r;
            else if (cascadeIndex == 3) sampleDepth = texture(shadowMaps[3], sampleCoords + offset).r;

            if (curDepth - bias > sampleDepth)
            {
                shadowCounter++;
            }
        }
    }
    
    // If outside shadow map range, treat as fully lit (no shadow)
    if(fragPosLightSpace.z > 1.0 || fragPosLightSpace.z < 0.0 ||
       sampleCoords.x < 0.0 || sampleCoords.x > 1.0 ||
       sampleCoords.y < 0.0 || sampleCoords.y > 1.0)
    {
        shadowCounter = 0;
    }
    
    float shadow = 1.0 - (float(shadowCounter) / float(numSamples));

    // Combine lighting components
    vec3 cascadeDebugComponent = vec3(0, 0, 0);
    if (cascadeIndex == 0) cascadeDebugComponent = vec3(1, 0, 0);
    else if (cascadeIndex == 1) cascadeDebugComponent = vec3(0, 1, 0);
    else if (cascadeIndex == 2) cascadeDebugComponent = vec3(0, 0, 1);
    else if (cascadeIndex == 3) cascadeDebugComponent = vec3(1, 1, 0);

    vec3 ambientComponent = ambient.rgb * texelColor.rgb;
    vec3 diffuseComponent = lightColor.rgb * NdotL * texelColor.rgb * shadow;
    vec3 specularComponent = lightColor.rgb * specular * shadow * 0.05; // Reduced specular intensity
    
    vec3 color = ambientComponent + diffuseComponent + specularComponent; // + cascadeDebugComponent;
  
    finalColor = vec4(color, 1.0) * colDiffuse;

    // Gamma correction
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
