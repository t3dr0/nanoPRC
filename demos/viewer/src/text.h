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

#pragma once

#include "stb_truetype.h"
#include <glad/glad.h>
#include <mutil/mutil.h>
#include "texture.h"

class Resource;

using namespace mutil;

class Font
{
public:

	static constexpr int FIRST_GLPYH = 32;
	static constexpr int GLYPH_COUNT = 96;
	static constexpr int TAB_SIZE = 4;

	static void ReleaseFonts();

public:
	static Font *CreateFromData(const char *name, const void *data, size_t data_size, int point, prc_api_texture &input_texture);
	Font* Derive(int point);
	Vector2 MeasureChar(char c, float scale) const;
	Vector2 Measure(const char *string, float scale) const;
	const stbtt_packedchar *GetChar(char c) const;

	constexpr const char *GetName() const { return _name; }
	constexpr int GetPointSize() const { return _point; }

	constexpr GLuint GetTexture() const { return _texture; }
    constexpr void SetTextureId(GLuint id) { _texture = id; }
	constexpr int GetAtlasSize() const { return _atlas_size; }

	virtual ~Font();
private:
	Font(const char *name);

	char *_name;
	stbtt_packedchar _chars[96];
	int _point;
	GLuint _texture;
	int _atlas_size;
};
