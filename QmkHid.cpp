#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <dwmapi.h>
#include <chrono>
#include <wingdi.h>
#include <dbt.h>
#include <initguid.h>
#include <hidclass.h>
#include <thread>
#include <mutex>
#include <sstream>
#include <ranges>
#include <regex>
#include <functional>
#include <shlobj.h>
#include <iostream>

#include "hidex.h"
#include "sqlite/sqlite3.h"
#include "json.hpp"
#include "msgpack.h"

#include "CallbackHandler.h"

#include "qmkhid.h"
#include "Resource.h"
#include "DeviceNameWindow.h"
#include "database.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Msimg32.lib")

using json = nlohmann::json;
using namespace std::chrono;

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT 1002
#define ID_TRAY_WRITE 10031

#define IDT_HIDE_WINDOW 1015

#define QMK_VID 0x35EE //0xFEED QMK default VID
#define QMK_PID 0x1308 //0x1308 Your keyboard PID

// Streamdeck
#define STMDECK_VID 0x0fd9
#define STMDECK_PID 0x0080

typedef struct _StreamDeckHIDIn {
    uint8_t reportID[4]; // Report ID to identify the report type
    uint8_t buttonStates[15]; // Button states (adjust size as needed)
} StreamDeckHIDIn;

enum ProductType {
    NoBoard = 0,
    StreamDeck,
    QMK
};

typedef struct _HIDData {
    uint32_t seqnr;
    uint8_t type;
    std::shared_ptr<HID> hid;
	std::vector<uint8_t> readData;
	std::vector<uint8_t> writeData;
    uint8_t curLayer;// current layer if qmk sends it
    uint16_t curKey;   // last key pressed
}HIDData;

typedef struct _QMKHIDPREFERENCE {
	USHORT seqnr;
    uint8_t curLayer;
	uint8_t showTime;       // time to show the client window
	uint8_t showLayerSwitch; // show layer switch in the client window
    std::string windowPos; // serialized RECT, status window position
	std::string traydev; // USB device which shows its state in the tray
}QMKHIDPREFERENCE;

typedef struct _QMKHID {
    std::vector<HIDData> hidData;
    std::vector<DeviceSupport> usbSuppDevs;// devices which are allowed
    std::vector<DeviceSupport> dbSuppDevs; // active/inactive devices on the usb bus
    std::shared_ptr<sqlite3> sqLite;
    HICON iTrayIcon;
    std::atomic<bool> winTimerActive;    
 // preferences
    uint8_t curLayer; // Make curLayer atomic
    uint16_t showTime; // time to show the client window
    std::string windowPos; // serialized RECT, status window position
    uint8_t showLayerSwitch; // show layer switch in the client window
}QMKHID;


QMKHID qmkData = {
	.hidData = {},
    .usbSuppDevs = {
        // the active shows the state in the taskbar
        {0, true, "QMK", QMK, QMK_VID, QMK_PID, 0, "&MI_01"}, // DeviceSupport instances
        {0, false, "StreamDeck", StreamDeck, STMDECK_VID, STMDECK_PID, 0, ""}, // DeviceSupport instances
        {0, false, "QMK", QMK, 0x4653, 0x0001, 0, "&MI_01"}, // DeviceSupport instances
    },
    .dbSuppDevs = {},
    .iTrayIcon = nullptr,
};

NOTIFYICONDATA nid;
HWND hTrayWnd;
HWND hChildWnd;

void readCallback(HID& hid, const std::vector<uint8_t>& data, void* userData);
std::optional<HIDData*> findMatchingPortDevice(QMKHID& qmkData, const std::string& deviceName);
static bool OpenHidDevices(QMKHID& qmkData, std::vector<DeviceSupport>& devsupport);

bool caseInsensitiveCompare(const std::string& str1, const std::string& str2) {
    return std::equal(str1.begin(), str1.end(), str2.begin(), str2.end(),
        [](char a, char b) {
            return std::tolower(a) == std::tolower(b);
        });
}

void qmk_log(const std::string& format_str, auto&&... args) {
    std::string formatted_str = std::vformat(format_str, std::make_format_args(args...));
    OutputDebugString(formatted_str.c_str());
}

