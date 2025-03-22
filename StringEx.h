#pragma once

#include <iostream>
#include <string>
#include <algorithm>

namespace StringEx {
	inline std::string toUpper(const std::string& str) {
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::toupper(c); });
		return result;
	}
	inline std::string toLower(const std::string& str) {
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
		return result;
	}
}