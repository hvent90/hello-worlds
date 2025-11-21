#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 lightSpaceMatrix;
out vec3 fragNormal;
out vec3 fragPosition;
out vec4 fragPosLightSpace;

void main() {
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragPosLightSpace = lightSpaceMatrix * matModel * vec4(vertexPosition, 1.0);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
