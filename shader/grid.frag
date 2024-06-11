#version 330 core

in vec2 TexCoords;

out vec4 FragColor;

uniform vec2 xRange;
uniform vec2 yRange;
uniform vec2 grid1;
uniform vec2 grid2;

bool equal(float a, float b, float tolerance) {
    return abs(a - b) <= tolerance;
}

void main() {
    float x = TexCoords.x * (xRange.y - xRange.x) + xRange.x;
    float y = TexCoords.y * (yRange.y - yRange.x) + yRange.x;

    FragColor = vec4(x / (xRange.y - xRange.x), y / (yRange.y - yRange.x), 0.0, 1.0);

    if (equal(x, 0.0, 0.05) || equal(y, 0.0, 0.05))
    {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
    }
    else if (equal(mod(x, grid1.x), 0.0, 0.05) || equal(mod(y, grid1.y), 0.0, 0.05))
    {
        FragColor = vec4(1.0, 1.0, 1.0, 0.8);
    }
    else if (equal(mod(x, grid2.x), 0.0, 0.05) || equal(mod(y, grid2.y), 0.0, 0.05))
    {
        FragColor = vec4(1.0, 1.0, 1.0, 0.5);
    }
    else
    {
        discard;
    }
}
