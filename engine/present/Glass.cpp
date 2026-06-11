#include "Glass.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstdio>

namespace gxr
{
namespace
{
const char *kFullVs = R"(#version 330 core
layout(location=0) in vec2 pos;
out vec2 vUv;
void main(){ vUv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); })";

// 5-tap linear-sampling gaussian (effective 9-tap), run separably H then V.
const char *kBlurFs = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D tex;
uniform vec2 dir; // (1/w, 0) or (0, 1/h)
void main(){
    vec3 c = texture(tex, vUv).rgb * 0.2270270;
    c += texture(tex, vUv + dir * 1.3846154).rgb * 0.3162162;
    c += texture(tex, vUv - dir * 1.3846154).rgb * 0.3162162;
    c += texture(tex, vUv + dir * 3.2307692).rgb * 0.0702703;
    c += texture(tex, vUv - dir * 3.2307692).rgb * 0.0702703;
    frag = vec4(c, 1.0);
})";

const char *kPanelVs = R"(#version 330 core
layout(location=0) in vec2 pos; // NDC of the (shadow-expanded) quad
void main(){ gl_Position = vec4(pos, 0.0, 1.0); })";

// SDF rounded rect: frosted interior (blurred scene + dark tint), a faint
// luminous rim just inside the edge, and an exponential soft shadow outside.
const char *kPanelFs = R"(#version 330 core
out vec4 frag;
uniform vec4 rect;     // x, y, w, h in GL pixels (origin bottom-left)
uniform float radius;
uniform vec2 fbSize;
uniform sampler2D blurTex;
void main(){
    vec2 p = gl_FragCoord.xy;
    vec2 c = rect.xy + rect.zw * 0.5;
    vec2 hs = rect.zw * 0.5 - vec2(radius);
    vec2 q = abs(p - c) - hs;
    float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;

    float aPanel = 1.0 - smoothstep(-0.8, 0.8, d);
    float aShadow = exp(-max(d, 0.0) / 15.0) * 0.42;

    vec3 frost = texture(blurTex, p / fbSize).rgb;
    vec3 glass = frost * 0.40 + vec3(0.060, 0.072, 0.104) * 0.74;
    glass += vec3((1.0 - smoothstep(0.0, 2.4, abs(d + 1.3))) * 0.075); // rim light

    float alpha = max(aPanel, aShadow * (1.0 - aPanel));
    vec3 rgb = glass * (aPanel / max(alpha, 1e-4)); // shadow contributes black
    frag = vec4(rgb, alpha);
})";

constexpr float kShadowPad = 30.0f;
}

unsigned int Glass::compile(const char *vs, const char *fs)
{
    auto make = [](GLenum type, const char *src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "glass shader error: %s\n", log);
        }
        return s;
    };
    GLuint v = make(GL_VERTEX_SHADER, vs);
    GLuint f = make(GL_FRAGMENT_SHADER, fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

Glass::Glass()
{
    blurProg_ = compile(kFullVs, kBlurFs);
    panelProg_ = compile(kPanelVs, kPanelFs);
    uBlurTex_ = glGetUniformLocation(blurProg_, "tex");
    uBlurDir_ = glGetUniformLocation(blurProg_, "dir");
    uRect_ = glGetUniformLocation(panelProg_, "rect");
    uRadius_ = glGetUniformLocation(panelProg_, "radius");
    uFbSize_ = glGetUniformLocation(panelProg_, "fbSize");
    uPanelTex_ = glGetUniformLocation(panelProg_, "blurTex");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    glGenFramebuffers(2, fbo_);
    glGenTextures(2, tex_);
    makeTargets();
}

Glass::~Glass()
{
    glDeleteProgram(blurProg_);
    glDeleteProgram(panelProg_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteFramebuffers(2, fbo_);
    glDeleteTextures(2, tex_);
}

void Glass::makeTargets()
{
    qW_ = std::max(1, fbW_ / 4);
    qH_ = std::max(1, fbH_ / 4);
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, tex_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, qW_, qH_, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Glass::resize(int fbWidth, int fbHeight)
{
    fbW_ = std::max(1, fbWidth);
    fbH_ = std::max(1, fbHeight);
    makeTargets();
}

void Glass::capture()
{
    // backbuffer -> quarter res (linear-filtered downscale blit)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_[0]);
    glBlitFramebuffer(0, 0, fbW_, fbH_, 0, 0, qW_, qH_, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // two separable blur iterations at quarter res (strong, stable frost)
    glDisable(GL_BLEND);
    glViewport(0, 0, qW_, qH_);
    glUseProgram(blurProg_);
    glUniform1i(uBlurTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    const float tri[12] = {-1, -1, 3, -1, -1, 3, 0, 0, 0, 0, 0, 0};
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
    for (int it = 0; it < 2; ++it)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_[1]);
        glBindTexture(GL_TEXTURE_2D, tex_[0]);
        glUniform2f(uBlurDir_, 1.0f / static_cast<float>(qW_), 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_[0]);
        glBindTexture(GL_TEXTURE_2D, tex_[1]);
        glUniform2f(uBlurDir_, 0.0f, 1.0f / static_cast<float>(qH_));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW_, fbH_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Glass::panel(float x, float y, float w, float h, float radius)
{
    // top-left pixel coords -> GL bottom-left
    const float gx = x, gy = static_cast<float>(fbH_) - y - h;
    const float x0 = gx - kShadowPad, y0 = gy - kShadowPad;
    const float x1 = gx + w + kShadowPad, y1 = gy + h + kShadowPad;
    auto nx = [&](float v) { return v / static_cast<float>(fbW_) * 2.0f - 1.0f; };
    auto ny = [&](float v) { return v / static_cast<float>(fbH_) * 2.0f - 1.0f; };
    const float quad[12] = {nx(x0), ny(y0), nx(x1), ny(y0), nx(x1), ny(y1),
                            nx(x0), ny(y0), nx(x1), ny(y1), nx(x0), ny(y1)};

    glUseProgram(panelProg_);
    glUniform4f(uRect_, gx, gy, w, h);
    glUniform1f(uRadius_, radius);
    glUniform2f(uFbSize_, static_cast<float>(fbW_), static_cast<float>(fbH_));
    glUniform1i(uPanelTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_[0]);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
}
