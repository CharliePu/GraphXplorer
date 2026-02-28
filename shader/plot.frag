#version 330 core

in vec2 TexCoords;

uniform sampler2D texture1;

out vec4 FragColor;

void main() {
    float state = texture(texture1, TexCoords).r;
    if (state <= 0.0) {
        discard;
    }

    FragColor = vec4(0.0, 0.47, 0.95, 1.0);
}