bool IsDarkTheme() {
    DWORD value = 0;
    DWORD valueSize = sizeof(value);
    HKEY hKey;

    // Open the registry key for the current user
    if (RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        // Query the value of the AppsUseLightTheme key
        if (RegQueryValueEx(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &valueSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return value == 0; // 0 means dark mode is enabled
        }
        RegCloseKey(hKey);
    }
 
    // Default to light theme if the registry key is not found
    return false;
}

HICON CreateIconWithNumber(int number, bool darkTheme) {
    // Create a 32x32 bitmap
    HDC hdc = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, 32, 32);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

    // Fill the background with the appropriate color
    RECT rect = { 0, 0, 32, 32 };
    HBRUSH hBrush = CreateSolidBrush(darkTheme ? RGB(0, 0, 0) : RGB(255, 255, 255));
    FillRect(hMemDC, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw the number
    HFONT hFont = CreateFont(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
    HFONT hOldFont = (HFONT)SelectObject(hMemDC, hFont);
    SetBkMode(hMemDC, TRANSPARENT);
    SetTextColor(hMemDC, darkTheme ? RGB(255, 255, 255) : RGB(0, 0, 0));
    std::string text = "[" + std::to_string(number) + "]";
    DrawText(hMemDC, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hMemDC, hOldFont);
    DeleteObject(hFont);

    // Create the icon
    ICONINFO iconInfo = { 0 };
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = hBitmap;
    iconInfo.hbmColor = hBitmap;
    HICON hIcon = CreateIconIndirect(&iconInfo);

    // Clean up
    SelectObject(hMemDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hdc);

    return hIcon;
}

void UpdateTrayIcon() {
    nid.uFlags = NIF_ICON; // Set the flag to update only the icon
    nid.hIcon = qmkData.hidData.size()?
        CreateIconWithNumber(qmkData.curLayer, IsDarkTheme()) : qmkData.iTrayIcon;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void InitNotifyIconData() {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hTrayWnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    nid.uCallbackMessage = WM_TRAYICON;
    strcpy_s(nid.szTip, "Foot Switch\nomrsh31h");
    qmkData.iTrayIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_QMKHID));
    nid.hIcon = qmkData.iTrayIcon;
}

void ShowNotification(const HIDData& hidData,const char* title, const char* message) {
    nid.uFlags = NIF_INFO | NIF_ICON;
    strcpy_s(nid.szInfoTitle, title);
    strcpy_s(nid.szInfo, message);
    nid.dwInfoFlags = NIIF_NONE; // No sound

    // Check if hidData is uninitialized
    if (hidData.hid == nullptr || hidData.hid->handle == INVALID_HANDLE_VALUE) {
        qmkData.iTrayIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_QMKHID));
    }
    else {
        // Load the standard application icon
        qmkData.iTrayIcon = CreateIconWithNumber(qmkData.curLayer, IsDarkTheme());
    }
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// Thread callback to show the layer switch window and start to hide it
// This function is started inside readCallback
void LayerWindowSwitchCallback(int curlayer, const std::string& type) {
	// Check if the child window is already visible
	if (IsWindowVisible(hChildWnd)) {
		qmk_log("Child window is already visible\n");
	}
	// Kill running timer
	KillTimer(hChildWnd, IDT_HIDE_WINDOW);

    ShowWindow(hChildWnd, SW_SHOWNOACTIVATE);
    InvalidateRect(hChildWnd, NULL, TRUE);
   // Set a timer to hide the window
    SetTimer(hTrayWnd, IDT_HIDE_WINDOW, qmkData.showTime, NULL);
    UpdateTrayIcon();
}

void RegisterDeviceNotification(HWND hwnd) {
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;

    HDEVNOTIFY hDeviceNotify = RegisterDeviceNotification(hwnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!hDeviceNotify) {
        MessageBox(hwnd, "Failed to register for device notifications.", "Error", MB_OK | MB_ICONERROR);
    }
}

// this function is called from Arrival with the WM_DEVICECHANGE message
// - if the device is removed, the hidData is removed from the qmkData.hidData vector
//   but the device exists further in the dbSuppDevs vector
// - if the device arrived, we must check if the device is in the dbSuppDevs vector
//   and if it is active, we must open the device otherwise we ignore it
// - if the device is not in the dbSuppDevs but in the usbSuppDevs we ...

