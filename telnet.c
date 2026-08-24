/* ============================================================================
 * Telnet Single-Window GUI Client - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s telnet.c winsock.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s telnet.c
 *     wlink system windows option quiet option packcode option stack=16k name telnet.exe file telnet.obj library windows.lib library winsock.lib
 *
 * REQUIREMENTS: Windows 3.1x (Win16)
 * DEPENDENCIES: USER, GDI, WINSOCK
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX_PATH
#define MAX_PATH 128
#endif

#ifndef MAKEWORD
#define MAKEWORD(low, high) ((WORD)(((BYTE)(low)) | (((WORD)((BYTE)(high))) << 8)))
#endif

#define WM_SOCKET_NOTIFY (WM_USER + 1)

// --- Control IDs ---
#define ID_EDIT_HOST  101
#define ID_EDIT_PORT  102
#define ID_BTN_CONN   103
#define ID_EDIT_OUT   104
#define ID_EDIT_IN    105
#define ID_BTN_SEND   106

/* ---- Telnet protocol constants (RFC 854) ---- */
#define TN_IAC   255
#define TN_DONT  254
#define TN_DO    253
#define TN_WONT  252
#define TN_WILL  251
#define TN_SB    250
#define TN_SE    240

#define TN_OPT_ECHO 1
#define TN_OPT_SGA  3

// --- Globals ---
HWND hMain = NULL;
HWND hEditHost = NULL;
HWND hEditPort = NULL;
HWND hBtnConn = NULL;
HWND hEditOut = NULL;
HWND hEditIn = NULL;
HWND hBtnSend = NULL;

SOCKET sock = INVALID_SOCKET;
HFONT hFixedFont = NULL;
HFONT hGuiFont = NULL;

void AppendOutput(const char* text) {
    int len;
    if (!hEditOut) return;
    
    len = GetWindowTextLength(hEditOut);
    if (len > 30000) { /* Protect Win16 Edit control 32KB ceiling */
        SetWindowText(hEditOut, "");
        len = 0;
    }
    SendMessage(hEditOut, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEditOut, EM_REPLACESEL, 0, (LPARAM)(LPSTR)text);
    // Win16 equivalent for autoscrolling caret to the end
    SendMessage(hEditOut, EM_SETSEL, (WPARAM)32767, (LPARAM)32767);
}

void ProcessServerData(unsigned char *buf, int len) {
    char FAR* outBuf;
    int outLen = 0;
    int i = 0;

    outBuf = (char FAR*)malloc((size_t)len + 64);
    if (!outBuf) return;

    while (i < len) {
        if (buf[i] == TN_IAC) {
            if (i + 1 >= len) break;
            switch (buf[i + 1]) {
                case TN_DO:
                    if (i + 2 < len) {
                        unsigned char opt = buf[i + 2];
                        unsigned char reply = (opt == TN_OPT_SGA) ? TN_WILL : TN_WONT;
                        unsigned char r[3];
                        r[0] = TN_IAC; r[1] = reply; r[2] = opt;
                        if (sock != INVALID_SOCKET) send(sock, (char *)r, 3, 0);
                    }
                    i += 3;
                    break;
                case TN_WILL:
                    if (i + 2 < len) {
                        unsigned char opt = buf[i + 2];
                        unsigned char reply = (opt == TN_OPT_ECHO || opt == TN_OPT_SGA) ? TN_DO : TN_DONT;
                        unsigned char r[3];
                        r[0] = TN_IAC; r[1] = reply; r[2] = opt;
                        if (sock != INVALID_SOCKET) send(sock, (char *)r, 3, 0);
                    }
                    i += 3;
                    break;
                case TN_SB:
                    i += 2;
                    while (i < len) {
                        if (buf[i] == TN_IAC && i + 1 < len) {
                            if (buf[i + 1] == TN_SE) { i += 2; break; }
                            i += 2;
                        } else { i++; }
                    }
                    break;
                default:
                    i += 2;
                    break;
            }
        } else {
            if (buf[i] != '\r') {
                outBuf[outLen++] = (buf[i] == '\n') ? '\n' : (char)buf[i];
            }
            i++;
        }
    }
    outBuf[outLen] = '\0';
    if (outLen > 0) {
        AppendOutput(outBuf);
    }
    free(outBuf);
}

void DisconnectFromServer(void) {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
        WSACleanup();
    }
    SetWindowText(hBtnConn, "Connect");
    AppendOutput("\r\nDisconnected.\r\n");
}

void ConnectToServer(void) {
    char host[128];
    char portStr[16];
    int port;
    struct sockaddr_in addr;
    struct hostent *he;
    WSADATA wsa;

    GetWindowText(hEditHost, host, sizeof(host));
    GetWindowText(hEditPort, portStr, sizeof(portStr));
    port = atoi(portStr);
    if (port <= 0) port = 23;

    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        AppendOutput("Error: WSAStartup failed.\r\n");
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        AppendOutput("Error: socket() failed.\r\n");
        WSACleanup();
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)port);

    addr.sin_addr.s_addr = inet_addr(host);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        he = gethostbyname(host);
        if (he == NULL) {
            AppendOutput("Error: Cannot resolve hostname.\r\n");
            closesocket(sock);
            sock = INVALID_SOCKET;
            WSACleanup();
            return;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (WSAAsyncSelect(sock, hMain, WM_SOCKET_NOTIFY, FD_READ | FD_CONNECT | FD_CLOSE) == SOCKET_ERROR) {
        AppendOutput("Error: WSAAsyncSelect failed.\r\n");
        closesocket(sock);
        sock = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    {
        char msg[160];
        sprintf(msg, "Connecting to %s:%d...\r\n", host, port);
        AppendOutput(msg);
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            AppendOutput("Connection failed.\r\n");
        }
    }
}

