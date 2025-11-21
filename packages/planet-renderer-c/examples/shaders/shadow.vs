#version 330

in vec3 vertexPosition;
uniform mat4 lightSpaceMatrix;
uniform mat4 matModel;

void main() {
    gl_Position = lightSpaceMatrix * matModel * vec4(vertexPosition, 1.0);
}
