/* ============================================================================
 * Ad-Hoc Delta Sync Service GUI - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s csvsync.c winsock.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s csvsync.c
 *     wlink system windows option quiet option packcode option stack=16k name csvsync.exe file csvsync.obj library windows.lib library winsock.lib
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
#include <time.h>
#include <stdbool.h>
#include <stdarg.h>

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
#define ID_EDIT_LOG      101
#define ID_TIMER_SYNC    102

#define DELIM "¦"
#define MAX_SERVERS 7
#define TCP_TIMEOUT_MS 100 
#define CSV_HEADER "ID" DELIM "Title" DELIM "StartMin" DELIM "Duration" DELIM "Color" DELIM "Date" DELIM "PersonIdx" DELIM "Version" DELIM "LastModifiedBy\n"

// Global Configuration Variables
char g_iniPath[MAX_PATH];
char g_csvPath[MAX_PATH];
char g_logPath[MAX_PATH];
char g_myIP[64] = "127.0.0.1 (Fallback)";

int g_port = 9876;
long g_syncIntervalMs = 180000L; // Stored safely as long to prevent 16-bit truncation
bool g_logging = false;
int g_deleteThreshold = 100;
char g_servers[MAX_SERVERS][64];
int g_myNodeID = 1;

int g_nodeSyncCycles[MAX_SERVERS] = {0};

HWND hMain = NULL;
HWND hEditOut = NULL;
HFONT hFixedFont = NULL;
SOCKET g_listenSock = INVALID_SOCKET;

typedef struct {
    char id[64];
    int version;
    char FAR* lineContent;
} CsvRow;

typedef struct {
    char id[64];
    int version;
} ClientRowState;

char* _strdup_w16(const char* s) {
    size_t len = strlen(s) + 1;
    char FAR* p = (char FAR*)malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}
#define _strdup _strdup_w16

char* strtok_r(char *str, const char *delim, char **nextp) {
    char *ret;
    if (str == NULL) str = *nextp;
    if (str == NULL) return NULL;
    str += strspn(str, delim);
    if (*str == '\0') {
        *nextp = NULL;
        return NULL;
    }
    ret = str;
    str = strpbrk(ret, delim);
    if (str == NULL) {
        *nextp = NULL;
    } else {
        *str = '\0';
        *nextp = str + 1;
    }
    return ret;
}
#ifndef strtok_s
#define strtok_s strtok_r
#endif

void AppendLog(const char* text) {
    int len;
    if (!hEditOut) return;
    
    len = GetWindowTextLength(hEditOut);
    if (len > 30000) {
        SetWindowText(hEditOut, "");
        len = 0;
    }
    SendMessage(hEditOut, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEditOut, EM_REPLACESEL, 0, (LPARAM)(LPSTR)text);
    SendMessage(hEditOut, EM_SETSEL, (WPARAM)32767, (LPARAM)32767);
}

void _Log(const char *format, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timeBuf[64];
    char msgBuf[1024];
    char fullMsg[1150];
    va_list args;

    sprintf(timeBuf, "[%04d-%02d-%02d %02d:%02d:%02d]", 
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, 
            t->tm_hour, t->tm_min, t->tm_sec);

    va_start(args, format);
    vsprintf(msgBuf, format, args);
    va_end(args);

    sprintf(fullMsg, "%s %s\r\n", timeBuf, msgBuf);
    AppendLog(fullMsg);

    if (g_logging) {
        FILE *f = fopen(g_logPath, "a");
        if (f) {
            fprintf(f, "%s %s\n", timeBuf, msgBuf);
            fclose(f);
        }
    }
}

bool _IsIP(const char *ip) {
    int a, b, c, d;
    return sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4;
}

bool _IsLocalIP(const char *ip) {
    char hostname[128];
    struct hostent *host;
    int i;
    if (strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "localhost") == 0) return true;
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) return false;
    host = gethostbyname(hostname);
    if (!host) return false;

    for (i = 0; host->h_addr_list[i] != NULL; i++) {
        struct in_addr addr;
        memcpy(&addr, host->h_addr_list[i], sizeof(struct in_addr));
        if (strcmp(inet_ntoa(addr), ip) == 0) return true;
    }
    return false;
}

void InitConfig(void) {
    char exePath[128];
    char drive[3], dir[128], fname[128], ext[16];
    FILE *f, *fCsv;
    char temp[256];
    int i;

    GetModuleFileName(NULL, exePath, 128);
    _splitpath(exePath, drive, dir, fname, ext);

    sprintf(g_iniPath, "%s%s%s.ini", drive, dir, fname);
    sprintf(g_csvPath, "%s%s%s.csv", drive, dir, fname);
    sprintf(g_logPath, "%s%s%s.log", drive, dir, fname);

    f = fopen(g_iniPath, "r");
    if (!f) {
        f = fopen(g_iniPath, "w");
        if (f) {
            fprintf(f, "[Network]\nPort=9876\nSyncIntervalMs=180000\nLogging=1\nDeleteThreshold=100\n");
            fprintf(f, "[Servers]\n");
            for (i = 1; i <= MAX_SERVERS; i++) {
                fprintf(f, "Server%d=0\n", i);
            }
            fclose(f);
        }
    } else {
        fclose(f);
    }

    fCsv = fopen(g_csvPath, "r");
    if (!fCsv) {
        fCsv = fopen(g_csvPath, "w");
        if (fCsv) {
            fprintf(fCsv, CSV_HEADER);
            fclose(fCsv);
        }
    } else {
        fclose(fCsv);
    }

    GetPrivateProfileString("Network", "Port", "9876", temp, sizeof(temp), g_iniPath);
    g_port = atoi(temp);
    GetPrivateProfileString("Network", "SyncIntervalMs", "180000", temp, sizeof(temp), g_iniPath);
    g_syncIntervalMs = atol(temp);
    GetPrivateProfileString("Network", "Logging", "1", temp, sizeof(temp), g_iniPath);
    g_logging = (atoi(temp) == 1);
    GetPrivateProfileString("Network", "DeleteThreshold", "100", temp, sizeof(temp), g_iniPath);
    g_deleteThreshold = atoi(temp);

    for (i = 1; i <= MAX_SERVERS; i++) {
        char key[32];
        sprintf(key, "Server%d", i);
        GetPrivateProfileString("Servers", key, "0", g_servers[i - 1], sizeof(g_servers[i - 1]), g_iniPath);
    }

    g_myNodeID = 1;
    for (i = 0; i < MAX_SERVERS; i++) {
        if (strcmp(g_servers[i], "0") != 0 && _IsLocalIP(g_servers[i])) {
            g_myNodeID = i + 1;
            strcpy(g_myIP, g_servers[i]);
            break;
        }
    }
}

bool _ConnectTimeout(SOCKET sock, struct sockaddr_in *addr, int timeout_ms) {
    u_long mode = 1; 
    fd_set writefds;
    struct timeval tv;
    int ret;

    ioctlsocket(sock, FIONBIO, &mode);
    connect(sock, (struct sockaddr*)addr, sizeof(*addr));

    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ret = select(0, NULL, &writefds, NULL, &tv);

    mode = 0; 
    ioctlsocket(sock, FIONBIO, &mode);

    if (ret > 0) {
        int err = 0;
        int errlen = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
        return (err == 0);
    }
    return false;
}

int _RecvTimeout(SOCKET sock, char *buf, int len, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;
    int ret;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ret = select(0, &readfds, NULL, NULL, &tv);
    if (ret > 0) return recv(sock, buf, len, 0);
    return -1;
}

int _SendTimeout(SOCKET sock, const char *buf, int len, int timeout_ms) {
    fd_set writefds;
    struct timeval tv;
    int ret;

    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ret = select(0, NULL, &writefds, NULL, &tv);
    if (ret > 0) return send(sock, buf, len, 0);
    return -1;
}

bool _IsCsvEmpty(void) {
    FILE *f = fopen(g_csvPath, "r");
    char line[256];
    int dataRows = 0;
    if (!f) return true;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ID" DELIM, 3) == 0) continue;
        if (strlen(line) > 3) dataRows++;
    }
    fclose(f);
    return (dataRows == 0);
}

void _MergeCsvData(const char *csvFile, const char *newCsvContent) {
    CsvRow *localRows = NULL;
    int rowCount = 0;
    FILE *f;
    char *deltaCopy;
    char *nextDeltaLine;
    char *deltaLine;
    int updatedCount = 0;
    int insertedCount = 0;

    if (!newCsvContent || strlen(newCsvContent) == 0) return;

    f = fopen(csvFile, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *nextToken = NULL;
            char *copy;
            char *tok;
            int col;

            if (strncmp(line, "ID" DELIM, 3) == 0) continue;
            if (strlen(line) <= 3) continue;

            localRows = (CsvRow*)realloc(localRows, (rowCount + 1) * sizeof(CsvRow));
            localRows[rowCount].lineContent = _strdup(line);
            
            copy = _strdup(line);
            tok = strtok_s(copy, DELIM, &nextToken);
            if (tok) strcpy(localRows[rowCount].id, tok);
            
            col = 0;
            localRows[rowCount].version = 0;
            while (tok) {
                if (col == 7) localRows[rowCount].version = atoi(tok);
                tok = strtok_s(NULL, DELIM, &nextToken);
                col++;
            }
            free(copy);
            rowCount++;
        }
        fclose(f);
    }

    deltaCopy = _strdup(newCsvContent);
    nextDeltaLine = NULL;
    deltaLine = strtok_s(deltaCopy, "\r\n", &nextDeltaLine);

    while (deltaLine) {
        if (strncmp(deltaLine, "ID" DELIM, 3) == 0) {
            deltaLine = strtok_s(NULL, "\r\n", &nextDeltaLine);
            continue;
        }

        if (strlen(deltaLine) > 5) {
            char lineBuf[512];
            char *nextToken = NULL;
            char *copy;
            char *tok;
            char newId[64] = {0};
            int newVersion = 0;
            int col;
            bool found = false;
            int i;

            sprintf(lineBuf, "%s\n", deltaLine);

            copy = _strdup(deltaLine);
            tok = strtok_s(copy, DELIM, &nextToken);
            if (tok) strcpy(newId, tok);
            
            col = 0;
            while (tok) {
                if (col == 7) newVersion = atoi(tok);
                tok = strtok_s(NULL, DELIM, &nextToken);
                col++;
            }
            free(copy);

            for (i = 0; i < rowCount; i++) {
                if (strcmp(localRows[i].id, newId) == 0) {
                    found = true;
                    if (newVersion >= localRows[i].version) {
                        if (newVersion > localRows[i].version) {
                            updatedCount++;
                            _Log("[CLIENT] Incoming Sync Write (Updated) -> ID: %s | Ver: %d", newId, newVersion);
                        }
                        free(localRows[i].lineContent);
                        localRows[i].lineContent = _strdup(lineBuf);
                        localRows[i].version = newVersion;
                    }
                    break;
                }
            }

            if (!found) {
                localRows = (CsvRow*)realloc(localRows, (rowCount + 1) * sizeof(CsvRow));
                strcpy(localRows[rowCount].id, newId);
                localRows[rowCount].version = newVersion;
                localRows[rowCount].lineContent = _strdup(lineBuf);
                rowCount++;
                insertedCount++;
                _Log("[CLIENT] Incoming Sync Write (Inserted) -> ID: %s | Ver: %d", newId, newVersion);
            }
        }
        deltaLine = strtok_s(NULL, "\r\n", &nextDeltaLine);
    }
    free(deltaCopy);

    if (updatedCount > 0 || insertedCount > 0 || rowCount > 0) {
        char tempFile[MAX_PATH];
        FILE *fw;
        int i;

        sprintf(tempFile, "%s.tmp", csvFile);
        fw = fopen(tempFile, "w");
        if (fw) {
            fprintf(fw, CSV_HEADER);
            for (i = 0; i < rowCount; i++) {
                fprintf(fw, "%s", localRows[i].lineContent);
            }
            fclose(fw);
            
            remove(csvFile);
            rename(tempFile, csvFile);
        }
    }

    if (localRows) {
        int i;
        for (i = 0; i < rowCount; i++) free(localRows[i].lineContent);
        free(localRows);
    }
}

void _ProcessNodeSpecificCleanup(const char *csvFile, int targetNodeID) {
    FILE *f = fopen(csvFile, "r");
    char tempFile[MAX_PATH];
    FILE *fw;
    char line[512];
    int deletedCount = 0;

    if (!f) return;

    sprintf(tempFile, "%s.tmp", csvFile);
    fw = fopen(tempFile, "w");
    if (!fw) { fclose(f); return; }

    fprintf(fw, CSV_HEADER);

    while (fgets(line, sizeof(line), f)) {
        char lineCopy[512];
        char *nextToken = NULL;
        char *token;
        char fields[10][128];
        int fCount = 0;
        int color, modifiedBy;

        if (strncmp(line, "ID" DELIM, 3) == 0) continue;

        strcpy(lineCopy, line);
        lineCopy[strcspn(lineCopy, "\r\n")] = 0;
        
        if (strlen(lineCopy) == 0) continue; 
        
        token = strtok_s(lineCopy, DELIM, &nextToken);
        while (token != NULL && fCount < 10) {
            strcpy(fields[fCount++], token);
            token = strtok_s(NULL, DELIM, &nextToken);
        }

        color = (fCount >= 5) ? atoi(fields[4]) : 0;
        modifiedBy = (fCount >= 9) ? atoi(fields[8]) : 0;

        if (color == 2 && modifiedBy == targetNodeID) {
            deletedCount++;
            _Log("[CLEANUP] Deleted entry ID: %s originating from Node %d (Color is 2)", fields[0], targetNodeID);
            continue;
        }

        fprintf(fw, "%s", line);
    }
    
    fclose(f);
    fclose(fw);

    if (deletedCount > 0) {
        remove(csvFile);
        rename(tempFile, csvFile);
        _Log("Cleanup Complete for Node %d. Removed %d items.", targetNodeID, deletedCount);
    } else {
        remove(tempFile);
    }
}

void _HandleServerClient(SOCKET clientSocket) {
    char buffer[1024];
    char *data = NULL;
    int dataSize = 0;
    int bytesReceived;
    char *nextOuter;
    char *linePtr;
    int reqNodeID = -1;
    int reqIsEmpty = 0;
    ClientRowState *clientStates = NULL;
    int clientCount = 0;
    FILE *f;
    int sentRows = 0;
    const char *eofMarker = "[EOF]";

    while (true) {
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            data = (char*)realloc(data, dataSize + bytesReceived + 1);
            memcpy(data + dataSize, buffer, bytesReceived + 1);
            dataSize += bytesReceived;
            if (strstr(data, "[EOF]")) break;
        } else {
            break;
        }
    }

    if (!data) {
        closesocket(clientSocket);
        return;
    }

    nextOuter = NULL;
    linePtr = strtok_s(data, "\r\n", &nextOuter);
    if (linePtr && sscanf(linePtr, "CLIENT_SYNC_DELTA" DELIM "%d" DELIM "%d", &reqNodeID, &reqIsEmpty) >= 2) {
        linePtr = strtok_s(NULL, "\r\n", &nextOuter);
        while (linePtr) {
            if (strcmp(linePtr, "[EOF]") == 0) break;
            if (strlen(linePtr) > 0 && strncmp(linePtr, "ID" DELIM, 3) != 0) {
                char *nextToken = NULL;
                char *copy = _strdup(linePtr);
                char *tok = strtok_s(copy, DELIM, &nextToken);
                if (tok) {
                    clientStates = (ClientRowState*)realloc(clientStates, (clientCount + 1) * sizeof(ClientRowState));
                    strcpy(clientStates[clientCount].id, tok);
                    tok = strtok_s(NULL, DELIM, &nextToken);
                    clientStates[clientCount].version = tok ? atoi(tok) : 0;
                    clientCount++;
                }
                free(copy);
            }
            linePtr = strtok_s(NULL, "\r\n", &nextOuter);
        }

        f = fopen(g_csvPath, "r");
        if (f) {
            char csvLine[512];

            while (fgets(csvLine, sizeof(csvLine), f)) {
                char *nextToken = NULL;
                char *copy;
                char *tok;
                char rowID[64] = {0};
                int rowVersion = 0;
                int rowModifiedBy = 0;
                int col = 0;
                bool allowOwnData;
                int clientVer = -1;
                int k;

                if (strncmp(csvLine, "ID" DELIM, 3) == 0) continue;

                copy = _strdup(csvLine);
                tok = strtok_s(copy, DELIM, &nextToken);
                while (tok) {
                    if (col == 0) strcpy(rowID, tok);
                    if (col == 7) rowVersion = atoi(tok);
                    if (col == 8) rowModifiedBy = atoi(tok);
                    tok = strtok_s(NULL, DELIM, &nextToken);
                    col++;
                }
                free(copy);

                allowOwnData = (reqIsEmpty == 1);
                if (!allowOwnData && rowModifiedBy == reqNodeID) {
                    continue;
                }

                for (k = 0; k < clientCount; k++) {
                    if (strcmp(clientStates[k].id, rowID) == 0) {
                        clientVer = clientStates[k].version;
                        break;
                    }
                }

                if (clientVer == -1 || rowVersion > clientVer) {
                    send(clientSocket, csvLine, (int)strlen(csvLine), 0);
                    sentRows++;
                    _Log("[SERVER] Outgoing Sync Write -> ID: %s | Ver: %d", rowID, rowVersion);
                }
            }
            fclose(f);
        }
        
        if (clientStates) free(clientStates);
        
        if (sentRows > 0) {
            _Log("Server: Sent %d total row(s) to Node %d.", sentRows, reqNodeID);
        }
    } 

    send(clientSocket, eofMarker, (int)strlen(eofMarker), 0);
    closesocket(clientSocket);
    free(data);
}

void _RunAllClientSyncs(void) {
    bool isEmpty = _IsCsvEmpty();
    char *idVerBuf = NULL;
    int idVerLen = 0;
    FILE *f;
    int i;

    f = fopen(g_csvPath, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *nextToken = NULL;
            char *copy;
            char *tok;
            char id[64] = {0};
            int ver = 0;
            int col = 0;

            if (strncmp(line, "ID" DELIM, 3) == 0) continue;
            
            copy = _strdup(line);
            tok = strtok_s(copy, DELIM, &nextToken);
            while (tok) {
                if (col == 0) strcpy(id, tok);
                if (col == 7) ver = atoi(tok);
                tok = strtok_s(NULL, DELIM, &nextToken);
                col++;
            }
            free(copy);

            if (strlen(id) > 0) {
                char rowPair[128];
                int pairLen;
                sprintf(rowPair, "%s" DELIM "%d\n", id, ver);
                pairLen = (int)strlen(rowPair);
                idVerBuf = (char*)realloc(idVerBuf, idVerLen + pairLen + 1);
                strcpy(idVerBuf + idVerLen, rowPair);
                idVerLen += pairLen;
            }
        }
        fclose(f);
    }

    for (i = 0; i < MAX_SERVERS; i++) {
        int targetNodeID;
        SOCKET sock;
        struct sockaddr_in serverAddr;
        char header[256];
        char recvBuf[1024];
        char *receivedData = NULL;
        int recvSize = 0;
        int timeouts = 0;

        if (strcmp(g_servers[i], "0") == 0 || strcmp(g_servers[i], "") == 0 || !_IsIP(g_servers[i])) continue;
        if (_IsLocalIP(g_servers[i])) continue;

        targetNodeID = i + 1;

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) continue;

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(g_servers[i]);
        serverAddr.sin_port = htons((u_short)g_port);

        if (!_ConnectTimeout(sock, &serverAddr, TCP_TIMEOUT_MS)) {
            closesocket(sock);
            continue;
        }

        sprintf(header, "CLIENT_SYNC_DELTA" DELIM "%d" DELIM "%d\n", g_myNodeID, isEmpty ? 1 : 0);
        _SendTimeout(sock, header, (int)strlen(header), TCP_TIMEOUT_MS);

        if (idVerBuf && idVerLen > 0) {
            _SendTimeout(sock, idVerBuf, idVerLen, TCP_TIMEOUT_MS);
        }

        _SendTimeout(sock, "[EOF]", 5, TCP_TIMEOUT_MS);

        while (timeouts < 15) { 
            int bytes = _RecvTimeout(sock, recvBuf, sizeof(recvBuf) - 1, TCP_TIMEOUT_MS);
            if (bytes > 0) {
                recvBuf[bytes] = '\0';
                receivedData = (char*)realloc(receivedData, recvSize + bytes + 1);
                memcpy(receivedData + recvSize, recvBuf, bytes + 1);
                recvSize += bytes;
                timeouts = 0;
                if (strstr(receivedData, "[EOF]")) break;
            } else {
                timeouts++;
            }
        }

        if (receivedData) {
            char *eofPtr = strstr(receivedData, "[EOF]");
            if (eofPtr) *eofPtr = '\0';
            
            if (strlen(receivedData) > 0) {
                _MergeCsvData(g_csvPath, receivedData);
            }
            free(receivedData);

            g_nodeSyncCycles[i]++;
            _Log("Successful sync completed with Node %d (Cycle count: %d/%d)", targetNodeID, g_nodeSyncCycles[i], g_deleteThreshold);

            if (g_nodeSyncCycles[i] >= g_deleteThreshold) {
                _Log("Delete threshold reached for Node %d. Running node-specific cleanup.", targetNodeID);
                _ProcessNodeSpecificCleanup(g_csvPath, targetNodeID);
                g_nodeSyncCycles[i] = 0;
            }
        }

        closesocket(sock);
    }

    if (idVerBuf) free(idVerBuf);
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            WSADATA wsaData;
            struct sockaddr_in serverAddr;
            long timerInterval;

            hFixedFont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    FIXED_PITCH | FF_MODERN, "Courier");

            hEditOut = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
                                    10, 10, 605, 340, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);

            if (hFixedFont) {
                SendMessage(hEditOut, WM_SETFONT, (WPARAM)hFixedFont, TRUE);
            }

            InitConfig();

            if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
                _Log("[ERROR] WSAStartup failed.");
                break;
            }

            g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (g_listenSock == INVALID_SOCKET) {
                _Log("[ERROR] Failed to create listen socket.");
                WSACleanup();
                break;
            }

            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            serverAddr.sin_port = htons((u_short)g_port);

            if (bind(g_listenSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                _Log("[ERROR] Failed to bind port %d.", g_port);
                closesocket(g_listenSock);
                g_listenSock = INVALID_SOCKET;
                WSACleanup();
                break;
            }

            if (listen(g_listenSock, 7) == SOCKET_ERROR) {
                _Log("[ERROR] Failed to listen on socket.");
                closesocket(g_listenSock);
                g_listenSock = INVALID_SOCKET;
                WSACleanup();
                break;
            }

            if (WSAAsyncSelect(g_listenSock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT) == SOCKET_ERROR) {
                _Log("[ERROR] WSAAsyncSelect failed for listen socket.");
                closesocket(g_listenSock);
                g_listenSock = INVALID_SOCKET;
                WSACleanup();
                break;
            }

            _Log("================================================");
            _Log("   Delta Sync Service Started (Win16)");
            _Log("   Node ID      : %d", g_myNodeID);
            _Log("   IP Address   : %s", g_myIP);
            _Log("   Port         : %d", g_port);
            _Log("   Interval (ms): %ld", g_syncIntervalMs);
            _Log("================================================");

            // Cap timer interval to a safe 16-bit UINT max (60000ms = 1 minute) or value
            timerInterval = (g_syncIntervalMs > 60000L) ? 60000L : g_syncIntervalMs;
            SetTimer(hwnd, ID_TIMER_SYNC, (UINT)timerInterval, NULL);
            break;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 20 && h > 20) {
                MoveWindow(hEditOut, 10, 10, w - 20, h - 20, TRUE);
            }
            break;
        }
        case WM_TIMER: {
            if (wParam == ID_TIMER_SYNC) {
                _RunAllClientSyncs();
            }
            break;
        }
        case WM_SOCKET_NOTIFY: {
            SOCKET s = (SOCKET)wParam;
            if (WSAGETSELECTERROR(lParam)) break;

            if (s == g_listenSock) {
                if (WSAGETSELECTEVENT(lParam) == FD_ACCEPT) {
                    struct sockaddr_in clientAddr;
                    int clientAddrLen = sizeof(clientAddr);
                    SOCKET clientSock = accept(g_listenSock, (struct sockaddr*)&clientAddr, &clientAddrLen);
                    if (clientSock != INVALID_SOCKET) {
                        WSAAsyncSelect(clientSock, hwnd, WM_SOCKET_NOTIFY, FD_READ | FD_CLOSE);
                    }
                }
            } else {
                int event = WSAGETSELECTEVENT(lParam);
                if (event == FD_READ) {
                    _HandleServerClient(s);
                } else if (event == FD_CLOSE) {
                    closesocket(s);
                }
            }
            break;
        }
        case WM_DESTROY:
            KillTimer(hwnd, ID_TIMER_SYNC);
            if (g_listenSock != INVALID_SOCKET) {
                closesocket(g_listenSock);
                WSACleanup();
            }
            if (hFixedFont) DeleteObject(hFixedFont);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "Win16AdHocSyncClass";
    WNDCLASS wc;
    MSG msg;

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow(CLASS_NAME, "Ad-Hoc Delta Sync Service (Win16)", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
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