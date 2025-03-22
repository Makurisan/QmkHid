#pragma once
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <string>
#include <algorithm>

namespace stringex {
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
	inline std::string getCurrentTimestamp() {
		auto now = std::chrono::system_clock::now();
		std::time_t now_time = std::chrono::system_clock::to_time_t(now);
		std::tm now_tm;
		localtime_s(&now_tm, &now_time);

		std::ostringstream oss;
		oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
		return oss.str();
	}
	inline std::chrono::system_clock::time_point getTimePoint(const std::string& timestamp) {
		std::tm tm = {};
		std::istringstream ss(timestamp);
		ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
		return std::chrono::system_clock::from_time_t(std::mktime(&tm));
	}
}