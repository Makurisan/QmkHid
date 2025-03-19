#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <functional>


void qmk_log(const std::string& format_str, auto&&... args);

// Function to create and use a waitable timer with a custom callback
template <typename Callback, typename... Args>
void WaitableTimerThread(Callback callback, Args... args) {
    // Create a waitable timer
    HANDLE hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (hTimer == NULL) {
        qmk_log("CreateWaitableTimer failed ({})\n", GetLastError());
        return;
    }

    // Set the timer to signal after 2 seconds
    LARGE_INTEGER liDueTime;
    liDueTime.QuadPart = -200000LL; // 2 seconds in 100-nanosecond intervals

    if (!SetWaitableTimer(hTimer, &liDueTime, 0, NULL, NULL, FALSE)) {
        qmk_log("SetWaitableTimer failed ({})\n", GetLastError());
        CloseHandle(hTimer);
        return;
    }

    // Wait for the timer to be signaled
    qmk_log("Waiting for timer...\n");
    DWORD dwResult = WaitForSingleObject(hTimer, INFINITE);
    if (dwResult == WAIT_OBJECT_0) {
        callback(args...);
    }
    else {
        qmk_log("WaitForSingleObject failed ({})\n", GetLastError());
    }

    // Clean up
    CloseHandle(hTimer);
}
