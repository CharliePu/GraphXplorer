#version 330 core

in vec2 TexCoords;

uniform sampler2D texture1;

out vec4 FragColor;

void main() {
    float state = texture(texture1, TexCoords).r;
    if (state == 0.0) {
        discard;
    } else if (state < 0.0) {
        FragColor = vec4(1.0f, 0.65f, 0.0f, 1.0f);
    } else {
        FragColor = vec4(0.0f, 0.47f, 0.95f, 1.0f);
    }
}
