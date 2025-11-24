#version 330

out vec4 finalColor;

void main()
{
    finalColor = vec4(vec3(gl_FragCoord.z), 1.0);
}