bool OpenArrivedHidDevice(QMKHID& qmkData, const std::string& dev) {
    auto it = std::ranges::find_if(qmkData.dbSuppDevs, [&dev](const DeviceSupport& devsupport) {
        return devsupport.dev == dev;
        });

    if (it != qmkData.dbSuppDevs.end()) {
        std::vector<DeviceSupport> devsupport;
        devsupport.push_back(*it);
		// active check is done in OpenHidDevices
        return OpenHidDevices(qmkData, devsupport);
    }
    else {
		qmk_log("Device not found in dbSuppDevs: {}\n", dev);       
		// be careful with the device name, it can be a bluetooth device
		auto cdevParser = DeviceNameParser(dev);
		auto it = std::ranges::find_if(qmkData.usbSuppDevs, [&cdevParser](const DeviceSupport& device) {
			return cdevParser.isRegularHidDevice() && cdevParser.getVID().value() == device.vid && device.pid == cdevParser.getPID().value();
			});
        if (it != qmkData.usbSuppDevs.end()) {
			std::vector<DeviceSupport> devsupport;
			std::vector<DeviceSupport> system;
            devsupport.push_back(*it);
            // get the active USB devices
            if (hid_open_list(system, devsupport)) {
                // pick the right one
				auto it = std::ranges::find_if(system, [&dev](const DeviceSupport& devsupport) {
					return devsupport.dev == dev;
					});
                if (it != system.end()) {
                    std::vector<DeviceSupport> updateDb;
                    DeviceSupport suppdev = *it;
                    // set a new arrived and allowed usbdevice to true
                    suppdev.active = true;
                    updateDb.push_back(suppdev);
                    // push it also to our database usbdevice list
				    qmkData.dbSuppDevs.push_back(suppdev);
                    sqlite_add_update_devicesupport(qmkData.sqLite.get(), updateDb);
                    OpenHidDevices(qmkData, updateDb);
                }
            }
        }
    }
    return false;
}

bool OpenHidDevices(QMKHID& qmkData, std::vector<DeviceSupport>& devsupport) {
    bool anyDeviceOpened = false;
    std::string manufactor, product;
   
    // Search through supported devices and open if found
    for (const auto& device : devsupport) {
        if (device.active) {
            qmkData.hidData.push_back({});
            HIDData& adHidData = qmkData.hidData.back();
            adHidData.hid = std::make_shared<HID>(HID{
                INVALID_HANDLE_VALUE,
                0,
                0,
                { device.vid, device.pid, 0 },
                std::nullopt,
                nullptr,
                nullptr,
                });
            if (!hid_connect(*adHidData.hid, device.dev, readCallback)) {
                qmkData.hidData.pop_back();
            }
            else {
                // Set the hid_read() function to be non-blocking.
                adHidData.readData.resize(adHidData.hid->inEplength);
                adHidData.writeData.resize(adHidData.hid->outEplength);
                adHidData.type = device.type;
                anyDeviceOpened = true;
                manufactor = device.manufactor;
                product = device.product;
            }
        }

    }
    if (anyDeviceOpened) {
		auto hidData = qmkData.hidData[0];
        ShowNotification(hidData, "Device Status:",
            (manufactor + " / " + product + " ready").c_str());
    }
    else {
        InvalidateRect(hChildWnd, NULL, TRUE);
        ShowNotification({0}, "Device Status:", "No plugged in QMK device found.");
    }
    return anyDeviceOpened;
}


LRESULT CALLBACK ChildWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {

        if (!IsWindowVisible(hwnd)) {
            return 0; // If the window is not visible, do not process WM_PAINT
        }

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Get the client rectangle
        RECT rect;
        GetClientRect(hwnd, &rect);

        // Create a memory device context to reduce flickering
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

        // Fill the background with transparency
        HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
        HPEN hPen = CreatePen(PS_SOLID, 4, RGB(255, 255, 255)); // White border
        HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);

        // Draw the rounded rectangle border
        RoundRect(hdcMem, rect.left, rect.top, rect.right, rect.bottom, 20, 20);

        // Create a font
        HFONT hFont = CreateFont(40, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
        HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

        // Set the text color and background mode
        bool darkTheme = IsDarkTheme();
        SetTextColor(hdcMem, darkTheme ? RGB(255, 255, 255) : RGB(0, 0, 0));
        SetBkMode(hdcMem, TRANSPARENT);

        // Specify the text to draw
        std::string text = "FootSwitch\nLayer: " + std::to_string(qmkData.curLayer);

        // Calculate the text rectangle
        RECT textRect = rect;
        DrawText(hdcMem, text.c_str(), -1, &textRect, DT_CALCRECT | DT_CENTER | DT_VCENTER | DT_WORDBREAK);

        // Center the text rectangle
        int textWidth = textRect.right - textRect.left;
        int textHeight = textRect.bottom - textRect.top;
        textRect.left = (rect.right - textWidth) / 2;
        textRect.top = (rect.bottom - textHeight) / 2;
        textRect.right = textRect.left + textWidth;
        textRect.bottom = textRect.top + textHeight;

        // Draw the text
        DrawText(hdcMem, text.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);

        // Copy the memory device context to the screen
        BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, hdcMem, 0, 0, SRCCOPY);

        // Clean up
        SelectObject(hdcMem, hOldFont);
        DeleteObject(hFont);
        SelectObject(hdcMem, hOldPen);
        DeleteObject(hPen);
        SelectObject(hdcMem, hOldBrush);
        DeleteObject(hBrush);
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // Prevent background erasing to avoid flickering
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

