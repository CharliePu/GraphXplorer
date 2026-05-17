#version 330 core

in vec2 TexCoords;

uniform sampler2D texture1;

out vec4 FragColor;

void main() {
    float sampleValue = texture(texture1, TexCoords).r;
    if (sampleValue <= 0.001)
    {
        discard;
    }
    vec3 trueColor = vec3(0.0, 0.47, 0.95);
    float alpha = 1.0 - exp(-6.0 * sampleValue);
    FragColor = vec4(trueColor, alpha);
}
