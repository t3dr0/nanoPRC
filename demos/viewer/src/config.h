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

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <map>
#include <string>

#include <mutil/mutil.h>

using namespace mutil;

class Config final
{
public:
	bool readIni(const char *filename);
	bool writeIni(const char *filename) const;

	const std::string &getString(const char *key) const;
	bool getBool(const char *key, bool def = false) const;
	int getInt(const char *key, int def = 0) const;
	float getFloat(const char *key, float def = 0.0f) const;
	Vector3 getVector3(const char *key, const Vector3 &def = Vector3()) const;
	IntVector3 getIntVector3(const char *key, const IntVector3 &def = IntVector3()) const;

	bool hasKey(const char *key) const;

	void setBool(const char *key, bool value);
	void setInt(const char *key, int value);
	void setFloat(const char *key, float value);
	void setVector3(const char *key, const Vector3 &value);
	void setIntVector3(const char *key, const IntVector3 &value);

	Config();
	~Config();
private:
	std::map<std::string, std::string> _config;
};

#endif // _CONFIG_H_
