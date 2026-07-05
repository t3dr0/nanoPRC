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

#include <mutil/mutil.h>
#include "text.h"
#include <string>
#include <unordered_map>
/* For debug of font textures */
//#include "stb_image_write.h"
#include <cstring>

struct FontEntry
{
	std::string name;
	int point;
};

static inline bool operator==(const FontEntry &a, const FontEntry &b)
{
	return a.point == b.point && a.name == b.name;
}

struct FontHasher
{
	inline size_t operator()(const FontEntry &ent) const
	{
		return std::hash<std::string>()(ent.name) ^ ent.point;
	}
};

static std::unordered_map<FontEntry, Font*, FontHasher> _cache;

Font *Font::CreateFromData(const char *name, const void *data, size_t data_size,
	int point, prc_api_texture &input_texture)
{
	stbtt_fontinfo _info;
	if (!stbtt_InitFont(&_info, (const unsigned char *)data, 0))
		return nullptr;

	constexpr int glyph_padding = 4;
	int glyph_width = point + glyph_padding;
	int glyph_height = point + glyph_padding;
	int area = glyph_width * glyph_height * GLYPH_COUNT;

	int atlas_size = 256;
	while (atlas_size * atlas_size < area)
		atlas_size *= 2;

	Font *font = new Font(name);

	unsigned char *atlas_bitmap = (unsigned char *)calloc(atlas_size * atlas_size, 1);
	if (!atlas_bitmap)
		return nullptr;

	stbtt_pack_context pack_content;
	if (!stbtt_PackBegin(&pack_content, atlas_bitmap, atlas_size, atlas_size, 0, 1, nullptr))
	{
		free(atlas_bitmap);
		return nullptr;
	}

	// Add oversampling for sharper text
	stbtt_PackSetOversampling(&pack_content, 2, 2);

	stbtt_PackFontRange(&pack_content, (const unsigned char *)data,
		0, point, FIRST_GLPYH, GLYPH_COUNT, font->_chars);

	stbtt_PackEnd(&pack_content);

	/* Set texture data as single-channel RED */
	input_texture.width = atlas_size;
	input_texture.height = atlas_size;
	input_texture.num_channels = 1;  // CHANGED: Single channel
	input_texture.data = atlas_bitmap;  // Use the single-channel bitmap directly
	input_texture.has_transform = 0;
	memset(input_texture.transform, 0, sizeof(input_texture.transform));
	input_texture.transform[0] = 1.0;
	input_texture.transform[4] = 1.0;
	input_texture.transform[8] = 1.0;

	/* Save the atlas bitmap as an image for debug testing (uncomment to inspect) */
	//stbi_write_png("atlas_debug.png", atlas_size, atlas_size, 1, atlas_bitmap, atlas_size);

	font->_point = point;
	font->_atlas_size = atlas_size;

	return font;
}

void Font::ReleaseFonts()
{
	_cache.clear();
}

Font* Font::Derive(int point)
{
	return nullptr;
}

Vector2 Font::MeasureChar(char c, float scale) const
{
	/* Bound both ends: on platforms where char is unsigned, bytes above
	   FIRST_GLPYH + GLYPH_COUNT (e.g. from extended/UTF-8 PMI text in the
	   file) would otherwise index _chars out of bounds. */
	if (c < FIRST_GLPYH || c >= FIRST_GLPYH + GLYPH_COUNT)
		return Vector2(0.0f);

	const stbtt_packedchar &pc = _chars[c-FIRST_GLPYH];
    return Vector2(pc.xadvance * scale, -pc.yoff * scale);
}

Vector2 Font::Measure(const char *string, float scale) const
{
	if (!string)
		return Vector2(0.0f);

	Vector2 size;
	for (char c = *string; c; c = *(++string))
	{
		if (c < FIRST_GLPYH || c >= FIRST_GLPYH + GLYPH_COUNT)
			continue;

		const stbtt_packedchar &pc = _chars[c-FIRST_GLPYH];
		size.x += pc.xadvance * scale;

		float ascender = -pc.yoff * scale;
		if (ascender > size.y)
			size.y = ascender;
	}

	return size;
}

const stbtt_packedchar *Font::GetChar(char c) const
{
	if (c < FIRST_GLPYH || c >= FIRST_GLPYH + GLYPH_COUNT)
		return nullptr;
	return &_chars[c-FIRST_GLPYH];
}

Font::~Font()
{
	glDeleteTextures(1, &_texture);
	free(_name);
}

Font::Font(const char *name) :
	_texture(0), _point(0), _atlas_size(0)
{
	_name = strdup(name);
	memset(_chars, 0, sizeof(_chars));
}
