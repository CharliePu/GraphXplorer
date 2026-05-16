#version 330 core

in vec2 Uv;
in vec4 Color;

uniform int debugMode;

out vec4 FragColor;

void main() {
    if (debugMode == 0) {
        if (Color.a <= 0.0) {
            discard;
        }
        FragColor = vec4(Color.rgb, Color.a);
        return;
    }

    // In debug mode we render two kinds of chunk meshes:
    // 1) Normal chunk fill meshes (Color.a in [0,1]) remain normal.
    // 2) Debug overlay meshes (Color.a > 1) draw border-only frames.
    //    Color.a > 2.5: mixed chunk, otherwise uniform chunk.
    if (Color.a <= 1.0) {
        if (Color.a <= 0.0) {
            discard;
        }
        FragColor = vec4(Color.rgb, Color.a);
        return;
    }

    float borderWidth = (Color.a > 2.5) ? 0.018 : 0.010;
    float edgeDistance = min(min(Uv.x, Uv.y), min(1.0 - Uv.x, 1.0 - Uv.y));
    if (edgeDistance >= borderWidth) {
        discard;
    }
    FragColor = vec4(Color.rgb, 1.0);
}
