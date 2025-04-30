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
#include <setupapi.h>
#include <conio.h>
#include <devguid.h>
#include <string>
#include <vector>
#include <thread>

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

static HANDLE hComPort = INVALID_HANDLE_VALUE;
static std::vector<uint8_t> dataBuffer;

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

static bool ProcessMessage(const uint8_t *pData, size_t &Length)
{
        const DMR_Frame_t *pFrame = (const DMR_Frame_t *)pData;
        char Log[256];
        char TimeStamp[64];
        struct tm TimeInfo;
        time_t Now;

        Log[0] = 0;

        if (pFrame->Head == DMR_FRAME_HEAD && Length >= sizeof(DMR_Frame_t) + 1) {
                const uint16_t DataLength = (pFrame->Length[0] << 8) | pFrame->Length[1];

                if (Length >= sizeof(DMR_Frame_t) + 1 + DataLength && pFrame->Data[DataLength] == DMR_FRAME_TAIL) {
                        const uint16_t Sum = (pFrame->Sum[0] << 8) | pFrame->Sum[1];

                        // TODO: Verify checksum

                        switch (pFrame->Command) {
                        case 0x02:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(Log, sizeof(Log), "Set RX Volume to %d", pFrame->Data[0]);
                                        }
                                }
                                break;

                        case 0x05: // Ignore signal checks for now
                                break;

                        case 0x06:
                                if (pFrame->RW == DMR_RW_UPLOAD) {
                                        if (pFrame->Length[1] == 0x09) {
                                                sprintf_s(Log, sizeof(Log), "%s call started from %02X%02X%02X%02X to %02X%02X%02X%02X",
                                                        (pFrame->Data[0] == 0x01) ? "Private" : ((pFrame->Data[0] == 0x02) ? "Group" : "All"),
                                                        pFrame->Data[5], pFrame->Data[6], pFrame->Data[7], pFrame->Data[8],
                                                        pFrame->Data[1], pFrame->Data[2], pFrame->Data[3], pFrame->Data[4]);
                                        } else {
                                                sprintf_s(Log, sizeof(Log), "Call ended");
                                        }
                                }
                                break;

                        case 0x09: // Alarm
                                break;

                        case 0x0B:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(Log, sizeof(Log), "Set MIC Gain to %d", pFrame->Data[0]);
                                        }
                                }
                                break;

                        case 0x0C:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(Log, sizeof(Log), "Set Power Saving Mode to %s",
                                                        (pFrame->Data[0] == 00) ? "Off" : (
                                                        (pFrame->Data[0] == 0x01) ? "Level 1" : (
                                                        (pFrame->Data[0] == 0x02) ? "Level 2" : "Level 3"))
                                                        );
                                        }
                                }
                                break;

                        case 0x1A:
                                strcat_s(Log, sizeof(Log), "Initialization Status");
                                break;

                        case 0x25:
                                if (pFrame->RW == DMR_RW_TO_HOST) {
                                        if (DataLength == 4) {
                                                sprintf_s(Log, sizeof(Log), "Firmware: %X.%X.%X.%X", pFrame->Data[0], pFrame->Data[1], pFrame->Data[2], pFrame->Data[3]);
                                        }
                                }
                                break;

                        case 0x2A:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 4) {
                                                sprintf_s(Log, sizeof(Log), "Set Local ID: %02X%02X%02X%02X", pFrame->Data[3], pFrame->Data[2], pFrame->Data[1], pFrame->Data[0]);
                                        }
                                }
                                break;

                        case 0x3E:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        strcat_s(Log, sizeof(Log), "Wake Up");
                                }
                                break;

                        case 0x42:
                                strcat_s(Log, sizeof(Log), "Deep Sleep Mode");
                                break;

                        case 0x45:
                                strcat_s(Log, sizeof(Log), "Set Alarm Configuration");
                                break;

                        case 0x48: // Remote monitoring duration
                                break;

                        case 0x4C: // Enable VHF/UHF switch
                                break;

                        case 0x4D:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 1) {
                                                sprintf_s(Log, sizeof(Log), "Set Squelch Level to %d", pFrame->Data[0]);
                                        }
                                }
                                break;

                        case 0x59: // Digital service status
                                if (pFrame->RW == DMR_RW_UPLOAD) {
                                        sprintf_s(Log, sizeof(Log), "Channel is %s", pFrame->Data[0] ? "Busy" : "Idle");
                                }
                                break;

                        case 0x62:
                                if (DataLength == 10) {
                                        sprintf_s(Log, sizeof(Log), "Detected %s call from %02X%02X%02X%02X to %02X%02X%02X%02X in CC%d",
                                                (pFrame->Data[0] == 0x01) ? "Private" : ((pFrame->Data[0] == 0x02) ? "Group" : "All"),
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

                                                sprintf_s(Log, sizeof(Log), "Set key: Seq %d, ", pFrame->Data[0]);

                                                switch (pFrame->Data[1]) {
                                                case 0x00:
                                                        strcat_s(Log, sizeof(Log), "OFF");
                                                        break;

                                                case 0x01:
                                                        strcat_s(Log, sizeof(Log), "ARC =");
                                                        KeyLength = 5;
                                                        break;

                                                case 0x04:
                                                        strcat_s(Log, sizeof(Log), "AES128 =");
                                                        KeyLength = 16;
                                                        break;

                                                case 0x05:
                                                        strcat_s(Log, sizeof(Log), "AES256 =");
                                                        KeyLength = 32;
                                                        break;
                                                }

                                                if (!KeyLength) {
                                                        Log[0] = 0;
                                                        break;
                                                }

                                                for (i = 0; i < KeyLength && i < DataLength - 2; i++) {
                                                        char Hex[4];

                                                        sprintf_s(Hex, sizeof(Hex), " %02X", pFrame->Data[2 + i]);
                                                        strcat_s(Log, sizeof(Log), Hex);
                                                }
                                        }
                                }
                                break;

                        case 0x82:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength == 20) {
                                                const uint32_t RX = (pFrame->Data[3] << 24) | (pFrame->Data[4] << 16) | (pFrame->Data[5] << 8) | pFrame->Data[6];
                                                const uint32_t TX = (pFrame->Data[7] << 24) | (pFrame->Data[8] << 16) | (pFrame->Data[9] << 8) | pFrame->Data[10];

                                                sprintf_s(Log, sizeof(Log), "Set Channel: TS%d CC%d RX %d TX %d", pFrame->Data[0], pFrame->Data[1], RX, TX);
                                        }
                                }
                                break;

                        case 0x84:
                                if (pFrame->RW == DMR_RW_TO_DMR) {
                                        if (DataLength >= 5) {
                                                size_t i = pFrame->Data[0];

                                                strcat_s(Log, sizeof(Log), "Set group list:");
                                                for (i = 0; i < pFrame->Data[0] && i < DataLength - 1; i++) {
                                                        char Group[16];

                                                        sprintf_s(Group, sizeof(Group), " %d", GetId(&pFrame->Data[(i * 4) + 1]));
                                                        strcat_s(Log, sizeof(Log), Group);
                                                }
                                        } else {
                                                strcat_s(Log, sizeof(Log), "Cleared group list");
                                        }
                                }
                                break;

                        default:
                                for (size_t i = 0; i < 9 + DataLength; i++) {
                                        char Hex[4];
                                        sprintf_s(Hex, sizeof(Hex), " %02X", pData[i]);
                                        strcat_s(Log, sizeof(Log), Hex);
                                }
                                break;
                        }

                        if (Log[0]) {
                                Now = time(nullptr);
                                localtime_s(&TimeInfo, &Now);
                                strftime(TimeStamp, sizeof(TimeStamp), "[%Y-%m-%d %H:%M:%S] ", &TimeInfo);

                                printf("%s%s\n", TimeStamp, Log);
                        }

                        Length = sizeof(DMR_Frame_t) + 1 + DataLength;

                        return true;
                }
        }

        Length = 0;

        return false;
}

