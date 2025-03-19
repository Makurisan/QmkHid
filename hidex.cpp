#include <optional>
#include <format> // For std::
#include <thread>
#include <atomic>
#include <mutex>
#include <locale>
#include <codecvt>
#include <condition_variable>
#include "hidex.h"
#include "DeviceNameWindow.h"
#include "hidapi/hidapi.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

extern "C" struct hid_device_info* hid_internal_get_device_info(const wchar_t* path, HANDLE handle);

static void hid_stop_read_thread(HID& hid);
static void hid_read_thread(HID& hid, HIDReadCallback callback, void* userData);
static bool hid_open(HID& hid, USHORT vid, USHORT pid, USHORT sernbr);
static void hid_log(const std::string& format_str, auto&&... args);

static std::vector<DeviceNameParser> _systemHids;
static std::vector<DeviceSupport> _supportedDevices;

std::string wstringToString(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

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

    if (HidD_GetPreparsedData(hid.handle, &preparsedData)) {
        HIDP_CAPS caps;
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            hid.inEplength = static_cast<uint16_t>(caps.InputReportByteLength);
            hid.outEplength = static_cast<uint16_t>(caps.OutputReportByteLength);
           
            hid_log("------------------------------------------------\n");
            hid_log("VID: 0x{:04X}, PID: 0x{:04X}, SerialNr: 0x{:04X}\n", hid.info.vid, hid.info.pid, hid.info.sernr);
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
            hid_log("HidP_GetCaps failed with error: {}\n", hid_error(hid));
        }
        HidD_FreePreparsedData(preparsedData);
    }
    else {
        hid_log("HidD_GetPreparsedData failed with error: {}\n", hid_error(hid));
    }
}

void hid_device_info_log(const hid_device_info* dev) {
    if (dev == nullptr) {
        return;
    }

    hid_log("Device Info:\n");
    hid_log("  Path: {}\n", dev->path ? dev->path : "N/A");
    hid_log("  Vendor ID: 0x{:04X}\n", dev->vendor_id);
    hid_log("  Product ID: 0x{:04X}\n", dev->product_id);
    hid_log("  Serial Number: {}\n", dev->serial_number ? dev->serial_number : L"N/A");
    hid_log("  Release Number: 0x{:04X}\n", dev->release_number);
    hid_log("  Manufacturer String: {}\n", dev->manufacturer_string ? dev->manufacturer_string : L"N/A");
    hid_log("  Product String: {}\n", dev->product_string ? dev->product_string : L"N/A");
    hid_log("  Usage Page: 0x{:04X}\n", dev->usage_page);
    hid_log("  Usage: 0x{:04X}\n", dev->usage);
    hid_log("  Interface Number: {}\n", dev->interface_number);
}

void hid_list(std::vector<DeviceSupport> system, const std::vector<DeviceSupport>& supported){

    hid_init();

    hid_device_info * devs = hid_enumerate(0, 0);
    struct hid_device_info* d = devs;
    _systemHids.clear();

    hid_log("{} ...........................................  \n", __FUNCTION__);

    while (d) {

		auto cHidNameParser = DeviceNameParser(d->path);
		if (cHidNameParser.getVID().has_value() && cHidNameParser.getPID().has_value()) {
			auto it = std::find_if(supported.begin(), supported.end(), [&cHidNameParser](const DeviceSupport& supp) {
				return cHidNameParser.getVID().value() == supp.vid && cHidNameParser.getPID().value() == supp.pid;
				});
			if (it != supported.end()) {
			    DeviceSupport fSupport = *it;
                fSupport.serial_number = wstringToString(d->serial_number);
                fSupport.manufactor = wstringToString(d->manufacturer_string);
                if (cHidNameParser.getMI().has_value() && cHidNameParser.getMI() == fSupport.iface) {
                    system.push_back(fSupport);
                    hid_log("Relevant Device: {} - {} \n", d->manufacturer_string, d->path);
                    hid_device_info_log(d);
                }else
				if (!cHidNameParser.getMI().has_value() && fSupport.iface == "") {
                    system.push_back(fSupport);
                    hid_log("Relevant Device: {} \n", d->path);
				}
			}
		}
		d = d->next;
    }
    hid_free_enumeration(devs);
    hid_exit();
}

void _hid_list(std::vector<DeviceNameParser>& _topen, const std::vector<DeviceSupport>& supported) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to get device info\n";
        return;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    _systemHids.clear();
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
                    auto cHidNameParser =  DeviceNameParser(detailData->DevicePath);

                    // Check if the vid and pid from cHidNameParser are in the supported list
                    if (cHidNameParser.getVID().has_value() && cHidNameParser.getPID().has_value()) {

                        auto it = std::find_if(supported.begin(), supported.end(), [&cHidNameParser](const DeviceSupport& supp) {
                            return cHidNameParser.getVID().value() == supp.vid && cHidNameParser.getPID().value() == supp.pid;
                            });

                        if (it != supported.end()) {
                            hid_log("Relevant Device: {} \n", detailData->DevicePath);
                            const DeviceSupport& fSupport = *it;
                            if (cHidNameParser.getMI().has_value() && cHidNameParser.getMI() == fSupport.iface) {
                                _systemHids.push_back(cHidNameParser);
                                _topen.push_back(cHidNameParser);
                            }
                            if (!cHidNameParser.getMI().has_value() && fSupport.iface == "") {
                                _systemHids.push_back(cHidNameParser);
                                _topen.push_back(cHidNameParser);
                            }
                        }
                    }
                }
                CloseHandle(hidDevice);
            }
        }
        free(detailData);
    }
    SetupDiDestroyDeviceInfoList(deviceInfo);
}

void hid_open_list(std::vector<DeviceSupport>& toopen, const std::vector<DeviceSupport>& supported) {
	
    toopen.clear();

    std::vector<DeviceSupport> milist;
    std::vector<DeviceSupport> SystemOpen;
    hid_list(milist, supported);

    toopen = milist;

    //_hid_list(milist, supported);
    // Assign milist to toopen
    //for (const auto& device : milist) {
    //    auto it = std::find_if(supported.begin(), supported.end(), [&device](const DeviceSupport& supp) {
    //        return device.getVID().has_value() && device.getPID().has_value() &&
    //            device.getVID().value() == supp.vid && device.getPID().value() == supp.pid;
    //        });
    //    if (it != supported.end()) {
    //        toopen.push_back({ device.getDevName(), it->type, device.getVID().value(), device.getPID().value(), 0 });
    //    }
    //}

}

bool hid_open(HID &hid, const std::string& devName) {

    hid.handle = CreateFile(devName.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

	if (hid.handle != INVALID_HANDLE_VALUE) {
        DeviceNameParser devNameParse(devName);
		HIDD_ATTRIBUTES attributes;
		if (HidD_GetAttributes(hid.handle, &attributes)) {
            hid.info.vid = attributes.VendorID;
            hid.info.pid = attributes.ProductID;
            hid.info.sernr = attributes.VersionNumber;
            hid.info.devname = devName;
            hid.port = devNameParse.getPort();
            hid_caps(hid);
			return true;
		}
		CloseHandle(hid.handle);
	}
    return false;
}


void hid_close(HID& hid) {
    hid_stop_read_thread(hid);
    if(hid.handle != INVALID_HANDLE_VALUE)
        CloseHandle(hid.handle);
	hid.handle = INVALID_HANDLE_VALUE;
}

bool hid_connect(HID &hid, std::string devname, HIDReadCallback callback) {
    auto retHid = hid_open(hid, devname);
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
        hid_log("ReadFile failed with error:{}\n", hid_error(hid));
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



