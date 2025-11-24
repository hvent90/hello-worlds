#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;

out vec3 fragPos;
out vec3 fragNormal;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

void main()
{
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = mat3(transpose(inverse(matModel))) * vertexNormal;
    gl_Position = matProjection * matView * vec4(fragPos, 1.0);
}