static bool ScanForFrames(std::vector<uint8_t> &buffer)
{
        char Msg[512];
        size_t Length;
        bool Success;
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
                return false;
        }

        Length = buffer.size();

        Success = ProcessMessage(buffer.data(), Length);
        if (Length) {
                buffer.erase(buffer.begin(), buffer.begin() + Length);
        }

        return Success;
}

static void Capture(void)
{
        const int BUFFER_SIZE = 1024;
        static uint8_t Buffer[BUFFER_SIZE];

        while (!_kbhit()) {
                DWORD bytesRead = 0;

                if (!ReadFile(hComPort, Buffer, BUFFER_SIZE, &bytesRead, NULL)) {
                        DWORD error = GetLastError();

                        if (error != ERROR_IO_PENDING) {
                                printf("Error reading from COM port (%d)\n", error);
                                break;
                        }
                }

                if (bytesRead > 0) {
                        dataBuffer.insert(dataBuffer.end(), Buffer, Buffer + bytesRead);

                        while (ScanForFrames(dataBuffer)) {
                        }
                }

                Sleep(1);
        }
}

static void ScanComPorts(void)
{
        SP_DEVINFO_DATA devData;
        size_t Total = 0;
        DWORD i;

        // Get a list of all COM ports
        HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) {
                printf("Error: Failed to get device information.\n");
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
                                printf("-> %s\n", comPort.c_str());
                                Total++;
                        }
                }
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
        if (!Total) {
                printf("No COM ports found.\n");
        }
}

static void StartCapture(const char *portName)
{
        DCB dcb;
        COMMTIMEOUTS timeouts;

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

                printf("Error: Failed to open COM port (%d).\n", error);
                return;
        }

        memset(&dcb, 0, sizeof(dcb));
        dcb.DCBlength = sizeof(dcb);

        if (!GetCommState(hComPort, &dcb)) {
                printf("Error: Failed to get COM port state.\n");
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
                return;
        }

        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;

        if (!SetCommState(hComPort, &dcb)) {
                printf("Error: Failed to set COM port state.\n");
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
                return;
        }

        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 100;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 100;
        timeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(hComPort, &timeouts)) {
                printf("Error: Failed to set COM port timeouts.\n");
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
                return;
        }

        dataBuffer.clear();

        printf("Started capturing data from %s\n", portName);
}

static void StopCapture(void)
{
        // Close the COM port
        if (hComPort != INVALID_HANDLE_VALUE) {
                CloseHandle(hComPort);
                hComPort = INVALID_HANDLE_VALUE;
        }

        printf("Stopped capturing data.\n");
}

int main(int argc, char *argv[])
{
        if (argc == 2 && strcmp(argv[1], "-l") == 0) {
                ScanComPorts();
                return 0;
        }
        if (argc != 3 || strcmp(argv[1], "-p")) {
                printf("Usage:\n");
                printf("    %s -l         List available COM ports.\n", argv[0]);
                printf("    %s -p COMx    Start capture on port COMx.\n", argv[0]);
                return 1;
        }

        StartCapture(argv[2]);

        Capture();

        StopCapture();

        return 0;
}
