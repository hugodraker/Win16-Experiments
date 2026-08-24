/* ============================================================================
 * Basic Win16 Wget GUI Client - OpenWatcom Implementation
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s wget.c winsock.lib commdlg.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s wget.c
 *     wlink system windows option quiet option packcode option stack=16k name wget.exe file wget.obj library windows.lib library winsock.lib library commdlg.lib
 *
 * USAGE:
 *   Run the GUI application, enter a plain HTTP URL and output filename, then click Download.
 *
 * PUBLIC DOMAIN DEDICATION:
 *   This is free and unencumbered software released into the public domain.
 *   Anyone is free to copy, modify, publish, use, compile, sell, or
 *   distribute this software, either in source code form or as a compiled
 *   binary, for any purpose, commercial or non-commercial, and by any
 *   means.
 * ============================================================================ */

#include <windows.h>
#include <winsock.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX_PATH
#define MAX_PATH 128
#endif

#ifndef MAKEWORD
#define MAKEWORD(low, high) ((WORD)(((BYTE)(low)) | (((WORD)((BYTE)(high))) << 8)))
#endif

// --- Control IDs ---
#define ID_EDIT_URL   101
#define ID_EDIT_OUT   102
#define ID_BTN_DOWN   103
#define ID_EDIT_LOG   104

// --- Globals ---
HWND hMain = NULL;
HWND hEditUrl = NULL;
HWND hEditOut = NULL;
HWND hBtnDown = NULL;
HWND hEditLog = NULL;

HFONT hFixedFont = NULL;
HFONT hGuiFont = NULL;

void AppendLog(const char* text) {
    int len;
    if (!hEditLog) return;
    
    len = GetWindowTextLength(hEditLog);
    if (len > 30000) { /* Protect Win16 Edit control 32KB ceiling */
        SetWindowText(hEditLog, "");
        len = 0;
    }
    SendMessage(hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEditLog, EM_REPLACESEL, 0, (LPARAM)(LPSTR)text);
    // Win16 equivalent for autoscrolling caret to the end
    SendMessage(hEditLog, EM_SETSEL, (WPARAM)32767, (LPARAM)32767);
}

