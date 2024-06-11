#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D text;

void main()
{
    float state = texture(text, TexCoord).r;
    if (state == 0.0)
    {
        discard;
    }
    else
    {
        FragColor = vec4(vec3(state), 1.0);
    }

//    FragColor = vec4(1.0, 0.2, 0.2, 1.0);
}
