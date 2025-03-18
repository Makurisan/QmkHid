#include <optional>
#include <format> // For std::
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "hid.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static void hid_stop_read_thread(HID& hid);
static void hid_read_thread(HID& hid, HIDReadCallback callback, void* userData);
static bool hid_open(HID& hid, USHORT vid, USHORT pid, USHORT sernbr);
static void hid_log(const std::string& format_str, auto&&... args);

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
            hid.inEplength = static_cast<uint16_t>(caps.InputReportByteLength);
            hid.outEplength = static_cast<uint16_t>(caps.OutputReportByteLength);
           
            hid_log("-----------------------------------------\n");
            hid_log("VID: 0x{:04X}, PID: 0x{:04X}, SerialNr: 0x{:04X}\n", hid.info.VendorID, hid.info.ProductID, hid.info.VersionNumber);
            hid_log("Usage Page: {}\n", caps.UsagePage);
            hid_log("Usage: {}\n", caps.Usage);
            hid_log("Input Report Byte Length: {}\n", caps.InputReportByteLength);
            hid_log("Output Report Byte Length: {}\n", caps.OutputReportByteLength);
            hid_log("Feature Report Byte Length: {}\n", caps.FeatureReportByteLength);
            hid_log("Number of Link Collection Nodes: {}\n", caps.NumberLinkCollectionNodes);
            hid_log("Number of Input Button Caps: {}\n", caps.NumberInputButtonCaps);
            hid_log("Number of Input Value Caps: {}\n", caps.NumberInputValueCaps);
            hid_log("Number of Input Data Indices: {}\n", caps.NumberInputDataIndices);
            hid_log("Number of Output Button Caps: {}\n", caps.NumberOutputButtonCaps);
            hid_log("Number of Output Value Caps: {}\n", caps.NumberOutputValueCaps);
            hid_log("Number of Output Data Indices: {}\n", caps.NumberOutputDataIndices);
            hid_log("Number of Feature Button Caps: {}\n", caps.NumberFeatureButtonCaps);
            hid_log("Number of Feature Value Caps: {}\n", caps.NumberFeatureValueCaps);
            hid_log("Number of Feature Data Indices: {}\n", caps.NumberFeatureDataIndices);
        }
        else {
            hid_log("HidP_GetCaps failed with error: {}\n", GetLastErrorAsString());
        }
        HidD_FreePreparsedData(preparsedData);
    }
    else {
        hid_log("HidD_GetPreparsedData failed with error: {}\n", GetLastErrorAsString());
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

bool hid_open(HID &hid, USHORT vid, USHORT pid, USHORT sernbr) {
    //hid = { INVALID_HANDLE_VALUE, 0, 0, {} };
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to get device info\n";
        return false;
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
                        return true;
                    }
                }
                CloseHandle(hidDevice);
            }
        }
        free(detailData);
    }
    SetupDiDestroyDeviceInfoList(deviceInfo);
    return false;
}

void hid_close(HID& hid) {
    hid_stop_read_thread(hid);
    CloseHandle(hid.handle);
	hid.handle = INVALID_HANDLE_VALUE;
}

bool hid_connect(HID &hid, USHORT vid, USHORT pid, HIDReadCallback callback) {
    auto retHid = hid_open(hid,vid, pid, 0);
    if (!retHid)
		return false;
    hid_read_thread(hid, callback, nullptr);
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
    bool readSuccess = false;

    if (ReadFile(hid.handle, data.data(), static_cast<DWORD>(data.size()), &bytesRead, &overlapped) ||
        GetLastError() == ERROR_IO_PENDING) {
        HANDLE events[] = { overlapped.hEvent,  *hid.stopReadEvent };
        DWORD waitResult = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE); // Wait for either event
        if (waitResult == WAIT_OBJECT_0) {
            if (GetOverlappedResult(hid.handle, &overlapped, &bytesRead, FALSE)) {
                if (bytesRead > 0) {
                    data.resize(bytesRead);
                    readSuccess = true;
                    return readSuccess;
                }
            }
        }
        else if (waitResult == WAIT_OBJECT_0 + 1) {
            // Stop event was signaled
            readSuccess = false;
        }
}
    else {
        hid_log("ReadFile failed with error:{}\n", GetLastErrorAsString());
    }

    CloseHandle(overlapped.hEvent);
    data.clear();
    return readSuccess;
}

void hid_stop_read_thread(HID& hid) {
    *hid.stopFlag = true; // Set the stop flag
    if (hid.stopReadEvent && *hid.stopReadEvent) {
        SetEvent(*hid.stopReadEvent); // Signal the stop event
    }
    if (hid.readThread) {
        hid.readThread->request_stop(); // Request the thread to stop
        hid.readThread->join(); // Join the thread to ensure it has exited
    }
    hid.readThread.reset();
}

void hid_log(const std::string& format_str, auto&&... args) {
    std::string fmtstr = std::vformat(format_str, std::make_format_args(args...));
    OutputDebugString(("HID: " + fmtstr).c_str());
}

static void hid_read_func_thread(HID& hid, HIDReadCallback callback, void* userData) {
    while (WaitForSingleObject(*hid.stopReadEvent, 10) == WAIT_TIMEOUT) {
        std::vector<uint8_t> data;
        if (hid_read(hid, data)) {
            callback(hid, data, userData);
        }
		// not needed if WaitForSingeObject is used
        //std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    hid_log("Read thread {} exiting...\n", "0");
}

void hid_read_thread(HID& hid, HIDReadCallback callback, void* userData) {
    hid.stopReadEvent = std::make_shared<HANDLE>(CreateEvent(NULL, TRUE, FALSE, NULL));
    hid.stopFlag = std::make_shared<std::atomic<bool>>(false); // Reset the stop flag

    hid.readThread = std::make_shared<std::jthread>(hid_read_func_thread, std::ref(hid), callback, userData);
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
        hid_log("WriteFile failed with error: {}", GetLastErrorAsString());
    }
    CloseHandle(overlapped.hEvent);
    return false;
}



