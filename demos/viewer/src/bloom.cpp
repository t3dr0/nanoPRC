/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nano_prc is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nano_prc is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nano_prc. If not, see <https://www.gnu.org/licenses/>.
*/

#include "bloom.h"
#include <cstring>
#include <memory>
#include <cstdio>

extern const char *composite_vert_source; // see composite.cpp
#include "shaders/downsample.frag.h"
#include "shaders/upsample.frag.h"

static constexpr float kFilterRadius = 0.005f;

// <vec2 pos, vec2 tex>
static const Vector4 kQuadVertices[] = {
    Vector4(-1.0f, -1.0f, 0.0f, 0.0f),
    Vector4(1.0f, -1.0f, 1.0f, 0.0f),
    Vector4(1.0f, 1.0f, 1.0f, 1.0f),
    Vector4(-1.0f, 1.0f, 0.0f, 1.0f)};

static const GLuint kQuadIndices[] = {
    0, 2, 1,
    0, 3, 2};

void Bloom::render(GLuint srcTexture) const
{
    renderDownsamples(srcTexture);
    renderUpsamples();
}

void Bloom::load()
{
    /* Load shaders */

    _downsampleShader = new DownsampleShader();
    _downsampleShader->load();

    _upsampleShader = new UpsampleShader();
    _upsampleShader->load();

    /* Create a quad */

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ebo);

    glBindVertexArray(_vao);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Bloom::resize(int width, int height)
{
    unload();

    glGenFramebuffers(1, &_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);

    /* Create textures for each stage */
    IntVector2 size(width, height);
    for (int32_t i = 0; i < BLOOM_DOWNSAMPLES; i++)
    {
        BloomStage &stage = _stages[i];
        size = size / 2;
        stage.size = size;
        stage.fsize = Vector2(size);

        glGenTextures(1, &stage.texture);
        glBindTexture(GL_TEXTURE_2D, stage.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, size.x, size.y, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _stages[0].texture, 0);

    const unsigned int attachments[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, attachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("Bloom::resize: Framebuffer is not complete\n");
        exit(1);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    _width = width;
    _height = height;
}

Bloom::Bloom() : _fbo(0),
                 _downsampleShader(nullptr), _upsampleShader(nullptr),
                 _width(0), _height(0),
                 _vao(0), _vbo(0), _ebo(0),
                 _filterRadius(kFilterRadius)
{
    memset(_stages, 0, sizeof(_stages));
}

Bloom::~Bloom()
{
    unload();

    if (!_upsampleShader)
        delete _upsampleShader;

    if (!_downsampleShader)
        delete _downsampleShader;
}

void Bloom::unload()
{
    if (_fbo)
    {
        glDeleteFramebuffers(1, &_fbo);
        _fbo = 0;
    }

    for (int32_t i = 0; i < BLOOM_DOWNSAMPLES; i++)
    {
        if (_stages[i].texture)
        {
            glDeleteTextures(1, &_stages[i].texture);
            _stages[i].texture = 0;
        }
    }
}

void Bloom::renderDownsamples(GLuint srcTexture) const
{
    _downsampleShader->getShader()->use();

    _downsampleShader->setSize(Vector2(_width, _height));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTexture);
    _downsampleShader->setTexture(0);

    glBindVertexArray(_vao);

    for (int32_t i = 0; i < BLOOM_DOWNSAMPLES; i++)
    {
        const BloomStage &stage = _stages[i];

        glViewport(0, 0, stage.size.x, stage.size.y);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stage.texture, 0);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        _downsampleShader->setSize(stage.fsize);
        glBindTexture(GL_TEXTURE_2D, stage.texture);
    }
}

void Bloom::renderUpsamples() const
{
    _upsampleShader->getShader()->use();

    _upsampleShader->setFilterRadius(_filterRadius);

    /* Additive blending */
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);

    glActiveTexture(GL_TEXTURE0);
    _upsampleShader->setTexture(0);

    glBindVertexArray(_vao);

    for (int32_t i = BLOOM_DOWNSAMPLES - 1; i > 0; i--)
    {
        const BloomStage &stage = _stages[i];
        const BloomStage &nextStage = _stages[i - 1];

        glBindTexture(GL_TEXTURE_2D, stage.texture);

        glViewport(0, 0, nextStage.size.x, nextStage.size.y);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nextStage.texture, 0);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    }

    glDisable(GL_BLEND);
}

void DownsampleShader::load()
{
    _shader.load(composite_vert_source, downsample_frag_source);

    _shader.use();

    uTexture = _shader.getUniformLocation("uTexture");
    uSize = _shader.getUniformLocation("uSize");
}

DownsampleShader::DownsampleShader() : uTexture(-1), uSize(-1)
{
}

void UpsampleShader::load()
{
    _shader.load(composite_vert_source, upsample_frag_source);

    _shader.use();

    uTexture = _shader.getUniformLocation("uTexture");
    uFilterRadius = _shader.getUniformLocation("uFilterRadius");
}

UpsampleShader::UpsampleShader() : uTexture(-1), uFilterRadius(-1)
{
}