void DoDownload(void) {
    char url[256];
    char outFile[256];
    char host[128];
    char path[256];
    int port = 80;
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in addr;
    struct hostent *he;
    char req[512];
    FILE *f;
    char FAR* buf;
    int bytes;
    long totalBytes = 0;
    char logMsg[160];
    char *p, *p2;
    int headersParsed = 0;
    MSG m;

    GetWindowText(hEditUrl, url, sizeof(url));
    GetWindowText(hEditOut, outFile, sizeof(outFile));

    if (lstrlen(url) == 0 || lstrlen(outFile) == 0) {
        AppendLog("Error: URL and Output Filename cannot be empty.\r\n");
        return;
    }

    // Parse URL (expects http://host:port/path or http://host/path)
    p = url;
    if (strnicmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strnicmp(p, "https://", 8) == 0) {
        AppendLog("Error: HTTPS is not supported in Win16 Winsock 1.1 (plain HTTP only).\r\n");
        return;
    }

    p2 = strchr(p, '/');
    if (p2) {
        int hostLen = (int)(p2 - p);
        if (hostLen >= (int)sizeof(host)) hostLen = sizeof(host) - 1;
        lstrcpyn(host, p, hostLen + 1);
        host[hostLen] = '\0';
        lstrcpy(path, p2);
    } else {
        lstrcpy(host, p);
        lstrcpy(path, "/");
    }

    // Check for explicit port in host (e.g. host:port)
    p2 = strchr(host, ':');
    if (p2) {
        *p2 = '\0';
        port = atoi(p2 + 1);
        if (port <= 0) port = 80;
    }

    sprintf(logMsg, "Resolving %s...\r\n", (LPSTR)host);
    AppendLog(logMsg);

    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        AppendLog("Error: WSAStartup failed.\r\n");
        return;
    }

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        AppendLog("Error: socket() failed.\r\n");
        WSACleanup();
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(host);
    
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        he = gethostbyname(host);
        if (!he) {
            AppendLog("Error: Cannot resolve hostname.\r\n");
            closesocket(s);
            WSACleanup();
            return;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    sprintf(logMsg, "Connecting to port %d...\r\n", port);
    AppendLog(logMsg);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        AppendLog("Error: Connection failed.\r\n");
        closesocket(s);
        WSACleanup();
        return;
    }

    AppendLog("Connected. Sending HTTP request...\r\n");
    sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Win16Wget/1.0\r\nConnection: close\r\n\r\n", (LPSTR)path, (LPSTR)host);
    send(s, req, lstrlen(req), 0);

    f = fopen(outFile, "wb");
    if (!f) {
        AppendLog("Error: Could not open output file for writing.\r\n");
        closesocket(s);
        WSACleanup();
        return;
    }

    buf = (char FAR*)malloc(4096);
    if (!buf) {
        fclose(f);
        closesocket(s);
        WSACleanup();
        AppendLog("Error: Memory allocation failed.\r\n");
        return;
    }

    // Download loop with header parsing
    while ((bytes = recv(s, (char FAR*)buf, 4096, 0)) > 0) {
        if (!headersParsed) {
            int i;
            for (i = 0; i < bytes - 3; i++) {
                if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                    int bodyStart = i + 4;
                    int bodyLen = bytes - bodyStart;
                    if (bodyLen > 0) {
                        fwrite(buf + bodyStart, 1, (size_t)bodyLen, f);
                        totalBytes += bodyLen;
                    }
                    headersParsed = 1;
                    break;
                } else if (buf[i] == '\n' && buf[i+1] == '\n') {
                    int bodyStart = i + 2;
                    int bodyLen = bytes - bodyStart;
                    if (bodyLen > 0) {
                        fwrite(buf + bodyStart, 1, (size_t)bodyLen, f);
                        totalBytes += bodyLen;
                    }
                    headersParsed = 1;
                    break;
                }
            }
            if (!headersParsed) {
                // If headers span across chunks or aren't found yet, skip or discard leading headers chunk
                // (Basic fallback for robust single-file delivery)
            }
        } else {
            fwrite(buf, 1, (size_t)bytes, f);
            totalBytes += bytes;
        }

        // Keep UI responsive during active download chunks
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }

    free(buf);
    fclose(f);
    closesocket(s);
    WSACleanup();

    sprintf(logMsg, "Download complete! Total bytes: %ld\r\n", totalBytes);
    AppendLog(logMsg);
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND hLblUrl, hLblOut;

            hGuiFont = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            hFixedFont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    FIXED_PITCH | FF_MODERN, "Courier");

            hLblUrl = CreateWindow("STATIC", "URL:", WS_CHILD | WS_VISIBLE, 10, 12, 35, 20, hwnd, NULL, NULL, NULL);
            hEditUrl = CreateWindow("EDIT", "http://", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 50, 10, 420, 22, hwnd, (HMENU)ID_EDIT_URL, NULL, NULL);
            
            hLblOut = CreateWindow("STATIC", "Save As:", WS_CHILD | WS_VISIBLE, 10, 42, 50, 20, hwnd, NULL, NULL, NULL);
            hEditOut = CreateWindow("EDIT", "download.file", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 65, 40, 250, 22, hwnd, (HMENU)ID_EDIT_OUT, NULL, NULL);

            // Use BS_DEFPUSHBUTTON so pressing Enter triggers Download
            hBtnDown = CreateWindow("BUTTON", "Download", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 330, 39, 140, 25, hwnd, (HMENU)ID_BTN_DOWN, NULL, NULL);

            hEditLog = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
                                    10, 75, 460, 250, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);

            if (hGuiFont) {
                SendMessage(hLblUrl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hEditUrl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hLblOut, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hEditOut, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hBtnDown, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
            }
            if (hFixedFont) {
                SendMessage(hEditLog, WM_SETFONT, (WPARAM)hFixedFont, TRUE);
            }

            AppendLog("Ready. Enter an HTTP URL and output filename.\r\n");
            break;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 30 && h > 90) {
                MoveWindow(hEditUrl, 50, 10, w - 60, 22, TRUE);
                MoveWindow(hEditOut, 65, 40, w - 180, 22, TRUE);
                MoveWindow(hBtnDown, w - 105, 39, 95, 25, TRUE);
                MoveWindow(hEditLog, 10, 75, w - 20, h - 85, TRUE);
            }
            break;
        }
        case WM_COMMAND: {
            int wmId = wParam;
            if (wmId == ID_BTN_DOWN) {
                DoDownload();
            }
            break;
        }
        case WM_DESTROY:
            if (hFixedFont) DeleteObject(hFixedFont);
            if (hGuiFont) DeleteObject(hGuiFont);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "Win16WgetGuiClass";
    WNDCLASS wc;
    MSG msg;

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow(CLASS_NAME, "Win16 HTTP Downloader (Wget)", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 500, 375,
                         NULL, NULL, hInstance, NULL);

    if (!hMain) return 0;

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    memset(&msg, 0, sizeof(MSG));
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
/* EOF */