void CreateChildWindow() {
    // Get the work area of the primary monitor
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    // Register the child window class
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, ChildWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "FootswitchWindow", NULL };
    RegisterClassEx(&wc);

    // Calculate the position for the upper right corner
    #ifdef RIGHT_TOP
    int x = workArea.right - 300;
    int y = workArea.top + 10;
#else
	int x = workArea.left + 10;
	int y = workArea.top + 10;
#endif
    // Create the child window
    hChildWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
        wc.lpszClassName, "Notification", WS_POPUP, x, y, 290, 100, NULL, NULL, wc.hInstance, NULL);

    // Set the layered window attributes to make the background transparent
    SetLayeredWindowAttributes(hChildWnd, 0, 255, LWA_ALPHA);

    // Initially hide the child window
    ShowWindow(hChildWnd, SW_HIDE);
}

std::optional<HIDData*> findMatchingPortDevice(QMKHID& qmkData, const std::string& deviceName) {
    std::string upperDevName = StringEx::toUpper(deviceName);;

    for (auto& hidData : qmkData.hidData) {
        if (hidData.hid->port.has_value()) {
            std::string upperPort = StringEx::toUpper(*hidData.hid->port); ;
            std::transform(upperPort.begin(), upperPort.end(), upperPort.begin(), [](unsigned char c) { return std::toupper(c); });
            qmk_log("Hid port: {} == {}\n", upperPort, upperDevName);
            if (upperDevName.find(upperPort) != std::string::npos)
                return &hidData;
        }
    }
    return std::nullopt;
}

LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {

    case WM_CREATE:
        break;

    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVICEREMOVECOMPLETE) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;

                auto hidData = findMatchingPortDevice(qmkData, pDevInf->dbcc_name);
                if (hidData.has_value()) {
                    qmk_log("Device removed: {}\n", pDevInf->dbcc_name);
                    // Remove comes more than one, because of the multiple interfaces
                    HIDData& hidDataRef = *(*hidData);
                    if (hidDataRef.hid->handle != INVALID_HANDLE_VALUE) {
                        hid_close(*hidDataRef.hid);
                        hidDataRef.curLayer = 0;
                        ShowNotification(hidDataRef, "FootSwitch Device Status:", "Device unplugged");
                    }
                    // remove the hidData from qmkData.hidData
                    qmkData.hidData.erase(std::remove_if(qmkData.hidData.begin(), qmkData.hidData.end(),
                        [&hidDataRef](const HIDData& data) {
                            return data.hid->handle == hidDataRef.hid->handle;
                        }), qmkData.hidData.end());

                }
            }
        }
        else if (wParam == DBT_DEVICEARRIVAL) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                OpenArrivedHidDevice(qmkData, pDevInf->dbcc_name);
            }
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONDOWN) {
            POINT curPoint;
            GetCursorPos(&curPoint);
            SetForegroundWindow(hwnd);

            HMENU hMenu = CreatePopupMenu();
            InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_WRITE, "Write to HID");
            InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_EXIT, "Exit");

            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
            case ID_TRAY_WRITE: {
                msgpack_t msgpack = {0};
                init_msgpack(&msgpack);
                add_msgpack_add(&msgpack, MSGPACK_CURRENT_GETLAYER, -77);
				make_msgpack(&msgpack, qmkData.hidData[0].writeData);
                read_msgpack(&msgpack, qmkData.hidData[0].writeData);
                if (hid_write(*qmkData.hidData[0].hid, qmkData.hidData[0].writeData)) {
                    ShowNotification(qmkData.hidData[0], "HID Write", "Data written successfully");
                }
                else {
                    ShowNotification(qmkData.hidData[0], "HID Write", "Failed to write data");
                }
                break;
            }
            case ID_TRAY_EXIT:
                Shell_NotifyIcon(NIM_DELETE, &nid);
				DestroyWindow(hwnd);
                PostQuitMessage(0);
                break;
        }
        break;
    case WM_TIMER:
        if (wParam == IDT_HIDE_WINDOW) {
            KillTimer(hwnd, 1); 
            ShowWindow(hChildWnd, SW_HIDE);
        }
        break;
    case WM_QUERYENDSESSION:
        // Handle system shutdown or logoff
        return TRUE; // Indicate that the session can end
    case WM_ENDSESSION:
        if (wParam) {
            // System is shutting down or logging off
            for (auto& hidData : qmkData.hidData) {
                hid_close(*hidData.hid);
            }
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
        }
        break;
    case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		break;
    case WM_DESTROY:
        qmk_log("WM_DESTROY: Window is being destroyed\n");
        for (auto& hidData : qmkData.hidData) {
            hid_close(*hidData.hid);
        }
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void readCallback(HID& hid, const std::vector<uint8_t>& data, void* userData) {
	// Get the HIDData object associated with the HID device
	auto it = std::ranges::find_if(qmkData.hidData, [&hid](const HIDData& data) {
		return data.hid->handle == hid.handle;
		});
	if (it == qmkData.hidData.end()) {
		return;
	}

	HIDData& hidData  = *it;
	// remove the first report id byte from data
	hidData.readData = std::vector<uint8_t>(data.begin(), data.end());

	// Try to read from the device.
	if (data.size() == hidData.hid->inEplength) {

		// Check the USB device
		if (hidData.type == StreamDeck) { // repid for btn pressed is data[0] == 1
			// Set to the StreamDeck HID input
			auto report = reinterpret_cast<StreamDeckHIDIn*>(&hidData.readData[0]);

			// Display all bits set in the buttonStates array
			std::string bitString;
			for (int i = 0; i < sizeof(report->buttonStates); ++i) {
				bitString += (report->buttonStates[i] == 1) ? '1' : '0';
				bitString += ' ';
			}
			bitString += '\n';
			OutputDebugString(bitString.c_str());

		}
		else if (hidData.type == QMK) {
			msgpack_t km;
			if (read_msgpack(&km, hidData.readData)) {
				msgpack_log(&km);

				auto curLayer = msgpack_getValue(&km, MSGPACK_CURRENT_LAYER);
				if (curLayer.has_value()) {
					qmkData.curLayer = curLayer.value();

					// todo check the preference for showing the layer switch
					std::jthread timerThread2(CallbackThread<decltype(LayerWindowSwitchCallback),
						int, std::string>, LayerWindowSwitchCallback, curLayer.value(), "QMK");
				}
			}
			else {
				qmk_log("Wrong data from the USB Device\n");
				ShowNotification(hidData, "Device Status", "Wrong data from the USB Device");
			}
		}
	}
	else if (data.size() == 0) {
		; // No data received
	}
	else if (data.size() == -1) {
		InvalidateRect(hChildWnd, NULL, TRUE);
		std::string msgerr = hid_error(hid);
		ShowNotification(hidData, "Foot Switch", msgerr.c_str());
		hid_close(hid);
		hid.handle = nullptr;
	}
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Create a window class
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, TrayWindowProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, "QMkTrayIconWnd", NULL };
    RegisterClassEx(&wc);
    hTrayWnd = CreateWindow(wc.lpszClassName, "OMRS31H Foot Switch", WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);

    // Initialize the NOTIFYICONDATA structure
    InitNotifyIconData();
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Register for device notifications
    RegisterDeviceNotification(hTrayWnd);

    // Create the child window
    CreateChildWindow();

    auto opened = false;

    auto devCount = 0;;
    auto dbreturn = sqlite_database_open(qmkData.sqLite);
	if (dbreturn) {
        devCount = sqlite_tableCount(qmkData.sqLite.get(), "DeviceSupport");
    }
    else {
        qmk_log("Failed to get database connection");
        qmkData.sqLite = nullptr;
    }
	if (devCount > 0) {
		sqlite_get_devicesupport(qmkData.sqLite.get(), qmkData.dbSuppDevs);
        opened = OpenHidDevices(qmkData, qmkData.dbSuppDevs);
    }
	else {
		// Try to open the HID device initially
        std::vector<DeviceSupport> sysDeviceSupp;
        hid_open_list(sysDeviceSupp, qmkData.usbSuppDevs);
		OpenHidDevices(qmkData, sysDeviceSupp);
        sqlite_add_update_devicesupport(qmkData.sqLite.get(), sysDeviceSupp);
    }
    //sqlite_store_devicesupport(qmkData.sqLite.get(), allowedOpen);
    
	// preferences
	qmkData.showTime = 2000;

    // Message loop
    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
        }
        Sleep(10); // Sleep for 100 ms
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    for (auto& hidData : qmkData.hidData) {
        hid_close(*hidData.hid);
    }
    return 0;
}
