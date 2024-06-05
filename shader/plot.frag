#version 330 core

in vec2 TexCoords;

uniform sampler2D texture1;

out vec4 FragColor;

void main() {
    FragColor = vec4(vec3(texture(texture1, TexCoords).r), 1);
}
