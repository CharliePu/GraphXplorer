#version 330 core

layout(location = 0) in vec3 aPos;

uniform mat4 transform;

out vec2 TexCoords;

void main() {
    gl_Position = transform * vec4(aPos, 1.0);

    TexCoords = (aPos.xy + vec2(1.0))/2.0;
}
