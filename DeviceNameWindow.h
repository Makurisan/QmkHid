#pragma once

#include <string>
#include <vector>
#include <regex>
#include <optional>
#include <sstream>
#include <iostream>
#include <format>
#include <windows.h>

class DeviceNameParser {
public:
    DeviceNameParser(const std::string& deviceName) {
        parseDeviceName(deviceName);
    }

    std::optional<uint16_t> getVID() const { return vid; }
    std::optional<uint16_t> getPID() const { return pid; }
    std::optional<std::string> getMI() const { return mi; }
    std::optional<std::string> getPort() const { return port; }

    void log() const {
        if (vid.has_value()) _log("VID: {:04X}\n", vid.value());
        if (pid.has_value()) _log("PID: {:04X}\n", pid.value());
        if (mi.has_value()) _log("MI: {}\n", mi.value());
        if (port.has_value()) _log("Port: {}\n", port.value());
    }
	bool isQMKHidInterface(const HID& hid) const {
		std::string c_mi = mi.value();
        if (c_mi.find("&MI_01") != std::string::npos &&
            hid.info.ProductID == pid && hid.info.VendorID == vid)
            return true;
        return false;
	}

private:
    std::optional<uint16_t> vid;
    std::optional<uint16_t> pid;
    std::optional<std::string> mi;
    std::optional<std::string> port;

    void _log(const std::string& format_str, auto&&... args) const {
        std::string formatted_str = std::vformat(format_str, std::make_format_args(args...));
        OutputDebugString(formatted_str.c_str());
    }

    void parseDeviceName(const std::string& deviceName) {
        std::vector<std::string> pieces = splitDeviceName(deviceName);

        for (size_t i = 0; i < pieces.size(); ++i) {
            const auto& part = pieces[i];

            if (part.find("VID_") != std::string::npos) {
                vid = std::stoi(part.substr(part.find("VID_") + 4), nullptr, 16);
            }
            if (part.find("PID_") != std::string::npos) {
                pid = std::stoi(part.substr(part.find("PID_") + 4), nullptr, 16);
            }
            if (part.find("&MI_") != std::string::npos) {
                mi = part.substr(part.find("&MI_"));
            }
            if (i == 2) {
                port = part;
            }
        }
    }

    std::vector<std::string> splitDeviceName(const std::string& deviceName) {
        std::vector<std::string> pieces;
        std::stringstream ss(deviceName);
        std::string item;

        while (std::getline(ss, item, '#')) {
            pieces.push_back(item);
        }
        return pieces;
    }
};
