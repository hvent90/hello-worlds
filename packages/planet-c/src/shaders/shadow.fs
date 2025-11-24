#version 330

in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragPosLight;

uniform sampler2D shadowMap;
uniform vec3 lightDir;

out vec4 finalColor;

float ShadowCalculation(vec4 fragPosLight)
{
    // perform perspective divide
    vec3 projCoords = fragPosLight.xyz / fragPosLight.w;
    // transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(shadowMap, projCoords.xy).r; 
    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    // check whether current frag pos is in shadow
    float bias = 0.005;
    float shadow = currentDepth + bias > closestDepth ? 1.0 : 0.0;

    if(projCoords.z > 1.0)
        shadow = 0.0;
        
    return shadow;
}

void main()
{
    // Basic lighting - test normals
    vec3 normal = normalize(fragNormal);
    vec3 light = normalize(lightDir); // Use uniform light direction
    float diff = max(dot(normal, light), 0.2);

    float shadow = ShadowCalculation(fragPosLight);       
    vec3 lighting = (diff * (1.0 - shadow)) * vec3(0.8, 0.8, 0.8);    
    
    finalColor = vec4(lighting, 1.0);
}