void SendCommandText(void) {
    char FAR* textBuf;
    int len;

    if (sock == INVALID_SOCKET) {
        AppendOutput("Not connected.\r\n");
        return;
    }

    len = GetWindowTextLength(hEditIn);
    if (len <= 0) return;

    textBuf = (char FAR*)malloc((size_t)len + 16);
    if (!textBuf) return;

    GetWindowText(hEditIn, (LPSTR)textBuf, len + 1);
    lstrcat(textBuf, "\r\n");

    send(sock, textBuf, lstrlen(textBuf), 0);
    SetWindowText(hEditIn, "");
    free(textBuf);
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND hLblHost, hLblPort;

            hGuiFont = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            hFixedFont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    FIXED_PITCH | FF_MODERN, "Courier");

            hLblHost = CreateWindow("STATIC", "Host:", WS_CHILD | WS_VISIBLE, 10, 12, 35, 20, hwnd, NULL, NULL, NULL);
            hEditHost = CreateWindow("EDIT", "127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 50, 10, 150, 22, hwnd, (HMENU)ID_EDIT_HOST, NULL, NULL);
            
            hLblPort = CreateWindow("STATIC", "Port:", WS_CHILD | WS_VISIBLE, 210, 12, 35, 20, hwnd, NULL, NULL, NULL);
            hEditPort = CreateWindow("EDIT", "23", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 245, 10, 50, 22, hwnd, (HMENU)ID_EDIT_PORT, NULL, NULL);

            hBtnConn = CreateWindow("BUTTON", "Connect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 310, 9, 90, 25, hwnd, (HMENU)ID_BTN_CONN, NULL, NULL);

            hEditOut = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
                                    10, 42, 605, 275, hwnd, (HMENU)ID_EDIT_OUT, NULL, NULL);

            hEditIn = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
                                   10, 325, 510, 24, hwnd, (HMENU)ID_EDIT_IN, NULL, NULL);

            // Use BS_DEFPUSHBUTTON so pressing Enter automatically triggers Send
            hBtnSend = CreateWindow("BUTTON", "Send", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 525, 324, 90, 26, hwnd, (HMENU)ID_BTN_SEND, NULL, NULL);

            if (hGuiFont) {
                SendMessage(hLblHost, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hEditHost, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hLblPort, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hEditPort, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hBtnConn, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hEditIn, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hBtnSend, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
            }
            if (hFixedFont) {
                SendMessage(hEditOut, WM_SETFONT, (WPARAM)hFixedFont, TRUE);
            }

            // Connect on startup automatically
            ConnectToServer();
            break;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 30 && h > 70) {
                MoveWindow(hEditOut, 10, 42, w - 20, h - 85, TRUE);
                MoveWindow(hEditIn, 10, h - 35, w - 110, 24, TRUE);
                MoveWindow(hBtnSend, w - 95, h - 36, 85, 26, TRUE);
            }
            break;
        }
        case WM_SOCKET_NOTIFY: {
            SOCKET s = (SOCKET)wParam;
            if (s != sock) break;

            if (WSAGETSELECTERROR(lParam)) {
                AppendOutput("\r\nConnection error encountered.\r\n");
                break;
            }

            switch (WSAGETSELECTEVENT(lParam)) {
                case FD_CONNECT:
                    AppendOutput("Connected to server.\r\n");
                    SetWindowText(hBtnConn, "Disconnect");
                    break;
                case FD_READ: {
                    unsigned char buf[1024];
                    int bytes = recv(sock, (char *)buf, sizeof(buf), 0);
                    if (bytes > 0) {
                        ProcessServerData(buf, bytes);
                    }
                    break;
                }
                case FD_CLOSE:
                    AppendOutput("\r\nConnection closed by remote host.\r\n");
                    DisconnectFromServer();
                    break;
            }
            break;
        }
        case WM_COMMAND: {
            int wmId = wParam;
            if (wmId == ID_BTN_CONN) {
                if (sock == INVALID_SOCKET) {
                    ConnectToServer();
                } else {
                    DisconnectFromServer();
                }
            } else if (wmId == ID_BTN_SEND) {
                SendCommandText();
            }
            break;
        }
        case WM_DESTROY:
            if (sock != INVALID_SOCKET) {
                closesocket(sock);
                WSACleanup();
            }
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
    const char* CLASS_NAME = "Win16TelnetGuiClass";
    WNDCLASS wc;
    MSG msg;

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow(CLASS_NAME, "Win16 Telnet GUI Client", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 645, 410,
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