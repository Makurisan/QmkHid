#ifndef HIDHELPER_H
#define HIDHELPER_H

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>
#include <optional>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")


typedef struct _HID {
    HANDLE handle;
    uint8_t inEplength;
    uint8_t outEplength;
	HIDD_ATTRIBUTES info;
}HID;

typedef void (*HIDReadCallback)(const std::vector<BYTE>& data, void* userData);

const std::string hid_error(HID& hid);
std::optional<HID> hid_open(USHORT vid, USHORT pid);
void hid_close(HID& hid);
bool hid_connect(HID& hid);
bool hid_connect(std::vector<HID>& hids);
bool hid_read(HID& hid, std::vector<BYTE>& data);
void hid_read_thread(HID& hid, HIDReadCallback callback, void* userData);
void hid_read_cp(HID& hid, HIDReadCallback callback, void* userData);

bool hid_write(HID& hid, const std::vector<BYTE>& data);
void hid_caps(HID &hid);

#endif // HIDHELPER_H

