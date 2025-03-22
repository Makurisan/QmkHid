#pragma once

#include <string>
#include <vector>
#include <regex>
#include <optional>
#include <sstream>
#include <iostream>
#include <format>
#include "StringEx.h"

class DeviceNameParser {
public:
    DeviceNameParser(const std::string& deviceName) {
        parseDeviceName(deviceName);
    }

    std::optional<uint16_t> getVID() const { return vid; }
    std::optional<uint16_t> getPID() const { return pid; }
    std::optional<std::string> getMI() const { return mi; }
    std::optional<std::string> getPort() const { return port; }
    std::string getDevName() const { return devname; }

    void log() const {
        if (vid.has_value()) _log("VID: {:04X}\n", vid.value());
        if (pid.has_value()) _log("PID: {:04X}\n", pid.value());
        if (mi.has_value()) _log("MI: {}\n", mi.value());
        if (port.has_value()) _log("Port: {}\n", port.value());
    }
	bool isHidInterface(const std::string& iface) const {
		std::string c_mi = mi.value();
        // check something like "&MI_01"
        if (c_mi.find(iface) != std::string::npos)
            return true;
        return false;
	}
	// Bluetooth Low Energy (BLE) UUID: we don't support at the moment
    //devname = "\\\\?\\HID#{00001812-0000-1000-8000-00805F9B34FB}_DEV_VID&02046D_PID&B013_REV&0007_C4C4C279C660&COL03#B&2C603A6D&0&0002#{4D1E55B2-F16F-11CF-88CB-001111000030}"
	bool isRegularHidDevice() {
		// the minimal requirement
		if (vid.has_value() && pid.has_value()) {
			return true;
		}
		return false;
	}
	bool isValidPort(const std::string& port) const {
		std::regex portRegex(R"(([0-9A-Fa-f]+)&([0-9A-Fa-f]+)&([0-9A-Fa-f]+)&([0-9A-Fa-f]+))");
		return std::regex_match(port, portRegex);
	}
private:
    std::optional<uint16_t> vid;
    std::optional<uint16_t> pid;
    std::optional<std::string> mi;
    std::optional<std::string> port;
    std::string devname;


    void _log(const std::string& format_str, auto&&... args) const {
        std::string formatted_str = std::vformat(format_str, std::make_format_args(args...));
        OutputDebugString(formatted_str.c_str());
    }

    bool parseDeviceName(const std::string& deviceName) {
        this->devname =  stringex::toUpper(deviceName);
		std::vector<std::string> pieces = splitDeviceName(this->devname);

		std::smatch match;
		std::regex uuidRegex(R"(\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\})");

		// if device name start with a UUID we stop parsing -> BLE device
        if (pieces.size() > 1 && std::regex_search(pieces[1], match, uuidRegex)) {
			uint16_t vid = 0, pid = 0;
			const auto& part = pieces[1];

			std::regex vidRegex(R"(VID&([0-9a-fA-F]{4}))");
			if (std::regex_search(part, match, vidRegex)) {
				vid = std::stoi(match.str(1), nullptr, 16);
			}

			std::regex pidRegex(R"(PID&([0-9a-fA-F]{4}))");
			if (std::regex_search(part, match, pidRegex)) {
				pid = std::stoi(match.str(1), nullptr, 16);
			}
			// we don't support BLE device
            return false;
		}

        for (size_t i = 0; i < pieces.size(); ++i) {
            const auto& part = pieces[i];
			std::smatch match;

			std::regex vidRegex(R"(VID_([0-9a-fA-F]{4}))");
			if (std::regex_search(part, match, vidRegex)) {
                vid = std::stoi(match.str(1), nullptr, 16);
            }
			std::regex pidRegex(R"(PID_([0-9a-fA-F]{4}))");
			if (std::regex_search(part, match, pidRegex)) {
				pid = std::stoi(match.str(1), nullptr, 16);
			}

			std::regex miRegex(R"(&MI_([0-9a-fA-F]{2}))");
			if (std::regex_search(part, match, miRegex)) {
                mi = "&MI_" + match.str(1);
			}
            if (i == 2) {
                auto val = isValidPort(part);
                port = part;
            }
        }
        if (isRegularHidDevice()) {
            return true;
        }
        return false;
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
