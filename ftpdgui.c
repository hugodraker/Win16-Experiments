/* ============================================================================
 * FTP Server GUI - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s ftpservergui.c winsock.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s ftpservergui.c
 *     wlink system windows option quiet option packcode option stack=16k name ftpservergui.exe file ftpservergui.obj library windows.lib library winsock.lib
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

#ifndef WSAGETSELECTERROR
#define WSAGETSELECTERROR(lParam) HIWORD(lParam)
#endif
#ifndef WSAGETSELECTEVENT
#define WSAGETSELECTEVENT(lParam) LOWORD(lParam)
#endif
#ifndef WSAGetSELECTERROR
#define WSAGetSELECTERROR(lParam) HIWORD(lParam)
#endif
#ifndef WSAGetSELECTEVENT
#define WSAGetSELECTEVENT(lParam) LOWORD(lParam)
#endif

#define WM_SOCKET_NOTIFY (WM_USER + 1)

// --- Control IDs ---
#define ID_BTN_START_STOP 2001
#define ID_EDIT_PORT      2002
#define ID_EDIT_LOG       2003
#define ID_STATIC_STATUS  2004

// --- Globals ---
HWND hMain = NULL;
HWND hStatusBtn = NULL;
HWND hPortEdit = NULL;
HWND hStatusLabel = NULL;
HWND hEditLog = NULL;

int g_port = 21;
BOOL g_running = FALSE;
SOCKET g_server_socket = INVALID_SOCKET;
SOCKET g_data_socket = INVALID_SOCKET;
SOCKET g_active_client = INVALID_SOCKET;
SOCKET g_active_data_client = INVALID_SOCKET;

HFONT hFixedFont = NULL;
HFONT hGuiFont = NULL;

void AppendLog(const char* text) {
    int len;
    if (!hEditLog) return;
    
    len = GetWindowTextLength(hEditLog);
    if (len > 30000) {
        SetWindowText(hEditLog, "");
        len = 0;
    }
    SendMessage(hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEditLog, EM_REPLACESEL, 0, (LPARAM)(LPSTR)text);
    SendMessage(hEditLog, EM_SETSEL, (WPARAM)32767, (LPARAM)32767);
}

void UpdateServerStatus(void) {
    char buf[128];
    if (g_running) {
        sprintf(buf, "Status: Running on port %d", g_port);
        SetWindowText(hStatusLabel, buf);
        SetWindowText(hStatusBtn, "Stop Server");
        EnableWindow(hPortEdit, FALSE);
    } else {
        SetWindowText(hStatusLabel, "Status: Stopped");
        SetWindowText(hStatusBtn, "Start Server");
        EnableWindow(hPortEdit, TRUE);
    }
}

void SetupPassiveSocket(void) {
    struct sockaddr_in data_addr;
    if (g_data_socket != INVALID_SOCKET) {
        closesocket(g_data_socket);
        g_data_socket = INVALID_SOCKET;
    }
    g_data_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_data_socket == INVALID_SOCKET) return;

    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = 0; // Dynamic port
    data_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr)) == SOCKET_ERROR) {
        closesocket(g_data_socket);
        g_data_socket = INVALID_SOCKET;
        return;
    }
    listen(g_data_socket, 1);
    WSAAsyncSelect(g_data_socket, hMain, WM_SOCKET_NOTIFY, FD_ACCEPT);
}

void SendPasvResponse(SOCKET control_socket) {
    struct sockaddr_in addr;
    int len = sizeof(addr);
    unsigned short port;
    unsigned char *ip;
    char response[128];

    if (getsockname(g_data_socket, (struct sockaddr*)&addr, &len) == SOCKET_ERROR) {
        send(control_socket, "425 Can't open passive connection\r\n", 35, 0);
        return;
    }
    port = ntohs(addr.sin_port);
    
    // Get local IP address for PASV response
    {
        char hostname[128];
        struct hostent *he;
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            he = gethostbyname(hostname);
            if (he && he->h_addr_list[0]) {
                memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
            }
        }
    }
    ip = (unsigned char *)&addr.sin_addr;

    sprintf(response, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n",
            ip[0], ip[1], ip[2], ip[3], port >> 8, port & 0xFF);
    send(control_socket, response, (int)strlen(response), 0);
    AppendLog(response);
}

void SendBinaryFile(SOCKET control_socket, char *filename) {
    FILE *fp;
    char logBuf[256];

    if (g_active_data_client == INVALID_SOCKET) {
        send(control_socket, "425 Use PORT or PASV first\r\n", 28, 0);
        return;
    }

    fp = fopen(filename, "rb");
    if (!fp) {
        send(control_socket, "550 File not found\r\n", 21, 0);
        sprintf(logBuf, "FTP: File not found: %s\r\n", filename);
        AppendLog(logBuf);
        return;
    }

    send(control_socket, "150 Opening BINARY mode data connection\r\n", 41, 0);
    sprintf(logBuf, "FTP: Streaming file %s\r\n", filename);
    AppendLog(logBuf);

    {
        char file_buffer[1024];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, 1024, fp)) > 0) {
            send(g_active_data_client, file_buffer, (int)bytes_read, 0);
        }
    }
    fclose(fp);
    closesocket(g_active_data_client);
    g_active_data_client = INVALID_SOCKET;
    send(control_socket, "226 Transfer complete\r\n", 23, 0);
    AppendLog("FTP: Transfer complete.\r\n");
}

void HandleClientCommand(SOCKET client_socket, const char* cmd) {
    char logBuf[300];
    sprintf(logBuf, "FTP Cmd: %s", cmd);
    AppendLog(logBuf);

    if (strncmp(cmd, "USER", 4) == 0) {
        send(client_socket, "331 Password required\r\n", 23, 0);
    } else if (strncmp(cmd, "PASS", 4) == 0) {
        send(client_socket, "230 Logged in\r\n", 15, 0);
    } else if (strncmp(cmd, "PASV", 4) == 0) {
        SetupPassiveSocket();
        SendPasvResponse(client_socket);
    } else if (strncmp(cmd, "RETR", 4) == 0) {
        char fn[128];
        sscanf(cmd + 5, "%s", fn);
        SendBinaryFile(client_socket, fn);
    } else if (strncmp(cmd, "QUIT", 4) == 0) {
        send(client_socket, "221 Goodbye\r\n", 14, 0);
        closesocket(client_socket);
        if (client_socket == g_active_client) g_active_client = INVALID_SOCKET;
    } else if (strncmp(cmd, "SYST", 4) == 0) {
        send(client_socket, "215 UNIX Type: L8\r\n", 19, 0);
    } else if (strncmp(cmd, "TYPE", 4) == 0) {
        send(client_socket, "200 Command okay\r\n", 18, 0);
    } else {
        send(client_socket, "502 Not implemented\r\n", 22, 0);
    }
}

void StartServer(void) {
    WSADATA wsaData;
    struct sockaddr_in server_addr;
    char logBuf[128];

    if (g_running) return;

    if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
        AppendLog("Error: WSAStartup failed.\r\n");
        return;
    }

    g_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_server_socket == INVALID_SOCKET) {
        AppendLog("Error: socket() failed.\r\n");
        WSACleanup();
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)g_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        AppendLog("Error: Bind failed (Port in use?).\r\n");
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    if (listen(g_server_socket, 5) == SOCKET_ERROR) {
        AppendLog("Error: Listen failed.\r\n");
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    if (WSAAsyncSelect(g_server_socket, hMain, WM_SOCKET_NOTIFY, FD_ACCEPT) == SOCKET_ERROR) {
        AppendLog("Error: WSAAsyncSelect failed.\r\n");
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    g_running = TRUE;
    UpdateServerStatus();
    sprintf(logBuf, "FTP Server started successfully on port %d.\r\n", g_port);
    AppendLog(logBuf);
}

void StopServer(void) {
    if (!g_running) return;

    if (g_active_client != INVALID_SOCKET) {
        closesocket(g_active_client);
        g_active_client = INVALID_SOCKET;
    }
    if (g_active_data_client != INVALID_SOCKET) {
        closesocket(g_active_data_client);
        g_active_data_client = INVALID_SOCKET;
    }
    if (g_data_socket != INVALID_SOCKET) {
        closesocket(g_data_socket);
        g_data_socket = INVALID_SOCKET;
    }
    if (g_server_socket != INVALID_SOCKET) {
        closesocket(g_server_socket);
        g_server_socket = INVALID_SOCKET;
    }

    WSACleanup();
    g_running = FALSE;
    UpdateServerStatus();
    AppendLog("FTP Server stopped.\r\n");
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND hLblPort;

            hGuiFont = CreateFont(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            hFixedFont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    FIXED_PITCH | FF_MODERN, "Courier");

            hStatusLabel = CreateWindow("STATIC", "Status: Stopped", WS_CHILD | WS_VISIBLE, 10, 15, 200, 20, hwnd, NULL, NULL, NULL);
            
            hLblPort = CreateWindow("STATIC", "Port:", WS_CHILD | WS_VISIBLE, 220, 15, 35, 20, hwnd, NULL, NULL, NULL);
            hPortEdit = CreateWindow("EDIT", "21", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 255, 13, 60, 22, hwnd, (HMENU)ID_EDIT_PORT, NULL, NULL);

            hStatusBtn = CreateWindow("BUTTON", "Start Server", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 330, 11, 110, 25, hwnd, (HMENU)ID_BTN_START_STOP, NULL, NULL);

            hEditLog = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
                                    10, 45, 605, 310, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);

            if (hGuiFont) {
                SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hLblPort, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hPortEdit, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
                SendMessage(hStatusBtn, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
            }
            if (hFixedFont) {
                SendMessage(hEditLog, WM_SETFONT, (WPARAM)hFixedFont, TRUE);
            }

            AppendLog("FTP Server GUI ready. Click 'Start Server' to begin.\r\n");
            break;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 20 && h > 60) {
                MoveWindow(hEditLog, 10, 45, w - 20, h - 55, TRUE);
            }
            break;
        }
        case WM_SOCKET_NOTIFY: {
            SOCKET s = (SOCKET)wParam;
            if (WSAGETSELECTERROR(lParam)) break;

            if (s == g_server_socket) {
                if (WSAGETSELECTEVENT(lParam) == FD_ACCEPT) {
                    struct sockaddr_in client_addr;
                    int addr_len = sizeof(client_addr);
                    SOCKET client_fd = accept(g_server_socket, (struct sockaddr*)&client_addr, &addr_len);
                    if (client_fd != INVALID_SOCKET) {
                        g_active_client = client_fd;
                        WSAAsyncSelect(client_fd, hwnd, WM_SOCKET_NOTIFY, FD_READ | FD_CLOSE);
                        send(client_fd, "220 Welcome to Win16 FTP Server\r\n", 33, 0);
                        AppendLog("FTP: New client connected.\r\n");
                    }
                }
            } else if (s == g_data_socket) {
                if (WSAGetSELECTEVENT(lParam) == FD_ACCEPT) {
                    struct sockaddr_in d_addr;
                    int d_len = sizeof(d_addr);
                    SOCKET d_client = accept(g_data_socket, (struct sockaddr*)&d_addr, &d_len);
                    if (d_client != INVALID_SOCKET) {
                        g_active_data_client = d_client;
                        AppendLog("FTP: Passive data connection established.\r\n");
                    }
                }
            } else {
                int event = WSAGetSELECTEVENT(lParam);
                if (event == FD_READ) {
                    char recv_buf[512];
                    int bytes = recv(s, recv_buf, sizeof(recv_buf) - 1, 0);
                    if (bytes > 0) {
                        recv_buf[bytes] = '\0';
                        HandleClientCommand(s, recv_buf);
                    }
                } else if (event == FD_CLOSE) {
                    closesocket(s);
                    if (s == g_active_client) g_active_client = INVALID_SOCKET;
                    if (s == g_active_data_client) g_active_data_client = INVALID_SOCKET;
                    AppendLog("FTP: Client disconnected.\r\n");
                }
            }
            break;
        }
        case WM_COMMAND: {
            int wmId = wParam;
            if (wmId == ID_BTN_START_STOP) {
                if (!g_running) {
                    char portStr[16];
                    GetWindowText(hPortEdit, portStr, sizeof(portStr));
                    g_port = atoi(portStr);
                    if (g_port <= 0) g_port = 21;
                    StartServer();
                } else {
                    StopServer();
                }
            }
            break;
        }
        case WM_DESTROY:
            StopServer();
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
    const char* CLASS_NAME = "Win16FtpServerClass";
    WNDCLASS wc;
    MSG msg;

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow(CLASS_NAME, "Win16 FTP Server GUI", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 640, 410,
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