#version 330

in vec3 fragPos;
in vec3 fragNormal;

out vec4 finalColor;

uniform sampler2D shadowMap;
uniform mat4 lightMatrix;

void main()
{
    // Basic lighting - test normals
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(-0.5, 1.0, -0.5));
    float diff = max(dot(normal, lightDir), 0.2);

    vec3 baseColor = vec3(0.8, 0.8, 0.8);
    finalColor = vec4(baseColor * diff, 1.0);
}