#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include "hid.h"
#include "json.hpp"
#include <dwmapi.h>
#include <chrono>
#include <wingdi.h>
#include <dbt.h>
#include <initguid.h>
#include <hidclass.h>
#include <thread>
#include <mutex>

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


typedef struct _HIDData {
	HID hid;
    std::vector<BYTE> readData;
    std::vector<BYTE> writeData;
    HWND hTrayWnd;
    HWND hChildWnd;
}HIDData;

#define VID 0x35EF //0xFEED QMK default VID
#define PID 0x1308 //0x1308 Your keyboard PID

HIDData hidData = {
    .hid = { INVALID_HANDLE_VALUE, 0, 0, 
            { sizeof(HIDD_ATTRIBUTES), VID, PID, 0 } },
     .readData = {},
    .writeData = {},
    .hTrayWnd = nullptr,
    .hChildWnd = nullptr,
};

NOTIFYICONDATA nid;
HWND hTrayWnd;
HWND hChildWnd;
int currentLayer = 0;
std::vector<std::pair<int, steady_clock::time_point>> layerSwitches;

// Report ID
// •    Report ID: The first byte in the report data that identifies the report type.
// •	Usage: It is used to distinguish between multiple reports that a single HID device might support.
// •	Value: If the device does not use report IDs, the report ID is typically set to 0.
bool is_json_object(HIDData* phidData) {
    try {
        auto j = json::parse(phidData->readData.begin()+1, phidData->readData.end());
        return j.is_object();
    }
    catch (json::parse_error&) {
        return false;
    }
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

void InitNotifyIconData() {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hTrayWnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = CreateIconWithNumber(currentLayer, IsDarkTheme());
    strcpy_s(nid.szTip, "Foot Switch\nomrsh31h");
}

void ShowNotification(const char* title, const char* message) {
    nid.uFlags = NIF_INFO;
    strcpy_s(nid.szInfoTitle, title);
    strcpy_s(nid.szInfo, message);
    nid.dwInfoFlags = NIIF_NONE; // No sound
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void ResetLayerSwitchTimer() {
    // Kill the existing timer if it exists
    KillTimer(hTrayWnd, IDT_LAYER_SWITCH);
    // Set a new timer for 200 ms
    SetTimer(hTrayWnd, IDT_LAYER_SWITCH, 400, NULL);
}

void ShowChildWindow() {
    ShowWindow(hChildWnd, SW_SHOW);
    UpdateWindow(hChildWnd);
    // Set a timer to hide the window after 2 seconds
    SetTimer(hTrayWnd, IDT_HIDE_WINDOW, 2000, NULL);
}

void HideChildWindow() {
    ShowWindow(hChildWnd, SW_HIDE);
}

void UpdateTrayIcon() {
    nid.uFlags = NIF_ICON; // Set the flag to update only the icon
    nid.hIcon = CreateIconWithNumber(currentLayer, IsDarkTheme());
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void ProcessLayerSwitches() {
    if (!layerSwitches.empty()) {
        std::lock_guard<std::mutex> lock(mtx); // Lock the mutex to ensure thread-safe
        auto lastEntry = layerSwitches.back();
        currentLayer = lastEntry.first;
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

bool OpenHidDevice(HIDData& hidData, bool notifiy) {
  
    if (!hid_connect(hidData.hid, VID, PID)) {
        InvalidateRect(hChildWnd, NULL, TRUE);
        ShowNotification("FootSwitch Device Status:", "Device not ready");
    }
    else {
		// Set the hid_read() function to be non-blocking.
		hidData.readData.resize(hidData.hid.inEplength);
		hidData.writeData.resize(hidData.hid.outEplength);
        ShowNotification("FootSwitch Device Status:", "Device ready");
        return true;
    }
	return false;
}

LRESULT CALLBACK ChildWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
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
        std::string text = "FootSwitch\nLayer: " + std::to_string(currentLayer);

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

bool isMatchingDevice(const std::string& deviceName, const HIDData& hidData) {
    std::string lowDevName = deviceName;
    std::transform(lowDevName.begin(), lowDevName.end(), lowDevName.begin(), [](unsigned char c) { return std::tolower(c); });

    std::stringstream vidStream, pidStream;
    vidStream << "vid_" << std::hex << std::setw(4) << std::setfill('0') << hidData.hid.info.VendorID;
    pidStream << "pid_" << std::hex << std::setw(4) << std::setfill('0') << hidData.hid.info.ProductID;

    return strstr(lowDevName.c_str(), vidStream.str().c_str()) && strstr(lowDevName.c_str(), pidStream.str().c_str());
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVICEREMOVECOMPLETE) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                // Check if the device matches our VID and PID
                if (isMatchingDevice(pDevInf->dbcc_name, hidData)) {
					// Remove comes more than one, because of the multiple interfaces
                    if (hidData.hid.handle != INVALID_HANDLE_VALUE) {
                        hid_close(hidData.hid);
                        hidData.hid.handle = INVALID_HANDLE_VALUE;
                        ShowNotification("FootSwitch Device Status:", "Device unplugged");
                    }
                }
            }
        }
        else if (wParam == DBT_DEVICEARRIVAL) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
                // Check if the device matches our VID and PID
                // Arrival comes more than one, because of the multiple interfaces
                if (isMatchingDevice(pDevInf->dbcc_name, hidData) &&
                    hidData.hid.handle == INVALID_HANDLE_VALUE) {
                    // Handle device arrival
                    if (OpenHidDevice(hidData, false)) {
                        ;
                    }
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
                // Example data to write
                std::vector<BYTE> data(hidData.hid.outEplength, 0x00);
                const char* message = "Tray data...";
                std::copy(message, message + std::min<size_t>(strlen(message), data.size()), data.begin());

                if (hid_write(hidData.hid, data)) {
                    ShowNotification("HID Write", "Data written successfully");
                }
                else {
                    ShowNotification("HID Write", "Failed to write data");
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
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void readCallback(const std::vector<BYTE>& data, void* userData) {
    HIDData* phidData = static_cast<HIDData*>(userData);
    phidData->readData = data;

    // Try to read from the device.
    if (data.size() == phidData->hid.inEplength) {
        hid_write(phidData->hid, data);
        // Check if the data is in JSON object format.
        if (is_json_object(phidData)) {
            // Parse the JSON object.
            try {
                json j = json::parse(data.begin()+1, data.end());
                // Extract and update the "layer" value if it exists.
                if (j.contains("layer")) {
                    int newLayer = j["layer"].get<int>();

                    std::lock_guard<std::mutex> lock(mtx);
                    layerSwitches.push_back({ newLayer, steady_clock::now() });
                    ResetLayerSwitchTimer();
                }
            }
            catch (json::parse_error& e) {
                ShowNotification("Device Status", "Wrong data from the USB Device");
            }
        }
    }
    else if (data.size() == 0) {
        ; // No data received
    }
    else if (data.size() == -1) {
        InvalidateRect(hChildWnd, NULL, TRUE);
        std::string msgerr = hid_error(hidData.hid);
        ShowNotification("Foot Switch", msgerr.c_str());
        hid_close(hidData.hid);
        hidData.hid.handle = nullptr;
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
    OpenHidDevice(hidData, true);

    // Start the read thread
    hid_read_thread(hidData.hid, readCallback, &hidData);

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
    if (hidData.hid.handle != INVALID_HANDLE_VALUE) {
        hid_close(hidData.hid);
    }

    return 0;
}
