#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D text;

void main()
{
    FragColor = vec4(vec3(texture(text, TexCoord).r), 1.0);

//    FragColor = vec4(1.0, 0.2, 0.2, 1.0);
}
