#version 330 core

in vec2 TexCoords;

out vec4 FragColor;

uniform vec2 xRange;
uniform vec2 yRange;
uniform vec2 majorGrid;
uniform vec2 minorGrid;

bool equal(float a, float b, float tolerance) {
    return abs(a - b) <= tolerance;
}

void main() {
    float xAxis = (0 - yRange.x) / (yRange.y - yRange.x);
    float yAxis = (0 - xRange.x) / (xRange.y - xRange.x);

    float xMajorGridOffset = xRange.x / (xRange.y - xRange.x);
    float xMajorGridStride = majorGrid.x / (xRange.y - xRange.x);

    float yMajorGridOffset = yRange.x / (yRange.y - yRange.x);
    float yMajorGridStride = majorGrid.y / (yRange.y - yRange.x);

    float xMinorGridOffset = xRange.x / (xRange.y - xRange.x);
    float xMinorGridStride = minorGrid.x / (xRange.y - xRange.x);

    float yMinorGridOffset = yRange.x / (yRange.y - yRange.x);
    float yMinorGridStride = minorGrid.y / (yRange.y - yRange.x);

    float zoomLevelY = 1.0 - (yRange.y - yRange.x) / (majorGrid.y * 10.0);

    if (equal(TexCoords.x, yAxis, 0.002) || equal(TexCoords.y, xAxis, 0.002))
    {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
    }
    else if (equal(mod(TexCoords.x + xMajorGridOffset, xMajorGridStride), 0.0, 0.001) || equal(mod(TexCoords.y + yMajorGridOffset, yMajorGridStride), 0.0, 0.001))
    {
        FragColor = vec4(1.0, 1.0, 1.0, 0.8);
    }
    else if (equal(mod(TexCoords.x + xMinorGridOffset , xMinorGridStride), 0.0, 0.001)|| equal(mod(TexCoords.y + yMinorGridOffset, yMinorGridStride), 0.0, 0.001))
    {
        FragColor = vec4(1.0, 1.0, 1.0, 0.8 * zoomLevelY);
    }
    else
    {
        discard;
    }
}
