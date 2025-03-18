#include <optional>
#include <format> // For std::
#include <thread>
#include <atomic>
#include "hid.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

std::atomic<bool> stopThread{ false };

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
            hid.inEplength = static_cast<uint16_t>(caps.InputReportByteLength);
            hid.outEplength = static_cast<uint16_t>(caps.OutputReportByteLength);
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

std::optional<std::string> GetUsbPortNumber(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData) {
    DWORD requiredSize = 0;
    SetupDiGetDeviceInstanceId(deviceInfoSet, &deviceInfoData, NULL, 0, &requiredSize);
    std::vector<char> buffer(requiredSize);
    if (!SetupDiGetDeviceInstanceId(deviceInfoSet, &deviceInfoData, buffer.data(), requiredSize, &requiredSize)) {
        return std::nullopt;
    }

    std::string deviceInstanceId(buffer.data());
    size_t pos = deviceInstanceId.find_last_of('\\');
    if (pos != std::string::npos) {
        std::string portNumber = deviceInstanceId.substr(pos + 1);
        return portNumber;
    }

    return std::nullopt;
}

std::optional<HID> hid_open(USHORT vid, USHORT pid, USHORT sernbr) {
    HID hid = { INVALID_HANDLE_VALUE, 0, 0, {} };
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to get device info\n";
        return std::nullopt;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &hidGuid, i, &interfaceData); i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData, requiredSize, NULL, NULL)) {
            HANDLE hidDevice = CreateFile(detailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

            if (hidDevice != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attributes;
                if (HidD_GetAttributes(hidDevice, &attributes)) {
                    if (attributes.VendorID == vid && attributes.ProductID == pid) {
                        hid.handle = hidDevice;
                        hid.info = attributes;
                        hid_caps(hid);

                        SP_DEVINFO_DATA devInfoData;
                        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
                        if (SetupDiEnumDeviceInfo(deviceInfo, i, &devInfoData)) {
                            hid.port = GetUsbPortNumber(deviceInfo, devInfoData);
                        }
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

bool hid_connect(HID &hid, USHORT vid, USHORT pid, USHORT sernbr) {
    auto _hid = hid_open(vid, pid, sernbr);
    if (!_hid)
		return false;
	hid = _hid.value();
    return true;
}

bool hid_read(HID& hid, std::vector<uint8_t>& data) {
    if (hid.handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesRead;
    OVERLAPPED overlapped = { 0 };
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!overlapped.hEvent) {
        std::cerr << "CreateEvent failed with error: " << GetLastErrorAsString() << std::endl;
        return false;
    }

    data.resize(hid.inEplength);
    if (ReadFile(hid.handle, data.data(), static_cast<DWORD>(data.size()), &bytesRead, &overlapped) ||
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
    else {
        std::cerr << "ReadFile failed with error: " << GetLastErrorAsString() << std::endl;
    }

    CloseHandle(overlapped.hEvent);
    data.clear();
    return false;
}

void hid_stopread_thread(HID& hid) {
    if (hid.stopFlag) {
        *hid.stopFlag = true;
    }
    if (hid.readThread && hid.readThread->joinable()) {
        hid.readThread->join();
    }
    hid.readThread.reset();
}

void hid_log(const std::string& format_str, auto&&... args) {
    std::string fmtstr = std::vformat(format_str, std::make_format_args(args...));
    OutputDebugString(("HID: " + fmtstr).c_str());
}

void hid_read_thread(HID& hid, HIDReadCallback callback, void* userData) {

    hid.stopFlag = std::make_shared<std::atomic<bool>>(false); // Reset the stop flag
    hid.readThread = std::make_shared<std::thread>([&hid, callback, userData]() {
        while (!*hid.stopFlag) {
            std::vector<uint8_t> data;
            if (hid_read(hid, data)) {
                callback(hid, data, userData);
            }
            // Add a small sleep to prevent busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        hid_log("Read thread exiting...");
   });

}

bool hid_write(HID& hid, const std::vector<uint8_t>& data) {
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



