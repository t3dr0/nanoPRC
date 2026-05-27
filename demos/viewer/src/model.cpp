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

#include "model.h"

#include <cstdio>
#include <vector>
#include <cstddef>
#include <cstring>

#include <prc_api.h>

#include "mesh.h"

void Model::render() const
{
    glBindVertexArray(_vao);

    /* Draw each mesh */
    for (uint32_t i = 0; i < _numMeshes; i++)
    {
        glDrawElements(
            _meshes[i].primitive,
            _meshes[i].numIndices,
            GL_UNSIGNED_INT,
            (void *)(_meshes[i].offset * sizeof(GLuint)));
    }
}

void Model::load(prc_context *ctx, prc_api_data data, const prc_api_tess *tess)
{
    size_t numGraphicObjects = prc_api_get_num_graphics_primitives(ctx, data, tess);
    size_t face_max;

    _numMeshes = numGraphicObjects;
    _meshes = new MeshSpec[_numMeshes];

    _name = tess->name ? tess->name : "<unnamed>";

    std::vector<unsigned int> indices;

    if (tess->type == PRC_API_TESS_3D_Wire)
    {
        face_max = 1;
    }
    else
    {
        face_max = tess->num_faces;
    }
    uint32_t counter = 0;
    for (uint32_t i = 0; i < face_max; i++)
    {
        size_t num_graphic_primitives;
        prc_api_texture texture;
        bool hasTexture = false;

        if (tess->type == PRC_API_TESS_3D_Wire)
        {
            num_graphic_primitives = tess->num_line_primitives;
        }
        else
        {
            num_graphic_primitives = tess->tess_faces[i].num_graphic_primitives;
            texture = tess->tess_faces[i].texture;
            hasTexture = texture.data != nullptr;
        }

        if (hasTexture)
        {
            if (!_material.diffuseTexture)
                _material.setDiffuseTexture(new Texture);
            _material.diffuseTexture->load(texture);
        }

        for (uint32_t j = 0; j < num_graphic_primitives; j++)
        {
            /* Get primitive from PRC data */
            prc_api_graphic_primitive primitive;
            int rc = prc_api_get_graphics_primitive(
                ctx, data, (prc_api_tess *)tess, i, j,
                &primitive);
            if (rc < 0)
            {
                printf("Model::load: prc_api_get_graphics_primitive failed\n");
                exit(1);
            }

            /* Create a mesh */
            MeshSpec &mesh = _meshes[counter];
            switch (primitive.type)
            {
            default:
                printf("Model::load: unknown primitive type\n");
                exit(1);
            case PRC_API_TRIANGLES:
                mesh.primitive = GL_TRIANGLES;
                break;
            case PRC_API_FAN:
                mesh.primitive = GL_TRIANGLE_FAN;
                break;
            case PRC_API_STRIP:
                mesh.primitive = GL_TRIANGLE_STRIP;
                break;
            case PRC_API_LINE:
                mesh.primitive = GL_LINES;
                break;
            case PRC_API_LINE_STRIP:
                mesh.primitive = GL_LINE_STRIP;
                break;
            case PRC_API_LINE_LOOP:
                mesh.primitive = GL_LINE_LOOP;
                break;
            }

            mesh.offset = indices.size();
            mesh.numIndices = primitive.num_indices;

            /* Copy index data */
            indices.resize(indices.size() + primitive.num_indices);
            memcpy(&indices[mesh.offset], primitive.indices, primitive.num_indices * sizeof(unsigned int));

            counter++;
        }
    }

    /* Upload to GPU */

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ebo);

    glBindVertexArray(_vao);

    /* vertex buffer */
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, tess->tess_vertices.num_vertices * sizeof(prc_api_vertex), tess->tess_vertices.vertices, GL_STATIC_DRAW);

    /* index buffer */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    /* vertex attributes */

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(prc_api_vertex), (void *)offsetof(prc_api_vertex, position));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(prc_api_vertex), (void *)offsetof(prc_api_vertex, normal));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(prc_api_vertex), (void *)offsetof(prc_api_vertex, color));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(prc_api_vertex), (void *)offsetof(prc_api_vertex, uv));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Model::update()
{
    if (_dirty)
    {
        _model = Matrix4(1.0f);
        _model = _model * _matrix;
        _model = mutil::translate(_model, _position);
        _model = _model * mutil::torotation(_rotation);
        _model = mutil::scale(_model, _scale);

        _dirty = false;
    }
}

Model::Model() : _meshes(nullptr), _numMeshes(0),
                 _vbo(0), _dirty(true), _enabled(true)
{
    _position = Vector3(0.0f);
    _rotation = Quaternion();
    _scale = Vector3(1.0f);
}

Model::~Model()
{
    if (_meshes)
        delete[] _meshes;

    if (_ebo)
        glDeleteBuffers(1, &_ebo);

    if (_vbo)
        glDeleteBuffers(1, &_vbo);

    if (_vao)
        glDeleteVertexArrays(1, &_vao);
}
