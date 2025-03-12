#include "hid.h"

static std::string GetLastErrorAsString() {
    DWORD error = GetLastError();
    if (error == 0) {
        return std::string(); // No error message has been recorded
    }

    LPVOID lpMsgBuf;
    DWORD bufLen = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);
    if (bufLen) {
        LPCSTR lpMsgStr = (LPCSTR)lpMsgBuf;
        std::string result(lpMsgStr, lpMsgStr + bufLen);
        LocalFree(lpMsgBuf);
        return result;
    }
    return std::string();
}

const std::string hid_error(HID& hid) {
    return GetLastErrorAsString();
}

void hid_caps(HID& hid) {
    PHIDP_PREPARSED_DATA preparsedData;
    HIDP_CAPS caps;

    if (HidD_GetPreparsedData(hid.handle, &preparsedData)) {
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            std::cout << "Usage Page: " << caps.UsagePage << std::endl;
            std::cout << "Usage: " << caps.Usage << std::endl;
            std::cout << "Input Report Byte Length: " << caps.InputReportByteLength << std::endl;
            hid.inEplength = static_cast<uint8_t>(caps.InputReportByteLength);
            hid.outEplength = static_cast<uint8_t>(caps.OutputReportByteLength);
            std::cout << "Output Report Byte Length: " << caps.OutputReportByteLength << std::endl;
            std::cout << "Feature Report Byte Length: " << caps.FeatureReportByteLength << std::endl;
            std::cout << "Number of Link Collection Nodes: " << caps.NumberLinkCollectionNodes << std::endl;
            std::cout << "Number of Input Button Caps: " << caps.NumberInputButtonCaps << std::endl;
            std::cout << "Number of Input Value Caps: " << caps.NumberInputValueCaps << std::endl;
            std::cout << "Number of Input Data Indices: " << caps.NumberInputDataIndices << std::endl;
            std::cout << "Number of Output Button Caps: " << caps.NumberOutputButtonCaps << std::endl;
            std::cout << "Number of Output Value Caps: " << caps.NumberOutputValueCaps << std::endl;
            std::cout << "Number of Output Data Indices: " << caps.NumberOutputDataIndices << std::endl;
            std::cout << "Number of Feature Button Caps: " << caps.NumberFeatureButtonCaps << std::endl;
            std::cout << "Number of Feature Value Caps: " << caps.NumberFeatureValueCaps << std::endl;
            std::cout << "Number of Feature Data Indices: " << caps.NumberFeatureDataIndices << std::endl;
        }
        else {
            std::cerr << "HidP_GetCaps failed with error: " << GetLastErrorAsString() << std::endl;
        }
        HidD_FreePreparsedData(preparsedData);
    }
    else {
        std::cerr << "HidD_GetPreparsedData failed with error: " << GetLastErrorAsString() << std::endl;
    }
}

void hid_open(std::vector<HID>& devices) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    // Get list of HID devices
    HDEVINFO deviceInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to get device info\n";
        return;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Enumerate devices
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &hidGuid, i, &interfaceData); i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA detailData =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData,
            requiredSize, NULL, NULL)) {
            // Open HID device
            HANDLE hidDevice = CreateFile(detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                NULL);

            if (hidDevice != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attributes;
                if (HidD_GetAttributes(hidDevice, &attributes)) {
                    for (auto& device : devices) {
                        if (attributes.VendorID == device.info.VendorID && attributes.ProductID == device.info.ProductID) {
                            std::cerr << "Found target device: VID=" << std::hex << attributes.VendorID
                                << " PID=" << attributes.ProductID << std::dec << std::endl;
                            device.handle = hidDevice;
                            device.info = attributes;
                            break;
                        }
                    }
                }
                if (std::find_if(devices.begin(), devices.end(), [hidDevice](const HID& device) { return device.handle == hidDevice; }) == devices.end()) {
                    CloseHandle(hidDevice);
                }
            }
        }
        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
}

#include <optional>

static std::optional<HID> hid_open(USHORT vid, USHORT pid) {
    HID hid = { INVALID_HANDLE_VALUE, 0, 0, {} };
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    // Get list of HID devices
    HDEVINFO deviceInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to get device info\n";
        return std::nullopt;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Enumerate devices
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &hidGuid, i, &interfaceData); i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA detailData =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData,
            requiredSize, NULL, NULL)) {
            // Open HID device
            HANDLE hidDevice = CreateFile(detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                NULL);

            if (hidDevice != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attributes;
                if (HidD_GetAttributes(hidDevice, &attributes)) {
                    std::cerr << "Found HID device: VID=" << std::hex << attributes.VendorID
                        << " PID=" << attributes.ProductID << std::dec << std::endl;

                    if (attributes.VendorID == vid && attributes.ProductID == pid) {
                        hid.handle = hidDevice;
                        hid.info = attributes;
                        hid_caps(hid);
                        free(detailData);
                        SetupDiDestroyDeviceInfoList(deviceInfo);
                        return hid;
                    }
                }
                CloseHandle(hidDevice);
            }
        }
        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return std::nullopt;
}

void hid_close(HID& hid) {
    CloseHandle(hid.handle);
	hid.handle = INVALID_HANDLE_VALUE;
}

bool hid_connect(HID &hid) {
    auto _hid = hid_open(hid.info.VendorID, hid.info.ProductID);
    if (!_hid) {
        std::cerr << "Failed to find target device, retrying in 5 seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
		return false;
    }
	hid = _hid.value();
    return true;
}

bool hid_read(HID& hid, std::vector<BYTE>& data) {
    DWORD bytesRead;
    OVERLAPPED overlapped = { 0 };
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    data.resize(hid.inEplength);
    if (ReadFile(hid.handle, data.data(), (DWORD)data.size(), &bytesRead, &overlapped) ||
        GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(overlapped.hEvent, INFINITE) == WAIT_OBJECT_0) {
            if (GetOverlappedResult(hid.handle, &overlapped, &bytesRead, FALSE)) {
                if (bytesRead > 0) {
                    data.resize(bytesRead);
                    CloseHandle(overlapped.hEvent);
                    return true;
                }
            }
        }
    }
    CloseHandle(overlapped.hEvent);
    data.clear();
    return false;
}

void hid_read_cp(HID& hid, HIDReadCallback callback, void* userData) {
    std::vector<BYTE> data;
    // dont call it always
    if(hid_read(hid, data))
        callback(data, userData);
}

static void hid_read_thread_func(HID& hid, HIDReadCallback callback, void* userData) {
    while (true) {
        hid_read_cp(hid, callback, userData);
        // Add a small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void hid_read_thread(HID& hid, HIDReadCallback callback, void* userData) {
    std::thread readThread(hid_read_thread_func, std::ref(hid), callback, userData);
    readThread.detach();
}

bool hid_write(HID& hid, const std::vector<BYTE>& data) {
    DWORD bytesWritten;
    OVERLAPPED overlapped = { 0 };
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (WriteFile(hid.handle, data.data(), (DWORD)data.size(), &bytesWritten, &overlapped) ||
        GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(overlapped.hEvent, INFINITE) == WAIT_OBJECT_0) {
            if (GetOverlappedResult(hid.handle, &overlapped, &bytesWritten, FALSE)) {
                CloseHandle(overlapped.hEvent);
                return bytesWritten == data.size();
            }
        }
    }
    else {
        DWORD error = GetLastError();
        std::cerr << "WriteFile failed with error: " << GetLastErrorAsString() << std::endl;
    }
    CloseHandle(overlapped.hEvent);
    return false;
}



