#ifndef HIDHELPER_H
#define HIDHELPER_H

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <vector>
#include <iostream>
#include <chrono>
#include <optional>
#include <thread>

typedef struct _HIDATTRIBUTE {
    USHORT vid;
    USHORT pid;
    USHORT sernr;
    std::string devname;
} HIDATTRIBUTE;

typedef struct _HID {
    HANDLE handle;
    uint16_t inEplength;
    uint16_t outEplength;
    HIDATTRIBUTE info;
    std::optional<std::string> port;
    std::shared_ptr<std::atomic<bool>> stopFlag;
    std::shared_ptr<std::jthread> readThread;
    std::shared_ptr<HANDLE> stopReadEvent;
} HID;

typedef struct _Support {
	int seqnr; // primary key, dont use
	bool active; // 1 = active, 0 = inactive
	std::string name; // display device name, e.g . QMK, StreamDeck
	uint8_t type; // 0 = NoBoard, 1 = StreamDeck, 2 = QMK
	USHORT vid; // vendor id
	USHORT pid; // product id
	USHORT sernbr; // serial number QMK: "device_version": "1.4.3"
    std::string iface; // MI_01, or empty
	std::string serial_number; // serial number e.g. DE631822C78142210000000000000000
    std::string manufactor;
    std::string product;
    std::string dev; // e.g. \\?\HID#VID_35EE&PID_1308&MI_01#a&55b843f&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}
    std::string timestamp; // format e.g. "2025-03-19 23:09:13"
}DeviceSupport;

typedef void (*HIDReadCallback)(HID& hid, const std::vector<BYTE>& data, void* userData);

const std::string hid_error(HID& hid);
void hid_close(HID& hid);
bool hid_connect(HID& hid, std::string devname, HIDReadCallback callback);
bool hid_read(HID& hid, std::vector<BYTE>& data);
void hid_open_list(std::vector<DeviceSupport>& toopen, const std::vector<DeviceSupport>& supported);

bool hid_write(HID& hid, const std::vector<BYTE>& data);
void hid_caps(HID &hid);


#endif // HIDHELPER_H

