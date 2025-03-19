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
    std::string name; // display device name
    uint8_t type;
    USHORT vid;
    USHORT pid;
    USHORT sernbr;
	std::string iface; // MI_01, or empty
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

