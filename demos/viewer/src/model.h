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

#ifndef _MODEL_H_
#define _MODEL_H_

#include <string>

#include "shader.h"

#include <prc_api.h>

struct MeshSpec
{
    GLenum primitive;
    GLsizei offset;
    GLsizei numIndices;
};

class MeshShader;

class Model
{
public:
    void render() const;

    void load(prc_context *ctx, prc_api_data data, const prc_api_tess *tess);

    constexpr Material *material() { return &_material; }
    constexpr const Material *material() const { return &_material; }

    constexpr const Vector3 &position() const { return _position; }
    constexpr const Quaternion &rotation() const { return _rotation; }
    constexpr const Vector3 &scale() const { return _scale; }

    constexpr const Vector4 &bboxMin() const { return _bbox_min; }
    constexpr const Vector4 &bboxMax() const { return _bbox_max; }
    constexpr void setBoundingBox(const double min[3], const double max[3])
    {
        _bbox_max = Vector4((float) max[0], (float) max[1], (float) max[2], 1.0f);
        _bbox_min = Vector4((float) min[0], (float) min[1], (float) min[2], 1.0f);
    }
    constexpr void setBoundingBox(const Vector4 &min, const Vector4 &max)
    {
        _bbox_max = max;
        _bbox_min = min;
    }

    constexpr void setMatrix(const Matrix4 &matrix)
    {
        _matrix = matrix;
        _dirty = true;
    }

    constexpr void setPosition(const Vector3 &position)
    {
        _position = position;
        _dirty = true;
    }

    constexpr void setRotation(const Quaternion &rotation)
    {
        _rotation = rotation;
        _dirty = true;
    }

    constexpr void setScale(const Vector3 &scale)
    {
        _scale = scale;
        _dirty = true;
    }

    constexpr const Matrix4 &model() const { return _model; }

    constexpr const std::string &name() const { return _name; }

    constexpr bool &enabled() { return _enabled; }

    void update();

    Model();
    ~Model();

private:
    MeshSpec *_meshes;
    uint32_t _numMeshes;

    GLuint _vao, _vbo, _ebo;

    Material _material;

    Vector3 _position;
    Quaternion _rotation;
    Vector3 _scale;
    Matrix4 _matrix;

    bool _dirty;

    Matrix4 _model;

    bool _enabled;

    Vector4 _bbox_min;
    Vector4 _bbox_max;

    std::string _name;
};

#endif // _MODEL_H_
