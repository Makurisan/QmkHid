#pragma once

#include "resource.h"

typedef struct _StreamDeckHIDIn {
	uint8_t reportID[4]; // Report ID to identify the report type
	uint8_t buttonStates[15]; // Button states (adjust size as needed)
} StreamDeckHIDIn;

enum ProductType {
	NoBoard = 0,
	StreamDeck,
	QMK
};

typedef struct _QMKHIDPREFERENCE {
	USHORT seqnr;
	uint8_t curLayer;
	uint16_t showTime;       // time to show the client window
	uint8_t showLayerSwitch; // show layer switch in the client window
	std::string windowPos; // serialized RECT, status window position
	std::string traydev; // USB device which shows its state in the tray
	std::string timestamp;
}QMKHIDPREFERENCE;


typedef struct _HIDData {
	uint32_t seqnr;
	uint8_t type;
	std::shared_ptr<HID> hid;
	std::vector<uint8_t> readData;
	std::vector<uint8_t> writeData;
	uint8_t curLayer;// current layer if qmk sends it
	uint16_t curKey;   // last key pressed
}HIDData;
