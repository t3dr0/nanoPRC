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

#include "config.h"

#include <cstdio>
#include <iostream>
#include <fstream>

static inline std::string &toLower(std::string &str)
{
	for (char &c : str)
		c = tolower(c);
	return str;
}

static std::string trim(const std::string &str)
{
	size_t start = 0;
	size_t end = str.size();

	while (start < end && isspace(str[start]))
		start++;

	while (end > start && isspace(str[end - 1]))
		end--;

	return str.substr(start, end - start);
}

bool Config::readIni(const char *filename)
{
	_config.clear();

	std::ifstream file(filename);
	if (!file.is_open())
		return false;

	std::string currentSection;

	std::string line;
	while (std::getline(file, line))
	{
		line = trim(line);

		if (line.empty() || line[0] == '#')
			continue;

		if (line[0] == '[')
		{
			size_t end = line.find(']');
			if (end == std::string::npos)
				continue;

			currentSection = trim(line.substr(1, end - 1));
			continue;
		}

		size_t pos = line.find('=');
		if (pos == std::string::npos)
			continue;

		std::string key = trim(line.substr(0, pos));
		if (key.empty())
			continue; // skip empty keys (e.g. " = value")

		std::string value = trim(line.substr(pos + 1));
		if (value.empty())
			continue; // skip empty values (e.g. "key = ")

		if (!currentSection.empty())
			key = currentSection + "." + trim(key);

		_config[key] = value;
	}

	return true;
}

bool Config::writeIni(const char *filename) const
{
	std::ofstream file(filename);
	if (!file.is_open())
		return false;

	file << "# nano_prc viewer configuration file\n";

	std::string currentSection;

	for (const auto &entry : _config)
	{
		std::string key = entry.first;
		std::string value = entry.second;

		size_t pos = key.find('.');
		if (pos != std::string::npos)
		{
			std::string section = key.substr(0, pos);
			if (section != currentSection)
			{
				file << "\n[" << section << "]\n";
				currentSection = section;
			}

			key = key.substr(pos + 1);
		}

		file << key << " = " << value << "\n";
	}

	return true;
}

const std::string &Config::getString(const char *key) const
{
	static const std::string empty;

	auto it = _config.find(key);
	return it == _config.end() ? empty : it->second;
}

bool Config::getBool(const char *key, bool def) const
{
	std::string value = getString(key);
	if (value.empty())
		return def;
	return toLower(value) == "true";
}

int Config::getInt(const char *key, int def) const
{
	std::string value = getString(key);
	if (value.empty())
		return def;

	int result;
	int count = sscanf(value.c_str(), "%d", &result);

	return count == 1 ? result : def;
}

float Config::getFloat(const char *key, float def) const
{
	std::string value = getString(key);
	if (value.empty())
		return def;

	float result;
	int count = sscanf(value.c_str(), "%f", &result);

	return count == 1 ? result : def;
}

Vector3 Config::getVector3(const char *key, const Vector3 &def) const
{
	std::string value = getString(key);
	if (value.empty())
		return def;

	Vector3 result;
	int count = sscanf(value.c_str(), "%f %f %f", &result.x, &result.y, &result.z);

	return count == 3 ? result : def;
}

IntVector3 Config::getIntVector3(const char *key, const IntVector3 &def) const
{
	std::string value = getString(key);
	if (value.empty())
		return def;

	IntVector3 result;
	int count = sscanf(value.c_str(), "%d %d %d", &result.x, &result.y, &result.z);

	return count == 3 ? result : def;
}

bool Config::hasKey(const char *key) const
{
	return _config.find(key) != _config.end();
}

void Config::setBool(const char *key, bool value)
{
	_config[key] = value ? "true" : "false";
}

void Config::setInt(const char *key, int value)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", value);
	_config[key] = buf;
}

void Config::setFloat(const char *key, float value)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%g", value);
	_config[key] = buf;
}

void Config::setVector3(const char *key, const Vector3 &value)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "%g %g %g", value.x, value.y, value.z);
	_config[key] = buf;
}

void Config::setIntVector3(const char *key, const IntVector3 &value)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "%d %d %d", value.x, value.y, value.z);
	_config[key] = buf;
}

Config::Config() {}

Config::~Config() {}
