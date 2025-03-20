#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <functional>

// Function to create a custom callback
template <typename Callback, typename... Args>
void WaitableTimerThread(Callback callback, Args... args) {

	callback(args...);

}
