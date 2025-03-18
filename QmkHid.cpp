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
#include "hid.h"
#include "json.hpp"
#include "msgpack.h"

#include "Resource.h"
#include "DeviceNameWindow.h"

std::mutex mtx;

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Msimg32.lib")

using json = nlohmann::json;
using namespace std::chrono;

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT 1002
#define ID_TRAY_WRITE 1003

#define IDT_TIMER1 1013
#define IDT_LAYER_SWITCH 1014
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

typedef struct _Support {
    std::string name;
    ProductType type;
    USHORT vid;
    USHORT pid;
    USHORT sernbr;
}DeviceSupport;

typedef struct _HIDData {
    USHORT seqnr;
    ProductType type;
    std::shared_ptr<HID> hid;
	std::vector<uint8_t> readData;
	std::vector<uint8_t> writeData;
    USHORT curLayer;
	SHORT curKey;   // last key pressed
}HIDData;

typedef struct _QMKHID {
    std::vector<HIDData> hidData;
	std::vector<DeviceSupport> usbSuppDevs;
    HICON iTrayIcon;
}QMKHID;


QMKHID qmkData = {
	.hidData = {},
    .usbSuppDevs = {
        {"QMK", QMK, QMK_VID, QMK_PID, 0}, // DeviceSupport instances
        {"StreamDeck", StreamDeck, STMDECK_VID, STMDECK_PID, 0}, // DeviceSupport instances
    },
    .iTrayIcon = nullptr,
};

NOTIFYICONDATA nid;
HWND hTrayWnd;
HWND hChildWnd;

std::vector<std::pair<int, steady_clock::time_point>> layerSwitches;
void readCallback(HID& hid, const std::vector<uint8_t>& data, void* userData);

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
    nid.hIcon = CreateIconWithNumber(qmkData.hidData[0].curLayer, IsDarkTheme());
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void InitNotifyIconData() {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hTrayWnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    nid.uCallbackMessage = WM_TRAYICON;
//nid.hIcon = CreateIconWithNumber(currentLayer, IsDarkTheme());
    strcpy_s(nid.szTip, "Foot Switch\nomrsh31h");
    qmkData.iTrayIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_QMKHID));
}

