/* Copyright 2025 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <setupapi.h>
#include <devguid.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <sstream>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum {
        DMR_RW_TO_HOST = 0,
        DMR_RW_TO_DMR,
        DMR_RW_UPLOAD,

        DMR_FRAME_HEAD = 0x68,
        DMR_FRAME_TAIL = 0x10,
};

typedef struct {
        uint8_t Head;
        uint8_t Command;
        uint8_t RW;
        uint8_t SR;
        uint8_t Sum[2];
        uint8_t Length[2];
        uint8_t Data[];
} DMR_Frame_t;

static HWND hMainWnd = NULL;
static HWND hComPortList = NULL;
static HWND hRefreshButton = NULL;
static HWND hStartStopButton = NULL;
static HWND hLogPane = NULL;

static volatile bool isCapturing;
static std::unique_ptr<std::thread> Thread;
static HANDLE hComPort = INVALID_HANDLE_VALUE;
static std::mutex logMutex;
static std::vector<uint8_t> dataBuffer;

static void AddLogMessage(const std::string &Message)
{
        std::lock_guard<std::mutex> lock(logMutex);
        char TimeStamp[64];
        struct tm TimeInfo;
        time_t Now;

        // Get the current text length
        int textLength = GetWindowTextLength(hLogPane);

        // Append the message with a timestamp
        Now = time(nullptr);
        localtime_s(&TimeInfo, &Now);
        strftime(TimeStamp, sizeof(TimeStamp), "[%Y-%m-%d %H:%M:%S] ", &TimeInfo);

        std::string formattedMessage = TimeStamp + Message + "\r\n";

        SendMessage(hLogPane, EM_SETSEL, (WPARAM)textLength, (LPARAM)textLength);
        SendMessage(hLogPane, EM_REPLACESEL, FALSE, (LPARAM)formattedMessage.c_str());
        SendMessage(hLogPane, EM_SCROLLCARET, 0, 0);
}

static uint32_t GetId(const uint8_t *pData)
{
        uint32_t Value = 0;

        Value += ((pData[0] >> 4) * 10) + (pData[0] & 15);
        Value *= 100;
        Value += ((pData[1] >> 4) * 10) + (pData[1] & 15);
        Value *= 100;
        Value += ((pData[2] >> 4) * 10) + (pData[2] & 15);
        Value *= 100;
        Value += ((pData[3] >> 4) * 10) + (pData[3] & 15);

        return Value;
}

static bool ProcessMessage(const uint8_t *pData, size_t &Length, char *pOut)
{
        const DMR_Frame_t *pFrame = (const DMR_Frame_t *)pData;

        pOut[0] = 0;

        if (pFrame->Head == DMR_FRAME_HEAD && Length >= sizeof(DMR_Frame_t) + 1) {
                const uint16_t DataLength = (pFrame->Length[0] << 8) | pFrame->Length[1];

                if (Length >= sizeof(DMR_Frame_t) + 1 + DataLength && pFrame->Data[DataLength] == DMR_FRAME_TAIL) {
                        const uint16_t Sum = (pFrame->Sum[0] << 8) | pFrame->Sum[1];

                        // TODO: Verify checksum

                        switch (pFrame->Command) {
                        case 0x02:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(pOut, 1024, "Set RX Volume to %d", pFrame->Data[0]);
                                        }
                                }
                                break;

                        case 0x09: // Alarm
                                break;

                        case 0x0B:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(pOut, 1024, "Set MIC Gain to %d", pFrame->Data[0]);
                                        }
                                }
                                break;

                        case 0x0C:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(pOut, 1024, "Set Power Saving Mode to %s",
                                                        (pFrame->Data[0] == 00) ? "Off" : (
                                                        (pFrame->Data[0] == 0x01) ? "Level 1" : (
                                                        (pFrame->Data[0] == 0x02) ? "Level 2" : "Level 3"))
                                                        );
                                        }
                                }
                                break;

                        case 0x1A:
                                strcat_s(pOut, 1024, "Initialization Status");
                                break;

                        case 0x25:
                                if (pFrame->RW == DMR_RW_TO_HOST) {
                                        if (DataLength == 4) {
                                                sprintf_s(pOut, 1024, "Firmware: %X.%X.%X.%X", pFrame->Data[0], pFrame->Data[1], pFrame->Data[2], pFrame->Data[3]);
                                        }
                                }
                                break;

                        case 0x2A:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 4) {
                                                //sprintf_s(pOut, 1024, "Set Local ID: %02X%02X%02X%02X", pFrame->Data[3], pFrame->Data[2], pFrame->Data[1], pFrame->Data[0]);
                                                sprintf_s(pOut, 1024, "Set Local ID: 12345678");
                                        }
                                }
                                break;

                        case 0x3E:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        strcat_s(pOut, 1024, "Wake Up");
                                }
                                break;

                        case 0x42:
                                strcat_s(pOut, 1024, "Deep Sleep Mode");
                                break;

                        case 0x45:
                                //strcat_s(pOut, 1024, "Set Alarm Configuration");
                                break;

                        case 0x48: // Remote monitoring duration
                                break;

                        case 0x4C: // Enable VHF/UHF switch
                                break;

                        case 0x4D:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(pOut, 1024, "Set Squelch Level to %d", pFrame->Data[0]);
                                        }
                                }
                                break;

                        case 0x59: // Digital service status
                                break;

                        case 0x62:
                                if (DataLength == 10) {
                                        pOut[0] = 0;
                                        sprintf_s(pOut, 1024, "%s Call from %02X%02X%02X%02X to %02X%02X%02X%02X in CC%d",
                                                (pFrame->Data[0] == 0x01) ? "Individual" : ((pFrame->Data[0] == 0x02) ? "Group" : "All"),
                                                pFrame->Data[5], pFrame->Data[6], pFrame->Data[7], pFrame->Data[8],
                                                pFrame->Data[1], pFrame->Data[2], pFrame->Data[3], pFrame->Data[4],
                                                pFrame->Data[9]
                                        );
                                }
                                break;

                        case 0x81:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength >= 7) {
                                                size_t i, KeyLength = 0;

                                                sprintf_s(pOut, 1024, "Set key: Seq %d, ", pFrame->Data[0]);

                                                switch (pFrame->Data[1]) {
                                                case 0x00:
                                                        strcat_s(pOut, 1024, "OFF");
                                                        break;

                                                case 0x01:
                                                        strcat_s(pOut, 1024, "ARC =");
                                                        KeyLength = 5;
                                                        break;

                                                case 0x04:
                                                        strcat_s(pOut, 1024, "AES128 =");
                                                        KeyLength = 16;
                                                        break;

                                                case 0x05:
                                                        strcat_s(pOut, 1024, "AES128 =");
                                                        KeyLength = 32;
                                                        break;
                                                }

                                                if (!KeyLength) {
                                                        pOut[0] = 0;
                                                        break;
                                                }

                                                for (i = 0; i < KeyLength && i < DataLength - 2; i++) {
                                                        char Hex[4];

                                                        sprintf_s(Hex, sizeof(Hex), " %02X", pFrame->Data[2 + i]);
                                                        strcat_s(pOut, 1024, Hex);
                                                }
                                        }
                                }
                                break;

                        case 0x82:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 20) {
                                                const uint32_t RX = (pFrame->Data[3] << 24) | (pFrame->Data[4] << 16) | (pFrame->Data[5] << 8) | pFrame->Data[6];
                                                const uint32_t TX = (pFrame->Data[7] << 24) | (pFrame->Data[8] << 16) | (pFrame->Data[9] << 8) | pFrame->Data[10];

                                                sprintf_s(pOut, 1024, "Set Channel: TS%d CC%d RX %d TX %d", pFrame->Data[0], pFrame->Data[1], RX, TX);
                                        }
                                }
                                break;

                        case 0x84:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength >= 5) {
                                                size_t i = pFrame->Data[0];

                                                strcat_s(pOut, 1024, "Set group list:");
                                                for (i = 0; i < pFrame->Data[0] && i < DataLength - 1; i++) {
                                                        char Group[16];

                                                        sprintf_s(Group, sizeof(Group), " %d", GetId(&pFrame->Data[(i * 4) + 1]));
                                                        strcat_s(pOut, 1024, Group);
                                                }
                                        } else {
                                                strcat_s(pOut, 1024, "Cleared group list");
                                        }
                                }
                                break;

#if 0
                        case 0x05: // Query signal strength
                                break;
#endif

                        case 0x06: // Make/Stop call;
                                break;

                        default:
                                printf("Unprocessed:");
                                for (size_t i = 0; i < 9 + DataLength; i++) {
                                        char Hex[4];
                                        sprintf_s(Hex, sizeof(Hex), " %02X", pData[i]);
                                        strcat_s(pOut, 1024, Hex);
                                }
                                break;
                        }

                        Length = sizeof(DMR_Frame_t) + 1 + DataLength;

                        return true;
                }
        }

        Length = 0;

        return false;
}

static std::pair<bool, std::string> ScanForFrames(std::vector<uint8_t> &buffer)
{
        size_t i;

        while (buffer.size()) {
                for (i = 0; i < buffer.size(); i++) {
                        if (buffer[i] == DMR_FRAME_HEAD) {
                                break;
                        }
                }

                if (i) {
                        buffer.erase(buffer.begin(), buffer.begin() + i);
                }

                if (!buffer.size() || buffer[0] == DMR_FRAME_HEAD) {
                        break;
                }
        }

        if (!buffer.size()) {
                return { false, "" };
        }

        char Msg[1024];
        size_t Length = buffer.size();
        bool Success;

        Success = ProcessMessage(buffer.data(), Length, Msg);
        if (Length) {
                buffer.erase(buffer.begin(), buffer.begin() + Length);
        }

        return { Success, Msg };
}

// Capture thread function
static void CaptureThread(void)
{
        const int BUFFER_SIZE = 1024;
        static uint8_t Buffer[BUFFER_SIZE];

        while (isCapturing) {
                DWORD bytesRead = 0;

                if (!ReadFile(hComPort, Buffer, BUFFER_SIZE, &bytesRead, NULL)) {
                        DWORD error = GetLastError();

                        if (error != ERROR_IO_PENDING) {
                                std::stringstream ss;
                                ss << "Error reading from COM port (" << error << ").";
                                AddLogMessage(ss.str());
                                break;
                        }
                }

                if (bytesRead > 0) {
                        bool haveMessage = true;
                        std::string message;

                        dataBuffer.insert(dataBuffer.end(), Buffer, Buffer + bytesRead);

                        while (haveMessage) {
                                auto result = ScanForFrames(dataBuffer);

                                haveMessage = result.first;
                                message = result.second;

                                if (haveMessage && message.length() > 0) {
                                        AddLogMessage(message);
                                }
                        }
                }
        }
}

static void ScanComPorts(void)
{
        SP_DEVINFO_DATA devData;
        DWORD i;

        // Clear the current list
        ComboBox_ResetContent(hComPortList);

        // Get a list of all COM ports
        HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) {
                AddLogMessage("Error: Failed to get device information.");
                return;
        }

        devData.cbSize = sizeof(SP_DEVINFO_DATA);

        // Enumerate all COM ports
        for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); i++) {
                char friendlyName[256] = { 0 };
                DWORD dataType = 0;
                DWORD size = sizeof(friendlyName);

                // Get the friendly name of the device
                if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devData, SPDRP_FRIENDLYNAME,
                        &dataType, (PBYTE)friendlyName, size, &size)) {
                        std::string portName = friendlyName;
                        size_t startPos = portName.find("(COM");
                        size_t endPos = portName.find(")", startPos);

                        if (startPos != std::string::npos && endPos != std::string::npos) {
                                std::string comPort = portName.substr(startPos + 1, endPos - startPos - 1);
                                ComboBox_AddString(hComPortList, comPort.c_str());
                        }
                }
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);

        if (ComboBox_GetCount(hComPortList) > 0) {
                ComboBox_SetCurSel(hComPortList, 0);
                AddLogMessage("COM ports refreshed.");
        } else {
                AddLogMessage("No COM ports found.");
        }
}

static void StartCapture(void)
{
        char portName[32];
        DCB dcb;
        COMMTIMEOUTS timeouts;

        if (isCapturing) {
                return;
        }

        ComboBox_GetText(hComPortList, portName, sizeof(portName));

        // Open the COM port
        std::string fullPortName = "\\\\.\\";
        fullPortName += portName;

        hComPort = CreateFile(
                fullPortName.c_str(),
                GENERIC_READ,
                0,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL);

        if (hComPort == INVALID_HANDLE_VALUE) {
                DWORD error = GetLastError();

                std::stringstream ss;
                ss << "Error: Failed to open COM port (" << error << ").";
                AddLogMessage(ss.str());
                return;
        }

        memset(&dcb, 0, sizeof(dcb));
        dcb.DCBlength = sizeof(dcb);

        if (!GetCommState(hComPort, &dcb)) {
                AddLogMessage("Error: Failed to get COM port state.");
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
                return;
        }

        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;

        if (!SetCommState(hComPort, &dcb)) {
                AddLogMessage("Error: Failed to set COM port state.");
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
                return;
        }

        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 10;

        if (!SetCommTimeouts(hComPort, &timeouts)) {
                AddLogMessage("Error: Failed to set COM port timeouts.");
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
                return;
        }

        dataBuffer.clear();

        isCapturing = true;
        Thread = std::make_unique<std::thread>(CaptureThread);

        std::stringstream ss;
        ss << "Started capturing data from " << portName << ".";
        AddLogMessage(ss.str());
}

static void StopCapture(void)
{
        if (!isCapturing) {
                return;
        }

        // Signal the thread to stop
        isCapturing = false;

        // Close the COM port
        if (hComPort != INVALID_HANDLE_VALUE) {
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
        }

        // Wait for the thread to finish
        if (Thread && Thread->joinable()) {
                Thread->join();
                Thread.reset();
        }

        AddLogMessage("Stopped capturing data.");
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
        HINSTANCE hInstance;
        HFONT hFont;
        int wmId;
        int clientWidth;
        int clientHeight;

        switch (message) {
        case WM_CREATE:
                hInstance = ((LPCREATESTRUCT)lParam)->hInstance;

                hComPortList = CreateWindow(
                        WC_COMBOBOX, TEXT(""),
                        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                        10, 10, 200, 200,
                        hWnd, (HMENU)1, hInstance, NULL);

                hRefreshButton = CreateWindow(
                        WC_BUTTON, TEXT("Refresh"),
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        220, 10, 100, 25,
                        hWnd, (HMENU)2, hInstance, NULL);

                hStartStopButton = CreateWindow(
                        WC_BUTTON, TEXT("Start"),
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        330, 10, 100, 25,
                        hWnd, (HMENU)3, hInstance, NULL);

                hLogPane = CreateWindow(
                        WC_EDIT, TEXT(""),
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                        10, 45, 760, 500,
                        hWnd, (HMENU)4, hInstance, NULL);

                hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                SendMessage(hComPortList, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
                SendMessage(hRefreshButton, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
                SendMessage(hStartStopButton, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
                SendMessage(hLogPane, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));

                ScanComPorts();

                AddLogMessage("Application started. Select a COM port and click Start to begin capturing data.");
                break;

        case WM_COMMAND:
                wmId = LOWORD(wParam);

                switch (wmId) {
                case 2: // Refresh button
                        ScanComPorts();
                        break;

                case 3: // Start/Stop button
                        if (isCapturing) {
                                StopCapture();
                                SetWindowText(hStartStopButton, TEXT("Start"));
                        } else {
                                // Get the selected COM port
                                int selectedIndex = ComboBox_GetCurSel(hComPortList);
                                if (selectedIndex != CB_ERR) {
                                        char portName[32];
                                        ComboBox_GetText(hComPortList, portName, sizeof(portName));

                                        StartCapture();
                                        SetWindowText(hStartStopButton, TEXT("Stop"));
                                } else {
                                        AddLogMessage("Error: No COM port selected.");
                                }
                        }
                        break;
                }
                break;

        case WM_SIZE:
                clientWidth = LOWORD(lParam);
                clientHeight = HIWORD(lParam);

                // Resize the log pane
                MoveWindow(hLogPane, 10, 45, clientWidth - 20, clientHeight - 55, TRUE);
                break;

        case WM_DESTROY:
                if (isCapturing) {
                        StopCapture();
                }
                PostQuitMessage(0);
                break;

        default:
                return DefWindowProc(hWnd, message, wParam, lParam);
        }

        return 0;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
        INITCOMMONCONTROLSEX iccex;
        WNDCLASSEX wcex;
        MSG msg;

        iccex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        iccex.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&iccex);

        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = hInstance;
        wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = NULL;
        wcex.lpszClassName = TEXT("DigiMonitoR");
        wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

        if (!RegisterClassEx(&wcex)) {
                MessageBox(NULL, TEXT("Window Registration Failed"), TEXT("Error"), MB_ICONERROR);
                return 1;
        }

        // Create the main window
        hMainWnd = CreateWindow(TEXT("DigiMonitoR"), TEXT("DigiMonitoR"),
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT,
                800, 600,
                NULL, NULL, hInstance, NULL);

        if (!hMainWnd) {
                MessageBox(NULL, TEXT("Window Creation Failed"), TEXT("Error"), MB_ICONERROR);
                return 1;
        }

        // Show and update the window
        ShowWindow(hMainWnd, nCmdShow);
        UpdateWindow(hMainWnd);

        // Main message loop
        while (GetMessage(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
        }

        return (int)msg.wParam;
}

