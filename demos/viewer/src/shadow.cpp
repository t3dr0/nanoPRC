/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

#include "shadow.h"

#include <cstdio>

#include "shaders/shadow.vert.h"
#include "shaders/shadow.frag.h"

#include "camera.h"

#define SHADOW_MAP_EXTENT 20.0f
#define SHADOW_MAP_PLANE_NEAR 1.0f
#define SHADOW_MAP_PLANE_FAR 100.0f

void ShadowMap::load(int resolution)
{
    static const float kBorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};

    /* Create framebuffer */
    glGenFramebuffers(1, &_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);

    /* Create depth texture */
    glGenTextures(1, &_depthMap);
    glBindTexture(GL_TEXTURE_2D, _depthMap);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, resolution, resolution,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    /* Attach depth texture to framebuffer */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _depthMap, 0);

    /* Check if framebuffer is complete */
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("ShadowMap::Load: Framebuffer is not complete!\n");
        exit(1);
    }

    /* Instruct OpenGL that we won't bind a color texture with the currently bound framebuffer */
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    _lightProjection = mutil::ortho(
        -SHADOW_MAP_EXTENT, SHADOW_MAP_EXTENT,
        -SHADOW_MAP_EXTENT, SHADOW_MAP_EXTENT,
        SHADOW_MAP_PLANE_NEAR, SHADOW_MAP_PLANE_FAR);

    _resolution = resolution;
}

void ShadowMap::setLightDirection(const Vector3 &direction)
{
    _lightSpaceMatrix = _lightProjection * mutil::lookAt(-direction * SHADOW_MAP_EXTENT, Vector3(0.0f), kWorldUp);
}

ShadowMap::ShadowMap() : _fbo(0), _depthMap(0)
{
}

ShadowMap::~ShadowMap()
{
}

void ShadowShader::load()
{
    _shader.load(shadow_vert_source, shadow_frag_source);

    _shader.use();

    uModel = _shader.getUniformLocation("uModel");
    uLightSpaceMatrix = _shader.getUniformLocation("uLightSpaceMatrix");
}

ShadowShader::ShadowShader() : uModel(-1), uLightSpaceMatrix(-1)
{
}

ShadowShader::~ShadowShader()
{
}