void ShowNotification(const HIDData& hidData,const char* title, const char* message) {
    nid.uFlags = NIF_INFO | NIF_ICON;
    strcpy_s(nid.szInfoTitle, title);
    strcpy_s(nid.szInfo, message);
    nid.dwInfoFlags = NIIF_NONE; // No sound
    // Load the standard application icon
    qmkData.iTrayIcon = hidData.hid->handle != INVALID_HANDLE_VALUE?
        CreateIconWithNumber(hidData.curLayer, IsDarkTheme()): qmkData.iTrayIcon;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void ResetLayerSwitchTimer() {
    // Kill the existing timer if it exists
    KillTimer(hTrayWnd, IDT_LAYER_SWITCH);
    // Set a new timer for 200 ms
    SetTimer(hTrayWnd, IDT_LAYER_SWITCH, 400, NULL);
}

void ShowChildWindow() {
    ShowWindow(hChildWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hChildWnd);
    // Set a timer to hide the window after 2 seconds
    SetTimer(hTrayWnd, IDT_HIDE_WINDOW, 2000, NULL);
}

void HideChildWindow() {
    ShowWindow(hChildWnd, SW_HIDE);
}

void ProcessLayerSwitches() {
    if (!layerSwitches.empty()) {
        std::lock_guard<std::mutex> lock(mtx); // Lock the mutex to ensure thread-safe
        auto lastEntry = layerSwitches.back();
        qmkData.hidData[0].curLayer = lastEntry.first;
        layerSwitches.clear();
        InvalidateRect(hChildWnd, NULL, TRUE);
        ShowChildWindow();
        UpdateTrayIcon();
    }
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

HIDData* findHidFromPort(QMKHID& qmkData, const std::string& port) {
    auto it = std::ranges::find_if(qmkData.hidData, [&port](const HIDData& data) {
        return data.hid->port.has_value() && data.hid->port.value() == port;
        });
    return (it != qmkData.hidData.end()) ? &(*it) : nullptr;
}

std::optional<DeviceSupport> findDeviceSupport(const QMKHID& qmkData, const uint16_t vid, const uint16_t pid);
std::optional<HIDData*> findMatchingHIDDevice(QMKHID& qmkData, const std::string& deviceName);

bool OpenHidDevice(QMKHID& qmkData, const std::string& deviceName) {
    bool anyDeviceOpened = false;
    // Reopen if hidData is not null
    auto hidDataOpt = findMatchingHIDDevice(qmkData, deviceName);

    if (hidDataOpt.has_value()) {
        HIDData* hidData = *hidDataOpt;
        if (!hid_connect(*hidData->hid, hidData->hid->info.VendorID, hidData->hid->info.ProductID, readCallback)) {
            InvalidateRect(hChildWnd, NULL, TRUE);
            ShowNotification(*hidData, "FootSwitch Device Status:", (hidData->hid->port.value() + " not ready").c_str());
        }
        else {
            // Set the hid_read() function to be non-blocking.
            hidData->readData.resize(hidData->hid->inEplength);
            hidData->writeData.resize(hidData->hid->outEplength);
            anyDeviceOpened = true;
        }
    }
    else {
        DeviceNameParser devicName(deviceName);
        if (devicName.getVID().has_value() && devicName.getPID().has_value()) {
            auto deviceSupport = findDeviceSupport(qmkData, devicName.getVID().value(), devicName.getPID().value());
            if (deviceSupport.has_value()) {
                qmkData.hidData.push_back({ 0, NoBoard, std::make_shared<HID>(HID{ INVALID_HANDLE_VALUE, 0, 0, { sizeof(HIDD_ATTRIBUTES), 0, 0, 0 } }), {}, {}, 0, 0 });
                HIDData& adHidData = qmkData.hidData.back();
                if (hid_connect(*adHidData.hid, devicName.getVID().value(), devicName.getPID().value(), readCallback)) {
                    devicName.log();
                    adHidData.readData.resize(adHidData.hid->inEplength);
                    adHidData.writeData.resize(adHidData.hid->outEplength);
                    adHidData.type = deviceSupport->type;
                    anyDeviceOpened = true;
                }
                else {
                    qmkData.hidData.pop_back();
                }
            }
        }
    }
    return anyDeviceOpened;
}

bool OpenAllSupportedHidDevices(QMKHID& qmkData) {
    bool anyDeviceOpened = false;
    // Search through supported devices and open if found
    for (const auto& device : qmkData.usbSuppDevs) {
        qmkData.hidData.push_back({ 0, NoBoard, std::make_shared<HID>(HID{ INVALID_HANDLE_VALUE, 0, 0, { sizeof(HIDD_ATTRIBUTES), 0, 0, 0 } }), {}, {}, 0, 0 });
        HIDData& adHidData = qmkData.hidData.back();
        if (!hid_connect(*adHidData.hid, device.vid, device.pid, readCallback)) {
            InvalidateRect(hChildWnd, NULL, TRUE);
            ShowNotification(adHidData, "FootSwitch Device Status:", (device.name + " not ready").c_str());
            qmkData.hidData.pop_back();
        }
        else {
            // Set the hid_read() function to be non-blocking.
            adHidData.readData.resize(adHidData.hid->inEplength);
            adHidData.writeData.resize(adHidData.hid->outEplength);
            adHidData.type = device.type;
            anyDeviceOpened = true;
        }
    }
    if (anyDeviceOpened) {
        ShowNotification(qmkData.hidData[0], "FootSwitch Device Status:",
            (qmkData.hidData[0].hid->port.value() + " ready").c_str());
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
        std::string text = "FootSwitch\nLayer: " + std::to_string(qmkData.hidData[0].curLayer);

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
    int x = workArea.right - 300;
    int y = workArea.top + 10;

    // Create the child window
    hChildWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, "Notification", WS_POPUP, x, y, 290, 100, NULL, NULL, wc.hInstance, NULL);

    // Set the layered window attributes to make the background transparent
    SetLayeredWindowAttributes(hChildWnd, 0, 255, LWA_ALPHA);

    // Initially hide the child window
    ShowWindow(hChildWnd, SW_HIDE);
}

std::optional<HIDData*> findMatchingHIDDevice(QMKHID& qmkData, const std::string& deviceName) {
    std::string upperDevName = deviceName;
    std::transform(upperDevName.begin(), upperDevName.end(), upperDevName.begin(), [](unsigned char c) { return std::toupper(c); });

    for (auto& hidData : qmkData.hidData) {
        if (hidData.hid->port.has_value()) {
            std::string upperPort = *hidData.hid->port;
            std::transform(upperPort.begin(), upperPort.end(), upperPort.begin(), [](unsigned char c) { return std::toupper(c); });
            qmk_log("Hid port: {} == {}\n", upperPort, upperDevName);
            if (strstr(upperDevName.c_str(), upperPort.c_str()))
                return &hidData;
        }
    }
    return std::nullopt;
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVICEREMOVECOMPLETE) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;

                auto hidData = findMatchingHIDDevice(qmkData, pDevInf->dbcc_name);
                if (hidData.has_value()) {

                    // Check if the device matches our VID and PID
                    DeviceNameParser devicName(pDevInf->dbcc_name);
                    if (hidData.has_value() && devicName.isQMKHidInterface(*(*hidData)->hid)) {
                        qmk_log("Device removed: {}\n", pDevInf->dbcc_name);
                        // Remove comes more than one, because of the multiple interfaces
                        if (hidData.has_value() && (*hidData)->hid->handle != INVALID_HANDLE_VALUE) {
                            hid_close(*(*hidData)->hid);
                            (*hidData)->curLayer = 0;
                            ShowNotification(**hidData, "FootSwitch Device Status:", "Device unplugged");
                        }

                    }

                }
            }
        }
        else if (wParam == DBT_DEVICEARRIVAL) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                /*
                •	Interface Number: The MI part in the device name is followed by a two-digit 
                    number that specifies the interface number. For example, &MI_01 refers to 
                    interface 1, &MI_02 refers to interface 2, and so on.
                */
                // our interface is on MI_01
                DeviceNameParser devicName(pDevInf->dbcc_name);
                if (devicName.getMI().value() == "&MI_01") {
                    OpenHidDevice(qmkData, pDevInf->dbcc_name);
                }
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
                PostQuitMessage(0);
                break;
        }
        break;
    case WM_TIMER:
        if (wParam == IDT_TIMER1) {
            // Redraw the child window periodically
            InvalidateRect(hChildWnd, NULL, TRUE);
        }
        else if (wParam == IDT_LAYER_SWITCH) {
            ProcessLayerSwitches();
            KillTimer(hwnd, IDT_LAYER_SWITCH); // Stop the timer after processing
        }
        else if (wParam == IDT_HIDE_WINDOW) {
            HideChildWindow();
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
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

std::optional<DeviceSupport> findDeviceSupport(const QMKHID& qmkData, const uint16_t vid, const uint16_t pid) {
    auto it = std::ranges::find_if(qmkData.usbSuppDevs, [vid, pid](const DeviceSupport& device) {
        return device.vid == vid && device.pid == pid;
        });

    if (it != qmkData.usbSuppDevs.end()) {
        return *it;
    }
    else {
        return std::nullopt;
    }
}

HIDData* findHidData(QMKHID& qmkData, const HID& hid) {
    auto it = std::ranges::find_if(qmkData.hidData, [&hid](const HIDData& data) {
        return data.hid->handle == hid.handle;
        });
    return (it != qmkData.hidData.end()) ? &(*it) : nullptr;
}

void readCallback(HID& hid, const std::vector<uint8_t>& data, void* userData) {
    // Get the HIDData object associated with the HID device
    HIDData* phidData = findHidData(qmkData, hid);
    if (!phidData) return;
    // remove the first report id byte from data
    phidData->readData = std::vector<uint8_t>(data.begin(), data.end());

    // Try to read from the device.
    if (data.size() == phidData->hid->inEplength) {
        auto devSupport = findDeviceSupport(qmkData, hid.info.VendorID, hid.info.ProductID);
        // Check the USB device
        if (devSupport->type == StreamDeck) { // repid for btn pressed is data[0] == 1
            // Set to the StreamDeck HID input
            auto report = reinterpret_cast<StreamDeckHIDIn*>(&phidData->readData[0]);

            // Display all bits set in the buttonStates array
            std::string bitString;
            for (int i = 0; i < sizeof(report->buttonStates); ++i) {
                bitString += (report->buttonStates[i] == 1) ? '1' : '0';
                bitString += ' ';
            }
            bitString += '\n';
            OutputDebugString(bitString.c_str());

        }
        else if (devSupport->type == QMK) {
            msgpack_t km;
            if (read_msgpack(&km, phidData->readData)) {
                try {
                    make_msgpack(&km, phidData->writeData);
                    msgpack_log(&km);
                    /*hid_write(*phidData->hid, phidData->writeData);*/
                }
                catch (json::parse_error& e) {
                    ShowNotification(*phidData, "Device Status", "Wrong data from the USB Device");
                }
            }
        }
    }
    else if (data.size() == 0) {
        ; // No data received
    }
    else if (data.size() == -1) {
        InvalidateRect(hChildWnd, NULL, TRUE);
        std::string msgerr = hid_error(hid);
        ShowNotification(*phidData, "Foot Switch", msgerr.c_str());
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
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WindowProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, "HIDTest", NULL };
    RegisterClassEx(&wc);
    hTrayWnd = CreateWindow(wc.lpszClassName, "OMRS31H Foot Switch", WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);

    // Initialize the NOTIFYICONDATA structure
    InitNotifyIconData();
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Register for device notifications
    RegisterDeviceNotification(hTrayWnd);

    // Create the child window
    CreateChildWindow();

    // Set a timer to redraw the child window periodically
    SetTimer(hTrayWnd, IDT_TIMER1, 1000, NULL); // 1000 ms = 1 second

    // Try to open the HID device initially
    OpenAllSupportedHidDevices(qmkData);

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
        Sleep(100); // Sleep for 100 ms
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    for (auto& hidData : qmkData.hidData) {
        hid_close(*hidData.hid);
    }
    return 0;
}
