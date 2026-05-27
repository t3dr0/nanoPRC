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

#pragma once

#include "text.h"
#include "shader.h"
#include <vector>

using namespace mutil;

class Font;
class Program;

class Graphics2D final
{
public:
	void Begin(int w, int h);

	void SetClip(int x, int y, int w, int h);
	IntVector4 GetClip() const;

	void SetTransform(const Vector3 &t);
	constexpr const Vector3 &GetTransform() const { return _transform; }

	void SetLineWidth(float width);
	void SetColor(const Vector4 &color);
	void SetTexture(GLuint texture);

	void FillRect(float x0, float y0, float x1, float y1);
	void DrawRect(float x0, float y0, float x1, float y1);

	void SetFont(Font *font);
    constexpr Font *GetFont() const { return _font; }

	void DrawText(const char *text, float x, float y, float z);

	void Render(const Vector2 &dim);

	uint32_t GetNumIndices();
    uint32_t GetNumVertices();
	void GetVertices(prc_api_vertex *textVertices, Material in_material);
	void GetIndicesVector(std::vector<unsigned int> &indices);

	void load();

	Graphics2D();
	~Graphics2D();
private:
	struct Vertex
	{
		Vector3 position;
		Vector2 tex_coords;
		Vector4 color;
	};

	struct DrawCommand2D
	{
		int clip_rect[4];
		GLuint texture_id;
		GLsizei idx_offset;
		GLsizei elem_count;
		bool is_text;
	};

	struct DrawList2D
	{
		DrawCommand2D *cmd_list;
		size_t command_count;
		size_t max_commands;

		Vertex *vertices;
		GLsizei vertex_count;
		GLsizei max_vertices;

		uint32_t *indices;
		GLsizei index_count;
		GLsizei max_indices;

		DrawCommand2D *AllocCommand();
		Vertex *AllocVertices(GLsizei count);
		uint32_t *AllocIndices(GLsizei count);

		void EmitQuad(float x0, float y0, float z0, float x1, float y1, float z1, const Vector4 &color);

        //uint32_t GetNumIndices();

		DrawList2D();
		~DrawList2D();
	};

	Shader _shader;
	int _u_texture;
	int _u_view;
	int _u_has_texture;
	int _u_is_text;

	DrawList2D *_draw_lists;
	size_t _draw_list_count;
	size_t _max_draw_lists;

	int _clip_rect[4];
	Vector3 _transform;
	float _line_width;
	Vector4 _color;
	GLuint _texture;
	Font *_font;

	GLuint _vao;

	GLuint _vbo;
	GLsizei _vbo_count;

	GLuint _ebo;
	GLsizei _ebo_count;

	DrawList2D *NewDrawList();
};
