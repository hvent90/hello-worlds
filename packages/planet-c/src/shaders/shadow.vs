#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
uniform mat4 lightMatrix;

out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragPosLight;

void main()
{
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = mat3(transpose(inverse(matModel))) * vertexNormal;
    fragPosLight = lightMatrix * vec4(fragPos, 1.0);
    gl_Position = matProjection * matView * vec4(fragPos, 1.0);
}