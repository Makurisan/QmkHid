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

typedef struct _USB {
    std::string devicePath;
    USHORT vendorID;
    USHORT productID;
    std::string serialNumber;
}USB;

typedef struct _HID {
    HANDLE handle;
    uint16_t inEplength;
    uint16_t outEplength;
	HIDD_ATTRIBUTES info;
    std::optional<std::string> port;
    std::shared_ptr<std::atomic<bool>> stopFlag;
    std::shared_ptr<std::thread> readThread;
}HID;

typedef void (*HIDReadCallback)(HID& hid, const std::vector<BYTE>& data, void* userData);

const std::string hid_error(HID& hid);
std::optional<HID> hid_open(USHORT vid, USHORT pid, USHORT sernbr);
void hid_close(HID& hid);
bool hid_connect(HID& hid, USHORT vid, USHORT pid, USHORT sernbr=0);
bool hid_read(HID& hid, std::vector<BYTE>& data);
void hid_read_thread(HID& hid, HIDReadCallback callback, void* userData);
void hid_stopread_thread(HID& hid);

bool hid_write(HID& hid, const std::vector<BYTE>& data);
void hid_caps(HID &hid);

#endif // HIDHELPER_H

