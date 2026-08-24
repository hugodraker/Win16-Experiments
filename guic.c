/* ============================================================================
 * Touch-Friendly GCC C Compiler GUI - Win16 OpenWatcom Implementation
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s guic.c commdlg.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s guic.c
 *     wlink system windows option quiet option packcode option stack=16k name guic.exe file guic.obj library windows.lib library commdlg.lib
 *
 * ============================================================================
 * PUBLIC DOMAIN DEDICATION:
 *
 * This software is released into the public domain. It is not fit for any 
 * purpose. Use entirely at your own risk.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ============================================================================
 */

#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX_PATH
#define MAX_PATH 128
#endif

#define ID_GCC_PATH 101
#define ID_FILE_COMBO 102
#define ID_BTN_BROWSE 103
#define ID_BTN_EDIT 104
#define ID_BTN_COMPILE 105
#define ID_BTN_COPY 106
#define ID_OUTPUT 107
#define ID_BTN_RUN 108
#define ID_ARGS 109

// Global Variables
HWND hGccPath, hFileCombo, hBtnBrowse, hBtnEdit, hBtnCompile, hBtnCopy, hOutput, hBtnRun, hArgs;
HFONT hLargeFont;
char iniPath[MAX_PATH];

// Get the path to the INI file
void InitIniPath(void) {
    GetModuleFileName(NULL, iniPath, MAX_PATH);
    {
        char *lastSlash = strrchr(iniPath, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        lstrcat(iniPath, "guic.ini");
    }
}

// Load settings from INI
void LoadSettings(void) {
    char buffer[MAX_PATH];
    GetPrivateProfileString("Settings", "GCCPath", "C:\\MinGW\\bin", buffer, MAX_PATH, iniPath);
    SetWindowText(hGccPath, buffer);

    GetPrivateProfileString("Settings", "LastFile", "", buffer, MAX_PATH, iniPath);
    if (lstrlen(buffer) > 0) {
        SendMessage(hFileCombo, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)buffer);
        SendMessage(hFileCombo, CB_SETCURSEL, 0, 0);
        
        {
            char argsBuf[512];
            GetPrivateProfileString("Args", buffer, "", argsBuf, sizeof(argsBuf), iniPath);
            SetWindowText(hArgs, argsBuf);
        }
    }
}

// Save settings to INI
void SaveSettings(void) {
    char pathBuf[MAX_PATH];
    char fileBuf[MAX_PATH];

    GetWindowText(hGccPath, pathBuf, MAX_PATH);
    WritePrivateProfileString("Settings", "GCCPath", pathBuf, iniPath);

    GetWindowText(hFileCombo, fileBuf, MAX_PATH);
    WritePrivateProfileString("Settings", "LastFile", fileBuf, iniPath);

    if (lstrlen(fileBuf) > 0) {
        char argsBuf[512];
        GetWindowText(hArgs, argsBuf, sizeof(argsBuf));
        WritePrivateProfileString("Args", fileBuf, argsBuf, iniPath);
    }
}

// Copy output text box contents to Windows Clipboard
void CopyOutputToClipboard(HWND hwndOwner) {
    int len = GetWindowTextLength(hOutput);
    HGLOBAL hMem;
    char FAR* pMem;

    if (len <= 0) return;

    hMem = GlobalAlloc(GMEM_MOVEABLE, (DWORD)(len + 1));
    if (!hMem) return;

    pMem = (char FAR*)GlobalLock(hMem);
    if (pMem) {
        GetWindowText(hOutput, (LPSTR)pMem, len + 1);
        GlobalUnlock(hMem);

        if (OpenClipboard(hwndOwner)) {
            EmptyClipboard();
            SetClipboardData(CF_TEXT, hMem);
            CloseClipboard();
        } else {
            GlobalFree(hMem);
        }
    }
}

