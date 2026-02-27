#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;

uniform mat4 transform;

out vec2 Uv;
out vec4 Color;

void main() {
    gl_Position = transform * vec4(aPos, 1.0);
    Uv = aUv;
    Color = aColor;
}
