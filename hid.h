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
    std::shared_ptr<std::jthread> readThread;
    std::shared_ptr<HANDLE> stopReadEvent;
} HID;


typedef void (*HIDReadCallback)(HID& hid, const std::vector<BYTE>& data, void* userData);

const std::string hid_error(HID& hid);
void hid_close(HID& hid);
bool hid_connect(HID& hid, USHORT vid, USHORT pid, HIDReadCallback callback);
bool hid_read(HID& hid, std::vector<BYTE>& data);

bool hid_write(HID& hid, const std::vector<BYTE>& data);
void hid_caps(HID &hid);


#endif // HIDHELPER_H

