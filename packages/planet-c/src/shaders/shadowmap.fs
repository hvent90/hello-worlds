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
uniform mat4 lightVP;
uniform sampler2D shadowMap;
uniform int shadowMapResolution;

void main()
{
    // Texel color fetching from texture sampler
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 l = -lightDir;

    // Diffuse lighting
    float NdotL = max(dot(normal, l), 0.0);
    
    // Specular lighting (Blinn-Phong)
    vec3 halfDir = normalize(l + viewD);
    float specAngle = max(dot(halfDir, normal), 0.0);
    float specular = pow(specAngle, 32.0); // Lower shininess for smoother transitions

    // Shadow calculations
    vec4 fragPosLightSpace = lightVP * vec4(fragPosition, 1);
    fragPosLightSpace.xyz /= fragPosLightSpace.w;
    fragPosLightSpace.xyz = (fragPosLightSpace.xyz + 1.0) / 2.0;
    vec2 sampleCoords = fragPosLightSpace.xy;
    float curDepth = fragPosLightSpace.z;

    // Slope-scale depth bias (increased to eliminate shadow acne)
    float bias = max(0.002 * (1.0 - dot(normal, l)), 0.0005);
    int shadowCounter = 0;
    const int numSamples = 9;
    
    vec2 texelSize = vec2(1.0 / float(shadowMapResolution));
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float sampleDepth = texture(shadowMap, sampleCoords + texelSize * vec2(x, y)).r;
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
    vec3 ambientComponent = ambient.rgb * texelColor.rgb;
    vec3 diffuseComponent = lightColor.rgb * NdotL * texelColor.rgb * shadow;
    vec3 specularComponent = lightColor.rgb * specular * shadow * 0.3; // Reduced specular intensity
    
    vec3 color = ambientComponent + diffuseComponent + specularComponent;
    finalColor = vec4(color, 1.0) * colDiffuse;

    // Gamma correction
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
