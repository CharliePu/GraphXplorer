#version 330 core

in vec2 TexCoords;
out vec4 FragColor;

uniform vec2 xRange;
uniform vec2 yRange;
uniform vec2 majorGrid;
uniform vec2 minorGrid;
uniform vec2 viewportSize;

bool equal(float a, float b, float tolerance) {
    // Use fwidth for anti-aliasing and dynamic line thickness
    float delta = fwidth(a);
    return abs(a - b) <= (tolerance * delta);
}

void main() {
    float aspectRatio = viewportSize.x / viewportSize.y;

    // Transform coordinates to pixel space for better precision
    vec2 pixelCoords = TexCoords * viewportSize;

    float xAxis = (0.0 - yRange.x) / (yRange.y - yRange.x) * viewportSize.y;
    float yAxis = (0.0 - xRange.x) / (xRange.y - xRange.x) * viewportSize.x;

    float xMajorGridSpacing = (majorGrid.x / (xRange.y - xRange.x)) * viewportSize.x;
    float yMajorGridSpacing = (majorGrid.y / (yRange.y - yRange.x)) * viewportSize.y;

    float xMinorGridSpacing = (minorGrid.x / (xRange.y - xRange.x)) * viewportSize.x;
    float yMinorGridSpacing = (minorGrid.y / (yRange.y - yRange.x)) * viewportSize.y;

    float zoomLevelY = 1.0 - (yRange.y - yRange.x) / (majorGrid.y * 10.0);

    // Use smoothstep for anti-aliased lines
    float lineWidth = 1.0;
    float axisWidth = 2.0;

    // Check for axes (using pixel-space calculations)
    float xAxisDist = abs(pixelCoords.y - xAxis);
    float yAxisDist = abs(pixelCoords.x - yAxis);

    if (xAxisDist < axisWidth || yAxisDist < axisWidth) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0 - smoothstep(0.0, axisWidth, min(xAxisDist, yAxisDist)));
        return;
    }

    // Major grid lines
    float xMajor = mod(pixelCoords.x + xRange.x * viewportSize.x / (xRange.y - xRange.x), xMajorGridSpacing);
    float yMajor = mod(pixelCoords.y + yRange.x * viewportSize.y / (yRange.y - yRange.x), yMajorGridSpacing);

    if (xMajor < lineWidth || yMajor < lineWidth ||
    xMajor > xMajorGridSpacing - lineWidth || yMajor > yMajorGridSpacing - lineWidth) {
        FragColor = vec4(1.0, 1.0, 1.0, 0.8);
        return;
    }

    // Minor grid lines
    float xMinor = mod(pixelCoords.x + xRange.x * viewportSize.x / (xRange.y - xRange.x), xMinorGridSpacing);
    float yMinor = mod(pixelCoords.y + yRange.x * viewportSize.y / (yRange.y - yRange.x), yMinorGridSpacing);

    if (xMinor < lineWidth || yMinor < lineWidth ||
    xMinor > xMinorGridSpacing - lineWidth || yMinor > yMinorGridSpacing - lineWidth) {
        FragColor = vec4(1.0, 1.0, 1.0, 0.5 * zoomLevelY);
        return;
    }

    discard;
}