// Scan the first 20 lines of the C file for a compile command containing "gcc"
int GetHeaderCommand(const char* filepath, char* outCmd, size_t outSize) {
    FILE *f = fopen(filepath, "r");
    char line[512];
    int found = 0;
    int i;

    if (!f) return 0;

    for (i = 0; i < 20 && fgets(line, sizeof(line), f); i++) {
        char *gccPtr = strstr(line, "gcc");
        if (gccPtr) {
            int valid = 0;
            if (gccPtr == line) {
                valid = 1;
            } else {
                char prev = *(gccPtr - 1);
                if (prev == ' ' || prev == '\t' || prev == '*' || prev == '/') {
                    valid = 1;
                }
            }

            if (valid) {
                strncpy(outCmd, gccPtr, outSize);
                outCmd[outSize - 1] = '\0'; 
                outCmd[strcspn(outCmd, "\r\n")] = 0; // trim newline
                found = 1;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

// Compile the selected file using a temporary batch file and redirection (Win16 compatible)
void CompileFile(void) {
    char filepath[MAX_PATH];
    char gccPath[MAX_PATH];
    char compileCmd[1024];
    FILE *bat;
    FILE *out;
    long fsize;
    char FAR* logBuf;
    int exitCode;
    char FAR* finalLog;

    GetWindowText(hFileCombo, filepath, MAX_PATH);
    if (lstrlen(filepath) == 0) {
        SetWindowText(hOutput, "Error: No file selected.");
        return;
    }

    GetWindowText(hGccPath, gccPath, MAX_PATH);

    if (!GetHeaderCommand(filepath, compileCmd, sizeof(compileCmd))) {
        char outExe[MAX_PATH];
        char *dot;
        lstrcpy(outExe, filepath);
        dot = strrchr(outExe, '.');
        if (dot) *dot = '\0';
        lstrcat(outExe, ".exe");
        sprintf(compileCmd, "gcc \"%s\" -o \"%s\"", filepath, outExe);
    }

    // Write batch file to execute compilation and redirect output
    bat = fopen("compile.bat", "w");
    if (!bat) {
        SetWindowText(hOutput, "Error: Failed to create compile.bat");
        return;
    }
    fprintf(bat, "@echo off\n");
    fprintf(bat, "PATH %s;%%PATH%%\n", gccPath);
    fprintf(bat, "%s > out.txt 2>&1\n", compileCmd);
    fclose(bat);

    SetWindowText(hOutput, "Compiling...\r\n");
    exitCode = system("command.com /c compile.bat");

    // Read out.txt
    out = fopen("out.txt", "rb");
    if (!out) {
        SetWindowText(hOutput, "Error: Failed to read compilation output log.");
        return;
    }

    fseek(out, 0, SEEK_END);
    fsize = ftell(out);
    fseek(out, 0, SEEK_SET);

    logBuf = (char FAR*)malloc((size_t)fsize + 1);
    if (logBuf) {
        size_t bytesRead = fread(logBuf, 1, (size_t)fsize, out);
        logBuf[bytesRead] = '\0';
    }
    fclose(out);

    finalLog = (char FAR*)malloc((size_t)fsize + 512);
    if (finalLog) {
        sprintf(finalLog, "Command: %s\r\n\r\n%s\r\n", compileCmd, logBuf ? logBuf : "");
        if (exitCode == 0) {
            lstrcat(finalLog, "[Success] Compiled with 0 errors.");
        } else {
            lstrcat(finalLog, "[Failed] Compilation encountered errors.");
        }
        SetWindowText(hOutput, finalLog);
        free(finalLog);
    }
    if (logBuf) free(logBuf);

    // Cleanup temporary files
    remove("compile.bat");
    remove("out.txt");
}

// Window Procedure
LRESULT CALLBACK __export WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hLargeFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                    DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            hGccPath   = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_GCC_PATH, NULL, NULL);
            hFileCombo = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_FILE_COMBO, NULL, NULL);
            hBtnBrowse = CreateWindow("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
            hArgs      = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_ARGS, NULL, NULL);
            hBtnEdit   = CreateWindow("BUTTON", "Edit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_EDIT, NULL, NULL);
            hBtnCompile= CreateWindow("BUTTON", "COMPILE", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_COMPILE, NULL, NULL);
            hBtnRun    = CreateWindow("BUTTON", "RUN EXE", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_RUN, NULL, NULL);
            hBtnCopy   = CreateWindow("BUTTON", "Copy Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_COPY, NULL, NULL);
            hOutput    = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, 0, 0, 0, hwnd, (HMENU)ID_OUTPUT, NULL, NULL);

            SendMessage(hGccPath, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hFileCombo, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hArgs, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnEdit, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnCompile, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnRun, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnCopy, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hOutput, WM_SETFONT, (WPARAM)hLargeFont, TRUE);

            InitIniPath();
            LoadSettings();
            return 0;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            int pad = 10;
            int rowH = 30; 

            // Row 1: GCC Path
            MoveWindow(hGccPath, pad, pad, width - (pad*2), rowH, TRUE);
            
            // Row 2: File Dropdown & Browse
            {
                int btnW = 90;
                MoveWindow(hFileCombo, pad, pad*2 + rowH, width - (pad*3) - btnW, rowH + 200, TRUE);
                MoveWindow(hBtnBrowse, width - pad - btnW, pad*2 + rowH, btnW, rowH, TRUE);
            }

            // Row 3: Command Line Args
            MoveWindow(hArgs, pad, pad*3 + rowH*2, width - (pad*2), rowH, TRUE);

            // Row 4: Action Buttons (4 equal-width columns)
            {
                int quarterW = (width - (pad*5)) / 4;
                int btnY = pad*4 + rowH*3;
                int btnH = (int)(rowH * 1.2);
                MoveWindow(hBtnEdit,    pad,                  btnY, quarterW, btnH, TRUE);
                MoveWindow(hBtnCompile, pad*2 + quarterW,     btnY, quarterW, btnH, TRUE);
                MoveWindow(hBtnRun,     pad*3 + quarterW*2,   btnY, quarterW, btnH, TRUE);
                MoveWindow(hBtnCopy,    pad*4 + quarterW*3,   btnY, width - (pad*5) - (quarterW*3), btnH, TRUE);

                // Row 5: Output Log
                {
                    int outY = btnY + btnH + pad;
                    MoveWindow(hOutput, pad, outY, width - (pad*2), height - outY - pad, TRUE);
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            int wmId = wParam; // Win16 parameter unpacking for control notifications / IDs
            int notifyCode = HIWORD(lParam);
            HWND hwndCtl = (HWND)LOWORD(lParam);

            if (notifyCode == CBN_SELCHANGE && hwndCtl == hFileCombo) {
                int idx = (int)SendMessage(hFileCombo, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) {
                    char filepath[MAX_PATH];
                    char argsBuf[512];
                    SendMessage(hFileCombo, CB_GETLBTEXT, idx, (LPARAM)(LPSTR)filepath);
                    
                    GetPrivateProfileString("Args", filepath, "", argsBuf, sizeof(argsBuf), iniPath);
                    SetWindowText(hArgs, argsBuf);
                }
            }
            else if (wmId == ID_BTN_BROWSE) {
                OPENFILENAME ofn;
                char szFile[MAX_PATH] = {0};
                memset(&ofn, 0, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "C Source Files\0*.c\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileName(&ofn) == TRUE) {
                    char argsBuf[512];
                    int count;
                    SendMessage(hFileCombo, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)szFile);
                    count = (int)SendMessage(hFileCombo, CB_GETCOUNT, 0, 0);
                    SendMessage(hFileCombo, CB_SETCURSEL, count - 1, 0);
                    
                    GetPrivateProfileString("Args", szFile, "", argsBuf, sizeof(argsBuf), iniPath);
                    SetWindowText(hArgs, argsBuf);
                    
                    SaveSettings();
                }
            }
            else if (wmId == ID_BTN_EDIT) {
                char filepath[MAX_PATH];
                GetWindowText(hFileCombo, filepath, MAX_PATH);
                if (lstrlen(filepath) > 0) {
                    char cmdLine[MAX_PATH + 16];
                    sprintf(cmdLine, "notepad.exe %s", filepath);
                    WinExec(cmdLine, SW_SHOWNORMAL);
                }
            }
            else if (wmId == ID_BTN_COMPILE) {
                SaveSettings();
                CompileFile();
            }
            else if (wmId == ID_BTN_RUN) {
                char filepath[MAX_PATH];
                GetWindowText(hFileCombo, filepath, MAX_PATH);
                if (lstrlen(filepath) > 0) {
                    char argsBuf[512];
                    char exePath[MAX_PATH];
                    char *dot;
                    char cmdLine[MAX_PATH + 544];
                    UINT res;

                    SaveSettings(); 
                    
                    GetWindowText(hArgs, argsBuf, sizeof(argsBuf));

                    lstrcpy(exePath, filepath);
                    dot = strrchr(exePath, '.');
                    if (dot) *dot = '\0';
                    lstrcat(exePath, ".exe");

                    sprintf(cmdLine, "%s %s", exePath, argsBuf);
                    res = WinExec(cmdLine, SW_SHOWNORMAL);
                    if (res <= 32) {
                        SetWindowText(hOutput, "Error: Failed to run executable. Ensure it compiled successfully.");
                    } else {
                        SetWindowText(hOutput, "Executable launched successfully.");
                    }
                }
            }
            else if (wmId == ID_BTN_COPY) {
                CopyOutputToClipboard(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            SaveSettings();
            DeleteObject(hLargeFont);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "GccGuiClass";
    WNDCLASS wc = {0};
    HWND hwnd;
    MSG msg = {0};

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    hwnd = CreateWindowEx(
        0, CLASS_NAME, "Touch GCC Compiler", 
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 550, 480, 
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
/* EOF */