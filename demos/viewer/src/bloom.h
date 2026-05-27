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

#ifndef _BLOOM_H_
#define _BLOOM_H_

#include "shader.h"

#define BLOOM_DOWNSAMPLES 6

struct BloomStage
{
    IntVector2 size;
    Vector2 fsize;
    GLuint texture;
};

class DownsampleShader;
class UpsampleShader;

class Bloom final
{
public:
    void render(GLuint srcTexture) const;

    void load();

    void resize(int width, int height);

    inline void use() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    }

    constexpr GLuint texture() const { return _stages[0].texture; }

    constexpr float &filterRadius() { return _filterRadius; }
    constexpr float &strength() { return _strength; }

    Bloom();
    ~Bloom();

private:
    GLuint _fbo;
    BloomStage _stages[BLOOM_DOWNSAMPLES];

    DownsampleShader *_downsampleShader;
    UpsampleShader *_upsampleShader;

    int _width, _height;

    GLuint _vao, _vbo, _ebo;

    float _filterRadius;
    float _strength;

    void unload();

    void renderDownsamples(GLuint srcTexture) const;
    void renderUpsamples() const;
};

class DownsampleShader final
{
public:
    constexpr Shader *getShader() { return &_shader; }

    inline void setTexture(GLuint unit) { _shader.setInt(uTexture, unit); }
    inline void setSize(const Vector2 &size) { _shader.setVector2(uSize, size); }

    void load();

    DownsampleShader();

private:
    Shader _shader;

    int uTexture;
    int uSize;
};

class UpsampleShader final
{
public:
    constexpr Shader *getShader() { return &_shader; }

    inline void setTexture(GLuint unit) { _shader.setInt(uTexture, unit); }
    inline void setFilterRadius(float radius) { _shader.setFloat(uFilterRadius, radius); }

    void load();

    UpsampleShader();

private:
    Shader _shader;

    int uTexture;
    int uFilterRadius;
};

#endif // _BLOOM_H_
