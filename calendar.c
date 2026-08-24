/* ============================================================================
 * CSV Calendar - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s calendar.c commdlg.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s calendar.c
 *     wlink system windows option quiet option packcode option stack=16k name calendar.exe file calendar.obj library windows.lib library commdlg.lib
 *
 * REQUIREMENTS: Windows 3.1x (Win16)
 * DEPENDENCIES: USER, GDI, COMDLG
 *
 * FEATURES:
 * - INI configuration persistence
 * - Basic event viewing and editing
 * - Print & Export
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

/* Maximum dynamically loaded UI objects to protect heap limits */
#define MAX_EVENTS 512
#define MAX_UPCOMING 512

/* Custom round fallback for older math libraries */
#define ROUND_F(x) ((int)((x) + 0.5f))

/* Custom macro names to avoid Watcom 16-bit strict type evaluation conflicts */
#ifndef CSV_MAX
#define CSV_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef CSV_MIN
#define CSV_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

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

/* --- Zoom & Canvas Configuration --- */
float fZoom = 1.0f;
int iCanvasWidth = 1100;
int iCanvasHeight = 1440;
const int iTimeColWidth = 65;
const int iHeaderH = 50;
const int iSubHeaderH = 30;

int iScrollX = 0, iScrollY = 300;
int iClientW = 1050, iClientH = 700;

/* --- Window Position / Size / State --- */
int iWinX = CW_USEDEFAULT, iWinY = CW_USEDEFAULT;
int iWinW = 1050, iWinH = 700;
int iWinMax = 0;

int g_savedWinX = CW_USEDEFAULT, g_savedWinY = CW_USEDEFAULT;
int g_savedWinW = 1050, g_savedWinH = 700;
int g_savedWinMax = 0;
int g_savedNodeID = 1;

int iViewMode = 1;
char sCurrentDate[16];
char aPeople[7][64] = {"Jack", "Bob", "Charlie", "David", "Eve", "Frank", "Grace"};
int numPeople = 7;
int iEditingPersonIdx = -1;

int iMyNodeID = 1;

/* --- File I/O Configuration --- */
char sCSVFile[128];
char sINIFile[128];

/* --- Event Data Structure --- */
typedef struct {
    char id[64];
    char title[1024];
    int startMin;
    int duration;
    COLORREF color;
    char date[16];
    int personIdx;
    int version;
    int lastModifiedBy;
} Event;

/* Replaced single dynamic array with an Array of Pointers to bypass 64KB Win16 boundaries */
Event FAR* aEvents[MAX_EVENTS];
int numEvents = 0;

/* --- Interaction State Variables --- */
int iDragMode = 0;
int iDragIndex = -1;
int iDragOffsetY = 0;
int iOrigStart = 0, iOrigDuration = 0;
int bCopyTriggered = 0;
int iEditingIndex = -1;
int iSelectedForDelete = -1;

/* --- UI Handles --- */
HWND hMainGUI, hCanvas, hInPlaceEdit;
HWND hBtnZoomIn, hBtnZoomOut, hBtnPrev, hBtnNext, hBtnPrevDay, hBtnNextDay;
HWND hBtnPrint, hBtnExport, hBtnDelete, hComboView, hLblDateTitle;
HFONT hUIFont, hTitleFont;

/* --- Macros --- */
#define RGB_HEX(hex) RGB(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)
#define CONTRAST_COLOR(c) (((GetRValue(c)*299 + GetGValue(c)*587 + GetBValue(c)*114)/1000 > 125) ? RGB(0,0,0) : RGB(255,255,255))
#define DARKEN(c, p) RGB(GetRValue(c)*(100-p)/100, GetGValue(c)*(100-p)/100, GetBValue(c)*(100-p)/100)

/* --- Function Prototypes --- */
BOOL CALLBACK __export SetFontEnumProc(HWND hwnd, LPARAM lParam);
int GetSortedUpcomingIndices(int FAR* upIndices, int maxCount);

void UpdateScrollBars(void);
void UpdateDateTitle(void);
void SetZoom(float fNewZoom);
void OpenInPlaceEdit(int eIdx);
void CloseInPlaceEdit(int bSave);
void LoadCSV(void);
void SaveCSV(void);
void LoadINI(void);
void SaveINI(void);
void DrawCalendar(HDC hDC);
void DrawTimelineView(HDC hDC, int w, int h);
void DrawMonthView(HDC hDC, int w, int h);
void DrawUpcomingView(HDC hDC, int w, int h);
void PrintSchedule(void);
void ExportUpcomingSchedule(void);
void AddEvent(const char* id, const char* title, int start, int dur, COLORREF col, const char* dt, int pIdx, int ver, int modBy);
void MarkEventModified(int idx);
int GetEventColumnIndex(int eIdx);
int IsPeopleView(void);
int GetColCount(void);
void GetCalcDate(time_t t, char* out);
void DateAdd(char* ioDate, char unit, int amt);
int DateDiffDays(const char* d1, const char* d2);
int GetDayOfWeek(int y, int m, int d);
int GetDaysInMonth(int y, int m);
void MinToTimeString(int min, char* out);
void FormatDayHeader(const char* inDate, char* outStr);
LRESULT CALLBACK __export MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK __export CanvasWndProc(HWND, UINT, WPARAM, LPARAM);

void GetEventScreenRect(int eIdx, RECT FAR* r);
void PrintTimelineVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void PrintMonthVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void PrintUpcomingVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void InitConfig(void);

char* GenerateEventID(char* outBuffer) {
    time_t now = time(NULL);
    int randPart = rand() % 10000;
    sprintf(outBuffer, "%ld%04d", (long)now, randPart);
    return outBuffer;
}

void InitConfig(void) {
    char exePath[128];
    GetModuleFileName(NULL, exePath, 128);

    char drive[3], dir[128], fname[128], ext[16];
    _splitpath(exePath, drive, dir, fname, ext);

    sprintf(sINIFile, "%s%s%s.ini", drive, dir, fname);
    sprintf(sCSVFile, "%s%s%s.csv", drive, dir, fname);

    FILE *f = fopen(sINIFile, "r");
    if (!f) {
        f = fopen(sINIFile, "w");
        if (f) {
            fprintf(f, "[Window]\nNode=1\nWinX=-1\nWinY=-1\nWinW=1050\nWinH=700\nMaximized=0\n");
            fprintf(f, "[People]\nNames=Jack,Bob,Charlie,David,Eve,Frank,Grace\n");
            fclose(f);
        }
    } else {
        fclose(f);
    }

    char tempNames[512];
    GetPrivateProfileString("People", "Names", "", tempNames, sizeof(tempNames), sINIFile);
    if (lstrlen(tempNames) == 0) {
        lstrcpy(tempNames, "Jack,Bob,Charlie,David,Eve,Frank,Grace");
        WritePrivateProfileString("People", "Names", tempNames, sINIFile);
    }
    
    char* nextToken = NULL;
    char* token = strtok_s(tempNames, ",", &nextToken);
    int p = 0;
    while (token && p < 7) {
        strncpy(aPeople[p], token, 63);
        aPeople[p][63] = '\0';
        token = strtok_s(NULL, ",", &nextToken);
        p++;
    }

    char temp[32];
    GetPrivateProfileString("Window", "Node", "1", temp, sizeof(temp), sINIFile);
    iMyNodeID = atoi(temp);
    if (iMyNodeID < 1) iMyNodeID = 1; 

    GetPrivateProfileString("Window", "WinX", "-1", temp, sizeof(temp), sINIFile);
    iWinX = atoi(temp);
    if (iWinX < 0) iWinX = CW_USEDEFAULT;

    GetPrivateProfileString("Window", "WinY", "-1", temp, sizeof(temp), sINIFile);
    iWinY = atoi(temp);
    if (iWinY < 0) iWinY = CW_USEDEFAULT;

    GetPrivateProfileString("Window", "WinW", "1050", temp, sizeof(temp), sINIFile);
    iWinW = atoi(temp);
    if (iWinW < 200) iWinW = 1050;

    GetPrivateProfileString("Window", "WinH", "700", temp, sizeof(temp), sINIFile);
    iWinH = atoi(temp);
    if (iWinH < 200) iWinH = 700;

    GetPrivateProfileString("Window", "Maximized", "0", temp, sizeof(temp), sINIFile);
    iWinMax = (atoi(temp) == 1);

    g_savedWinX   = iWinX;
    g_savedWinY   = iWinY;
    g_savedWinW   = iWinW;
    g_savedWinH   = iWinH;
    g_savedWinMax = iWinMax;
    g_savedNodeID = iMyNodeID;
}

void SaveINI(void) {
    FILE* fp = fopen(sINIFile, "w");
    if (!fp) return;

    fprintf(fp, "[Window]\n");
    fprintf(fp, "Node=%d\n", iMyNodeID);
    fprintf(fp, "WinX=%d\n", iWinX);
    fprintf(fp, "WinY=%d\n", iWinY);
    fprintf(fp, "WinW=%d\n", iWinW);
    fprintf(fp, "WinH=%d\n", iWinH);
    fprintf(fp, "Maximized=%d\n", iWinMax ? 1 : 0);
    
    fprintf(fp, "[People]\n");
    fprintf(fp, "Names=%s,%s,%s,%s,%s,%s,%s\n", 
            aPeople[0], aPeople[1], aPeople[2], aPeople[3], 
            aPeople[4], aPeople[5], aPeople[6]);

    fclose(fp);
}

void AddEvent(const char* id, const char* title, int start, int dur, COLORREF col, const char* dt, int pIdx, int ver, int modBy) {
    if (numEvents >= MAX_EVENTS) return;
    
    aEvents[numEvents] = (Event FAR*)malloc(sizeof(Event));
    if (!aEvents[numEvents]) return;
    
    /* Strictly zero out memory to prevent corrupted string padding on save */
    memset(aEvents[numEvents], 0, sizeof(Event));
    
    strncpy(aEvents[numEvents]->id, id && strlen(id) > 0 ? id : "", 63);
    strncpy(aEvents[numEvents]->title, title, 1023);
    aEvents[numEvents]->startMin = start;
    aEvents[numEvents]->duration = dur;
    aEvents[numEvents]->color = col;
    strncpy(aEvents[numEvents]->date, dt, 15);
    aEvents[numEvents]->personIdx = pIdx;
    aEvents[numEvents]->version = ver;
    aEvents[numEvents]->lastModifiedBy = modBy > 0 ? modBy : iMyNodeID;
    
    numEvents++;
}

void MarkEventModified(int idx) {
    if (idx >= 0 && idx < numEvents) {
        aEvents[idx]->version++;
        aEvents[idx]->lastModifiedBy = iMyNodeID;
    }
}

/* Iterative string replacement shifted onto the FAR heap to protect the Win16 stack */
void ReplaceAll(char* str, const char* search, const char* replace) {
    char FAR* buffer;
    char* p;
    char* current = str;
    size_t searchLen;
    
    buffer = (char FAR*)malloc(4096);
    if (!buffer) return;
    
    searchLen = lstrlen(search);
    buffer[0] = '\0';
    
    while ((p = strstr(current, search)) != NULL) {
        int segLen = p - current;
        int bufLen = lstrlen(buffer);
        
        if (bufLen + segLen + lstrlen(replace) >= 4095) break; 
        
        strncpy(buffer + bufLen, current, segLen);
        buffer[bufLen + segLen] = '\0';
        
        lstrcat(buffer, replace);
        current = p + searchLen;
    }
    
    if (lstrlen(buffer) + lstrlen(current) < 4095) {
        lstrcat(buffer, current);
        lstrcpy(str, buffer);
    }
    free(buffer);
}

/* Character-strict token parser bypasses all codepage literal evaluation bugs */
char* GetNextCSVToken(char** context) {
    char* start;
    char* p;
    
    if (!context || !*context) return NULL;
    start = *context;
    p = start;
    
    while (*p && (unsigned char)*p != 0xA6) p++;

    if ((unsigned char)*p == 0xA6) {
        *p = '\0';
        *context = p + 1;
    } else {
        char* nl = strpbrk(start, "\r\n");
        if (nl) *nl = '\0';
        *context = NULL;
    }
    return start;
}

void LoadCSV(void) {
    FILE* fp = fopen(sCSVFile, "r");
    char FAR* line;

    if (!fp) return;
    
    line = (char FAR*)malloc(2048);
    if (!line) { fclose(fp); return; }
    
    fgets(line, 2048, fp); /* Skip header */

    while (fgets(line, 2048, fp)) {
        char id[64], title[1024], dt[16];
        int start = 0, dur = 0, pIdx = 0, ver = 1, modBy = -1;
        long col_val = 0; 
        char* ctx = line;
        char* token;
        char delimStr[2];

        if (lstrlen(line) < 10) continue; 

        memset(id, 0, 64);
        memset(title, 0, 1024);
        memset(dt, 0, 16);

        token = GetNextCSVToken(&ctx);
        if (token) strncpy(id, token, 63);

        token = GetNextCSVToken(&ctx);
        if (token) strncpy(title, token, 1023);

        token = GetNextCSVToken(&ctx);
        if (token) start = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token) dur = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token) col_val = atol(token); /* Safely parses full 32-bit color values */

        token = GetNextCSVToken(&ctx);
        if (token) strncpy(dt, token, 15);

        token = GetNextCSVToken(&ctx);
        if (token) pIdx = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token && *token) ver = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token && *token) modBy = atoi(token);

        if (modBy < 0) modBy = iMyNodeID;

        delimStr[0] = (char)0xA6;
        delimStr[1] = '\0';
        ReplaceAll(title, "%2C", delimStr);
        ReplaceAll(title, "%0A", "\r\n");

        AddEvent(id, title, start, dur, col_val, dt, pIdx, ver, modBy);
    }
    free(line);
    fclose(fp);
}

void SaveCSV(void) {
    FILE* fp = fopen(sCSVFile, "w");
    int i;
    char FAR* title;
    char delimStr[2];
    
    if (!fp) return;
    
    title = (char FAR*)malloc(4096);
    if (!title) { fclose(fp); return; }
    
    delimStr[0] = (char)0xA6;
    delimStr[1] = '\0';
    
    /* Safely format header using explicit bytes */
    fprintf(fp, "ID%cTitle%cStartMin%cDuration%cColor%cDate%cPersonIdx%cVersion%cLastModifiedBy\n",
            0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6);
            
    for (i = 0; i < numEvents; i++) {
        lstrcpy(title, aEvents[i]->title);
        ReplaceAll(title, delimStr, "%2C");
        ReplaceAll(title, "\r\n", "%0A");
        ReplaceAll(title, "\n", "%0A");
        
        fprintf(fp, "%s%c%s%c%d%c%d%c%ld%c%s%c%d%c%d%c%d\n",
                aEvents[i]->id, 0xA6, title, 0xA6, aEvents[i]->startMin, 0xA6, 
                aEvents[i]->duration, 0xA6, aEvents[i]->color, 0xA6, 
                aEvents[i]->date, 0xA6, aEvents[i]->personIdx, 0xA6, 
                aEvents[i]->version, 0xA6, aEvents[i]->lastModifiedBy);
    }
    free(title);
    fclose(fp);
}

void DrawCalendar(HDC hWinDC) {
    RECT rc; 
    int w, h;
    HDC hDC;
    HBITMAP hBmp, hOldBmp;
    RECT rAll;
    
    GetClientRect(hCanvas, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    hDC = CreateCompatibleDC(hWinDC);
    hBmp = CreateCompatibleBitmap(hWinDC, w, h);
    hOldBmp = (HBITMAP)SelectObject(hDC, hBmp);

    rAll.left = 0; rAll.top = 0; rAll.right = w; rAll.bottom = h;
    FillRect(hDC, &rAll, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(hDC, TRANSPARENT);

    if (iViewMode == 6) DrawMonthView(hDC, w, h);
    else if (iViewMode == 7) DrawUpcomingView(hDC, w, h);
    else DrawTimelineView(hDC, w, h);

    BitBlt(hWinDC, 0, 0, w, h, hDC, 0, 0, SRCCOPY);
    
    SelectObject(hDC, hOldBmp);
    DeleteObject(hBmp); 
    DeleteDC(hDC);
}

void DrawTimelineView(HDC hDC, int w, int h) {
    int effW = CSV_MAX(w, iCanvasWidth);
    int cols = GetColCount();
    int dColW = (effW - iTimeColWidth) / cols;
    int i, c;

    /* Store original GDI objects before replacing them */
    HPEN hOldPen = (HPEN)SelectObject(hDC, GetStockObject(BLACK_PEN));
    HFONT hOldFont = (HFONT)SelectObject(hDC, GetStockObject(SYSTEM_FONT));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hDC, GetStockObject(WHITE_BRUSH));

    HPEN hPenHr = CreatePen(PS_SOLID, 1, RGB_HEX(0xE0E0E0));
    HPEN hPenHf = CreatePen(PS_DOT, 1, RGB_HEX(0xF0F0F0));
    HFONT hFontTime = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hFontEv = CreateFont(13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");

    for (i = 0; i <= 24; i++) {
        int y = ROUND_F((i * 60) * fZoom) - iScrollY + iSubHeaderH;
        if (y >= iSubHeaderH - 10 && y <= h) {
            SelectObject(hDC, hPenHr);
            MoveTo(hDC, iTimeColWidth - iScrollX, y); LineTo(hDC, effW - iScrollX, y);
            if (i > 0 && i < 24) {
                char tm[16]; 
                RECT tr;
                sprintf(tm, "%d %s", i > 12 ? i - 12 : (i == 0 ? 12 : i), i >= 12 ? "PM" : "AM");
                if (i == 12) lstrcpy(tm, "12 PM");
                SelectObject(hDC, hFontTime); SetTextColor(hDC, RGB_HEX(0x70757A));
                tr.left = 0; tr.top = y - 10; tr.right = iTimeColWidth - 8 - iScrollX; tr.bottom = y + 10;
                DrawText(hDC, tm, -1, &tr, DT_RIGHT | DT_SINGLELINE);
            }
            if (i < 24) {
                int hfy = ROUND_F(((i * 60) + 30) * fZoom) - iScrollY + iSubHeaderH;
                if (hfy >= iSubHeaderH && hfy <= h) {
                    SelectObject(hDC, hPenHf);
                    MoveTo(hDC, iTimeColWidth - iScrollX, hfy); LineTo(hDC, effW - iScrollX, hfy);
                }
            }
        }
    }
    SelectObject(hDC, hPenHr);
    for (c = 0; c <= cols; c++) {
        int cx = iTimeColWidth + c * dColW - iScrollX;
        MoveTo(hDC, cx, iSubHeaderH); LineTo(hDC, cx, h);
    }

    SelectObject(hDC, hFontEv);
    for (i = 0; i < numEvents; i++) {
        RECT r; 
        if (aEvents[i]->color == 2) continue;
        
        GetEventScreenRect(i, &r);
        if (r.bottom > iSubHeaderH && r.top < h && r.right > r.left) {
            HBRUSH hb = CreateSolidBrush(aEvents[i]->color);
            HPEN hp = CreatePen(PS_SOLID, 1, DARKEN(aEvents[i]->color, 20));
            SelectObject(hDC, hb); SelectObject(hDC, hp);
            RoundRect(hDC, r.left, r.top, r.right, r.bottom, 8, 8);
            if (i != iEditingIndex && (r.bottom - r.top) > 18) {
                RECT tr;
                char t1[16], t2[16], dispText[1080];
                SetTextColor(hDC, CONTRAST_COLOR(aEvents[i]->color));
                tr.left = r.left + 8; tr.top = r.top + 4; tr.right = r.right - 4; tr.bottom = r.bottom - 2;
                MinToTimeString(aEvents[i]->startMin, t1);
                MinToTimeString(aEvents[i]->startMin + aEvents[i]->duration, t2);
                sprintf(dispText, "%s\r\n%s - %s", aEvents[i]->title, t1, t2);
                DrawText(hDC, dispText, -1, &tr, DT_LEFT | DT_WORDBREAK);
            }
            if ((r.bottom - r.top) > 24) {
                HPEN hPenGrip = CreatePen(PS_SOLID, 1, DARKEN(aEvents[i]->color, 35));
                int midX = r.left + ((r.right - r.left) / 2);
                SelectObject(hDC, hPenGrip);
                MoveTo(hDC, midX - 12, r.top + 3); LineTo(hDC, midX + 12, r.top + 3);
                MoveTo(hDC, midX - 12, r.bottom - 3); LineTo(hDC, midX + 12, r.bottom - 3);
                
                SelectObject(hDC, hp); 
                DeleteObject(hPenGrip);
            }
            
            SelectObject(hDC, hOldBrush); SelectObject(hDC, hOldPen); 
            DeleteObject(hb); DeleteObject(hp);
            SelectObject(hDC, hFontEv); 
        }
    }

    {
        char todayStr[16]; 
        int redCol = -1;
        GetCalcDate(time(NULL), todayStr);
        if (IsPeopleView() && strcmp(sCurrentDate, todayStr) == 0) redCol = -2;
        else if (!IsPeopleView()) {
            int diff = DateDiffDays(sCurrentDate, todayStr);
            if (diff >= 0 && diff < cols) redCol = diff;
        }
        if (redCol != -1) {
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            int curMin = (tm->tm_hour * 60) + tm->tm_min;
            int nowY = ROUND_F(curMin * fZoom) - iScrollY + iSubHeaderH;
            if (nowY >= iSubHeaderH && nowY <= h) {
                HPEN hPenRed = CreatePen(PS_SOLID, 2, RGB_HEX(0xEA4335));
                HBRUSH hBrushRed = CreateSolidBrush(RGB_HEX(0xEA4335));
                int rLeft = (redCol == -2) ? (iTimeColWidth - iScrollX) : (iTimeColWidth + (redCol * dColW) - iScrollX);
                int rRight = (redCol == -2) ? (effW - iScrollX) : (rLeft + dColW);
                SelectObject(hDC, hPenRed); SelectObject(hDC, hBrushRed);
                MoveTo(hDC, rLeft, nowY); LineTo(hDC, rRight, nowY);
                Ellipse(hDC, rLeft - 5, nowY - 5, rLeft + 5, nowY + 5);
                
                SelectObject(hDC, hOldBrush); SelectObject(hDC, hOldPen);
                DeleteObject(hPenRed); DeleteObject(hBrushRed);
            }
        }
    }

    SelectObject(hDC, hFontEv); SetTextColor(hDC, RGB_HEX(0x3C4043));
    for (c = 0; c < cols; c++) {
        RECT tr;
        char hdr[64] = {0};
        tr.left = iTimeColWidth + c * dColW - iScrollX; tr.top = 6; 
        tr.right = iTimeColWidth + (c + 1) * dColW - iScrollX; tr.bottom = 25;
        if (IsPeopleView()) lstrcpy(hdr, c < numPeople ? aPeople[c] : "Person");
        else {
            char dt[16]; lstrcpy(dt, sCurrentDate); DateAdd(dt, 'd', c);
            FormatDayHeader(dt, hdr);
        }
        DrawText(hDC, hdr, -1, &tr, DT_CENTER | DT_SINGLELINE);
    }

    SelectObject(hDC, hOldFont);
    SelectObject(hDC, hOldPen);
    SelectObject(hDC, hOldBrush);
    
    DeleteObject(hPenHr); DeleteObject(hPenHf); 
    DeleteObject(hFontTime); DeleteObject(hFontEv);
}

void DrawMonthView(HDC hDC, int w, int h) {
    int yr, m, d, i, row, col;
    int daysInMonth, startDay, colW, rowH;
    RECT rSub;
    HBRUSH hSubBg, hBrushToday, hBrushGrey;
    HPEN hPenBorder;
    HFONT hBold, hDayFont, hEvFont;
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char todayStr[16]; 

    HPEN hOldPen = (HPEN)SelectObject(hDC, GetStockObject(BLACK_PEN));
    HFONT hOldFont = (HFONT)SelectObject(hDC, GetStockObject(SYSTEM_FONT));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hDC, GetStockObject(WHITE_BRUSH));

    sscanf(sCurrentDate, "%d/%d/%d", &yr, &m, &d);
    daysInMonth = GetDaysInMonth(yr, m);
    startDay = GetDayOfWeek(yr, m, 1);
    colW = w / 7;
    rowH = (h - iSubHeaderH) / 6;

    rSub.left = 0; rSub.top = 0; rSub.right = w; rSub.bottom = iSubHeaderH;
    hSubBg = CreateSolidBrush(RGB_HEX(0xF8F9FA));
    FillRect(hDC, &rSub, hSubBg); 
    DeleteObject(hSubBg);

    hBold = CreateFont(13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    hDayFont = CreateFont(12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    hEvFont = CreateFont(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    
    SelectObject(hDC, hBold); SetTextColor(hDC, RGB_HEX(0x3C4043));
    for (i = 0; i < 7; i++) {
        RECT rc;
        rc.left = i * colW; rc.top = 6; rc.right = (i + 1) * colW; rc.bottom = iSubHeaderH;
        DrawText(hDC, days[i], -1, &rc, DT_CENTER | DT_SINGLELINE);
    }

    hPenBorder = CreatePen(PS_SOLID, 1, RGB_HEX(0xDADCE0));
    hBrushToday = CreateSolidBrush(RGB(254, 247, 224));
    hBrushGrey = CreateSolidBrush(RGB_HEX(0xF1F3F4));
    SelectObject(hDC, hPenBorder);

    GetCalcDate(time(NULL), todayStr);

    for (row = 0; row < 6; row++) {
        for (col = 0; col < 7; col++) {
            int cellIdx = (row * 7) + col + 1;
            int dayNum = cellIdx - startDay + 1;
            int x1 = col * colW, y1 = iSubHeaderH + (row * rowH);
            int x2 = x1 + colW, y2 = y1 + rowH;
            RECT cellRect;
            
            cellRect.left = x1; cellRect.top = y1; cellRect.right = x2; cellRect.bottom = y2;

            if (dayNum >= 1 && dayNum <= daysInMonth) {
                char cellDate[16]; 
                RECT numRect;
                char sDay[8];
                int dayEvents[100]; 
                int count = 0, j, p, maxVisible;

                sprintf(cellDate, "%04d/%02d/%02d", yr, m, dayNum);
                if (strcmp(cellDate, todayStr) == 0) FillRect(hDC, &cellRect, hBrushToday);

                SelectObject(hDC, hDayFont);
                SetTextColor(hDC, (strcmp(cellDate, todayStr) == 0) ? RGB_HEX(0x1A73E8) : RGB_HEX(0x3C4043));
                numRect.left = x1; numRect.top = y1 + 4; numRect.right = x2 - 6; numRect.bottom = y1 + 22;
                sprintf(sDay, "%d", dayNum);
                DrawText(hDC, sDay, -1, &numRect, DT_RIGHT | DT_SINGLELINE);

                for (i = 0; i < numEvents && count < 100; i++) {
                    if (aEvents[i]->color != 2 && strcmp(aEvents[i]->date, cellDate) == 0) {
                        dayEvents[count++] = i;
                    }
                }
                for (i = 0; i < count - 1; i++) {
                    for (j = i + 1; j < count; j++) {
                        if (aEvents[dayEvents[j]]->startMin < aEvents[dayEvents[i]]->startMin) {
                            int tmp = dayEvents[i]; dayEvents[i] = dayEvents[j]; dayEvents[j] = tmp;
                        }
                    }
                }

                maxVisible = (rowH - 22) / 15;
                if (count > maxVisible) maxVisible -= 1;

                SelectObject(hDC, hEvFont);
                for (p = 0; p < count; p++) {
                    if (p < maxVisible) {
                        int eIdx = dayEvents[p];
                        int pTop = y1 + 22 + (p * 15);
                        int pBottom = pTop + 13;
                        int pLeft = x1 + 4, pRight = x2 - 4;

                        HBRUSH hBrushEv = CreateSolidBrush(aEvents[eIdx]->color);
                        HPEN hPenEv = CreatePen(PS_SOLID, 1, DARKEN(aEvents[eIdx]->color, 15));
                        SelectObject(hDC, hPenEv); SelectObject(hDC, hBrushEv);
                        RoundRect(hDC, pLeft, pTop, pRight, pBottom, 4, 4);

                        if (eIdx != iEditingIndex) {
                            RECT evRect;
                            SetTextColor(hDC, CONTRAST_COLOR(aEvents[eIdx]->color));
                            evRect.left = pLeft + 5; evRect.top = pTop; evRect.right = pRight - 5; evRect.bottom = pBottom;
                            DrawText(hDC, aEvents[eIdx]->title, -1, &evRect, DT_LEFT | DT_SINGLELINE);
                        }
                        
                        SelectObject(hDC, hOldBrush); SelectObject(hDC, hPenBorder); 
                        DeleteObject(hPenEv); DeleteObject(hBrushEv);
                    } else if (p == maxVisible) {
                        RECT moreRect;
                        char sMore[32]; 
                        SetTextColor(hDC, RGB_HEX(0x5F6368));
                        moreRect.left = x1 + 6; moreRect.top = y1 + 22 + (p * 15); moreRect.right = x2 - 6; moreRect.bottom = y2;
                        sprintf(sMore, "+%d more", count - maxVisible);
                        DrawText(hDC, sMore, -1, &moreRect, DT_LEFT | DT_SINGLELINE);
                        break;
                    }
                }
            } else {
                FillRect(hDC, &cellRect, hBrushGrey);
            }
            SelectObject(hDC, hPenBorder);
            MoveTo(hDC, x1, y1); LineTo(hDC, x2, y1);
            MoveTo(hDC, x1, y1); LineTo(hDC, x1, y2);
        }
    }
    
    SelectObject(hDC, hOldPen);
    SelectObject(hDC, hOldFont);
    
    DeleteObject(hBold); DeleteObject(hDayFont); DeleteObject(hEvFont);
    DeleteObject(hPenBorder); DeleteObject(hBrushToday); DeleteObject(hBrushGrey);
}

void DrawUpcomingView(HDC hDC, int w, int h) {
    int effW = CSV_MAX(w, iCanvasWidth);
    HFONT hFontTitle = CreateFont(20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hFontText = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    int FAR* upIndices = (int FAR*)malloc(MAX_UPCOMING * sizeof(int));
    int count, y, i;
    HPEN hOldPen = (HPEN)SelectObject(hDC, GetStockObject(BLACK_PEN));
    HFONT hOldFont = (HFONT)SelectObject(hDC, GetStockObject(SYSTEM_FONT));

    if (!upIndices) {
        SelectObject(hDC, hOldPen); SelectObject(hDC, hOldFont);
        DeleteObject(hFontTitle); DeleteObject(hFontText);
        return;
    }

    count = GetSortedUpcomingIndices(upIndices, MAX_UPCOMING);
    y = 20 - iScrollY;

    for (i = 0; i < count; i++) {
        int e = upIndices[i];
        if (y + 100 > 0 && y < h) {
            int cardLeft = 20 - iScrollX;
            int cardRight = effW - 20 - iScrollX;
            int cardTop = y, cardBottom = y + 95;
            HBRUSH hBrushEv, hBrushBg;
            RECT colorRect, bgRect;
            HPEN hPenLine;
            char sTime1[32], sTime2[32], sPerson[64], desc[256];

            hBrushEv = CreateSolidBrush(aEvents[e]->color);
            colorRect.left = cardLeft; colorRect.top = cardTop; colorRect.right = cardLeft + 15; colorRect.bottom = cardBottom;
            FillRect(hDC, &colorRect, hBrushEv); DeleteObject(hBrushEv);

            hBrushBg = CreateSolidBrush(RGB_HEX(0xF8F9FA));
            bgRect.left = cardLeft + 15; bgRect.top = cardTop; bgRect.right = cardRight; bgRect.bottom = cardBottom;
            FillRect(hDC, &bgRect, hBrushBg); DeleteObject(hBrushBg);

            hPenLine = CreatePen(PS_SOLID, 1, RGB_HEX(0xDADCE0));
            SelectObject(hDC, hPenLine);
            MoveTo(hDC, cardLeft, cardTop); LineTo(hDC, cardRight, cardTop);
            LineTo(hDC, cardRight, cardBottom); LineTo(hDC, cardLeft, cardBottom);
            LineTo(hDC, cardLeft, cardTop); 
            
            SelectObject(hDC, hOldPen);
            DeleteObject(hPenLine);

            if (e != iEditingIndex) {
                RECT tTitle;
                SelectObject(hDC, hFontTitle); SetTextColor(hDC, RGB_HEX(0x202124));
                tTitle.left = cardLeft + 35; tTitle.top = y + 16; tTitle.right = cardRight - 15; tTitle.bottom = y + 50;
                DrawText(hDC, aEvents[e]->title, -1, &tTitle, DT_LEFT | DT_TOP | DT_SINGLELINE);
            }

            MinToTimeString(aEvents[e]->startMin, sTime1);
            MinToTimeString(aEvents[e]->startMin + aEvents[e]->duration, sTime2);
            lstrcpy(sPerson, "");
            if (aEvents[e]->personIdx < numPeople) lstrcpy(sPerson, aPeople[aEvents[e]->personIdx]);

            sprintf(desc, "%s       %s - %s       %s", aEvents[e]->date, sTime1, sTime2, sPerson);
            SelectObject(hDC, hFontText); SetTextColor(hDC, RGB_HEX(0x5F6368));
            {
                RECT tDesc;
                tDesc.left = cardLeft + 35; tDesc.top = y + 56; tDesc.right = cardRight - 15; tDesc.bottom = y + 86;
                DrawText(hDC, desc, -1, &tDesc, DT_LEFT | DT_TOP | DT_SINGLELINE);
            }
        }
        y += 115;
    }
    
    free(upIndices);
    SelectObject(hDC, hOldFont);
    DeleteObject(hFontTitle); DeleteObject(hFontText);
}

void ExportUpcomingSchedule(void) {
    OPENFILENAME ofn;
    char szFile[128] = "Upcoming_Schedule.txt";
    
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainGUI;
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0"; 
    ofn.lpstrFile = szFile; 
    ofn.nMaxFile = 128;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    
    if (GetSaveFileName(&ofn)) {
        FILE* fp = fopen(szFile, "w");
        if (fp) {
            int FAR* upIndices = (int FAR*)malloc(MAX_UPCOMING * sizeof(int));
            int count, i;
            
            fprintf(fp, "========================================\n           UPCOMING SCHEDULE            \n========================================\n\n");
            if (upIndices) {
                count = GetSortedUpcomingIndices(upIndices, MAX_UPCOMING);
                for (i = 0; i < count; i++) {
                    int e = upIndices[i];
                    char sTime1[32], sTime2[32], sPerson[64];
                    MinToTimeString(aEvents[e]->startMin, sTime1);
                    MinToTimeString(aEvents[e]->startMin + aEvents[e]->duration, sTime2);
                    lstrcpy(sPerson, "");
                    if (aEvents[e]->personIdx < numPeople) lstrcpy(sPerson, aPeople[aEvents[e]->personIdx]);
                    fprintf(fp, "%s\n%s       %s - %s       %s\n----------------------------------------\n",
                            aEvents[e]->title, aEvents[e]->date, sTime1, sTime2, sPerson);
                }
                free(upIndices);
            }
            fclose(fp);
            MessageBox(hMainGUI, "Export Successful", "Success", MB_OK | MB_ICONINFORMATION);
        }
    }
}

void PrintSchedule(void) {
    PRINTDLG pd;
    memset(&pd, 0, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hMainGUI;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS;

    if (PrintDlg(&pd) && pd.hDC) {
        DOCINFO di;
        int dpiX, dpiY, w, h;
        RECT rPage;
        
        memset(&di, 0, sizeof(di));
        di.cbSize = sizeof(DOCINFO);
        di.lpszDocName = "Calendar Print";

        StartDoc(pd.hDC, &di);
        StartPage(pd.hDC);
        SetBkMode(pd.hDC, TRANSPARENT);

        dpiX = GetDeviceCaps(pd.hDC, LOGPIXELSX);
        dpiY = GetDeviceCaps(pd.hDC, LOGPIXELSY);
        w = GetDeviceCaps(pd.hDC, HORZRES);
        h = GetDeviceCaps(pd.hDC, VERTRES);

        rPage.left = dpiX / 4; rPage.top = dpiY / 4; 
        rPage.right = w - (dpiX / 4); rPage.bottom = h - (dpiY / 4);

        if (iViewMode <= 5) PrintTimelineVector(pd.hDC, rPage, dpiX, dpiY);
        else if (iViewMode == 6) PrintMonthVector(pd.hDC, rPage, dpiX, dpiY);
        else if (iViewMode == 7) PrintUpcomingVector(pd.hDC, rPage, dpiX, dpiY);

        EndPage(pd.hDC);
        EndDoc(pd.hDC);
        DeleteDC(pd.hDC);
    }
}

void PrintTimelineVector(HDC hDC, RECT rPage, int dpiX, int dpiY) { 
    MessageBox(hMainGUI, "Timeline Print requires full GDI vector maps", "Info", MB_OK);
}
void PrintMonthVector(HDC hDC, RECT rPage, int dpiX, int dpiY) { }
void PrintUpcomingVector(HDC hDC, RECT rPage, int dpiX, int dpiY) { }

BOOL CALLBACK __export SetFontEnumProc(HWND hwnd, LPARAM lParam) {
    SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

typedef struct { int origIdx; char sortKey[32]; } UpcomingEntry;

int CompareUpcoming(const void* a, const void* b) {
    return lstrcmp(((UpcomingEntry*)a)->sortKey, ((UpcomingEntry*)b)->sortKey);
}

int GetSortedUpcomingIndices(int FAR* outIndices, int maxOut) {
    UpcomingEntry FAR* entries;
    int count = 0, i, outCount;
    
    entries = (UpcomingEntry FAR*)malloc(maxOut * sizeof(UpcomingEntry));
    if (!entries) return 0;
    
    for (i = 0; i < numEvents && count < maxOut; i++) {
        if (lstrcmp(aEvents[i]->date, sCurrentDate) >= 0 && aEvents[i]->color != 2) {
            entries[count].origIdx = i;
            sprintf(entries[count].sortKey, "%s%04d", aEvents[i]->date, aEvents[i]->startMin);
            count++;
        }
    }
    
    qsort(entries, count, sizeof(UpcomingEntry), CompareUpcoming);
    outCount = (count < maxOut) ? count : maxOut;
    for (i = 0; i < outCount; i++) outIndices[i] = entries[i].origIdx;
    
    free(entries);
    return outCount;
}

int GetEventColumnIndex(int eIdx) {
    if (IsPeopleView()) {
        if (DateDiffDays(sCurrentDate, aEvents[eIdx]->date) == 0 && aEvents[eIdx]->personIdx < GetColCount())
            return aEvents[eIdx]->personIdx;
    } else {
        int diff = DateDiffDays(sCurrentDate, aEvents[eIdx]->date);
        if (diff >= 0 && diff < GetColCount()) return diff;
    }
    return -1;
}

int IsPeopleView(void) { return (iViewMode == 4 || iViewMode == 5); }

int GetColCount(void) {
    if (iViewMode == 1) return 1;
    if (iViewMode == 2 || iViewMode == 4) return 4;
    if (iViewMode == 3 || iViewMode == 5) return 7;
    return 1;
}

void GetCalcDate(time_t t, char* out) {
    struct tm* tm = localtime(&t);
    sprintf(out, "%04d/%02d/%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

void DateAdd(char* ioDate, char unit, int amt) {
    int y, m, d;
    struct tm t;
    memset(&t, 0, sizeof(t));
    sscanf(ioDate, "%d/%d/%d", &y, &m, &d);
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
    if (unit == 'd') t.tm_mday += amt;
    else if (unit == 'M') t.tm_mon += amt;
    mktime(&t);
    sprintf(ioDate, "%04d/%02d/%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

/* Explicit Math Julian Date avoids all 16-bit C-Runtime <time.h> Overflow and Y2K bugs */
long DateToAbsolute(int y, int m, int d) {
    if (m < 3) { y--; m += 12; }
    return 365L * y + y/4 - y/100 + y/400 + (153L * m - 457)/5 + d - 306;
}

int DateDiffDays(const char* d1, const char* d2) {
    int y1=0, m1=0, dy1=0, y2=0, m2=0, dy2=0;
    sscanf(d1, "%d/%d/%d", &y1, &m1, &dy1);
    sscanf(d2, "%d/%d/%d", &y2, &m2, &dy2);
    return (int)(DateToAbsolute(y2, m2, dy2) - DateToAbsolute(y1, m1, dy1));
}

int GetDayOfWeek(int y, int m, int d) {
    long absDate = DateToAbsolute(y, m, d);
    return (int)((absDate % 7L) + 1L); /* 1=Sun, 2=Mon... */
}

int GetDaysInMonth(int y, int m) {
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

void MinToTimeString(int min, char* out) {
    int h, m, ampm, dispH;
    if (min < 0) min = 0;
    if (min > 1440) min = 1440;
    h = min / 60;
    m = min % 60;
    ampm = (h >= 12) ? 1 : 0;
    dispH = h > 12 ? h - 12 : (h == 0 ? 12 : h);
    sprintf(out, "%d:%02d %s", dispH, m, ampm ? "PM" : "AM");
}

void FormatDayHeader(const char* inDate, char* outStr) {
    int y, m, d, dow;
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    sscanf(inDate, "%d/%d/%d", &y, &m, &d);
    dow = GetDayOfWeek(y, m, d) - 1;
    sprintf(outStr, "%s %02d/%02d", days[dow], m, d);
}

void FormatDateTitle(const char* inDate, char* outStr) {
    int y, m, d;
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    sscanf(inDate, "%d/%d/%d", &y, &m, &d);
    sprintf(outStr, "%s %d, %d", months[m-1], d, y);
}

void SetZoom(float fNewZoom) {
    int visH;
    float centerMin;
    if (iViewMode >= 6) return;
    if (fNewZoom < 0.6f) fNewZoom = 0.6f;
    if (fNewZoom > 2.5f) fNewZoom = 2.5f;
    if (fNewZoom == fZoom) return;

    CloseInPlaceEdit(1);
    visH = iClientH - iHeaderH - iSubHeaderH;
    centerMin = (iScrollY + (visH / 2.0f)) / fZoom;
    fZoom = fNewZoom;

    UpdateScrollBars();
    iScrollY = ROUND_F((centerMin * fZoom) - (visH / 2.0f));
    UpdateScrollBars();
    InvalidateRect(hCanvas, NULL, TRUE);
}

void OpenInPlaceEdit(int eIdx) {
    RECT r;
    CloseInPlaceEdit(1);
    iEditingIndex = eIdx;
    GetEventScreenRect(eIdx, &r);
    if (!IsRectEmpty(&r)) {
        int w = CSV_MAX(120, r.right - r.left);
        int h = CSV_MAX(50, r.bottom - r.top);
        SetWindowText(hInPlaceEdit, aEvents[eIdx]->title);
        MoveWindow(hInPlaceEdit, r.left, r.top, w, h, TRUE);
        ShowWindow(hInPlaceEdit, SW_SHOW);
        SetFocus(hInPlaceEdit);
    }
}

void UpdateScrollBars(void) {
    RECT rc; 
    int effW, visH, maxX;
    GetClientRect(hMainGUI, &rc);
    iClientW = rc.right - rc.left; iClientH = rc.bottom - rc.top;

    effW = CSV_MAX(iClientW, iCanvasWidth);
    visH = iClientH - iHeaderH - iSubHeaderH;
    if (visH < 0) visH = 0;

    if (iViewMode == 6) {
        iScrollY = 0;
        SetScrollRange(hCanvas, SB_VERT, 0, 0, TRUE);
        SetScrollPos(hCanvas, SB_VERT, 0, TRUE);
    } else {
        int maxY;
        if (iViewMode == 7) {
            int c = 0, i;
            for (i = 0; i < numEvents; i++)
                if (lstrcmp(aEvents[i]->date, sCurrentDate) >= 0 && aEvents[i]->color != 2) c++;
            iCanvasHeight = (c * 115) + 40;
        } else {
            iCanvasHeight = ROUND_F(1440 * fZoom);
        }
        maxY = CSV_MAX(0, iCanvasHeight - visH);
        iScrollY = CSV_MAX(0, CSV_MIN(iScrollY, maxY));
        SetScrollRange(hCanvas, SB_VERT, 0, iCanvasHeight, FALSE);
        SetScrollPos(hCanvas, SB_VERT, iScrollY, TRUE);
    }

    maxX = CSV_MAX(0, effW - iClientW);
    iScrollX = CSV_MAX(0, CSV_MIN(iScrollX, maxX));
    SetScrollRange(hCanvas, SB_HORZ, 0, effW, FALSE);
    SetScrollPos(hCanvas, SB_HORZ, iScrollX, TRUE);
}

void UpdateDateTitle(void) {
    char txt[128] = {0};
    const char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    int y, m, d;
    sscanf(sCurrentDate, "%d/%d/%d", &y, &m, &d);

    if (iViewMode == 1) { char buf[64]; FormatDateTitle(sCurrentDate, buf); sprintf(txt, "%s", buf); }
    else if (iViewMode == 2) {
        char end[16]; lstrcpy(end, sCurrentDate); DateAdd(end, 'd', 3);
        char b1[64], b2[64]; FormatDateTitle(sCurrentDate, b1); FormatDateTitle(end, b2);
        sprintf(txt, "%s - %s", b1, b2);
    }
    else if (iViewMode == 3) {
        char end[16]; lstrcpy(end, sCurrentDate); DateAdd(end, 'd', 6);
        char b1[64], b2[64]; FormatDateTitle(sCurrentDate, b1); FormatDateTitle(end, b2);
        sprintf(txt, "%s - %s", b1, b2);
    }
    else if (iViewMode == 4) { char buf[64]; FormatDateTitle(sCurrentDate, buf); sprintf(txt, "%s (4 Person Team)", buf); }
    else if (iViewMode == 5) { char buf[64]; FormatDateTitle(sCurrentDate, buf); sprintf(txt, "%s (7 Person Team)", buf); }
    else if (iViewMode == 6) sprintf(txt, "%s %d", months[m-1], y);
    else if (iViewMode == 7) lstrcpy(txt, "Upcoming Schedule");

    SetWindowText(hLblDateTitle, txt);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASS wc;
    MSG msg;
    int i;
    FARPROC lpfnSetFont;
    const char* views[] = {"1 Day View", "4 Day View", "Week View", "4 Person View", "7 Person View", "Month View", "Upcoming Schedule"};

    InitConfig();
    GetCalcDate(time(NULL), sCurrentDate);
    LoadCSV();

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "CSVCalendarMain";
    RegisterClass(&wc);

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CanvasWndProc;
    wc.lpszClassName = "CSVCalendarCanvas";
    RegisterClass(&wc);

    hMainGUI = CreateWindow("CSVCalendarMain", "CSV Calendar", 
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            iWinX, iWinY, iWinW, iWinH, 
                            NULL, NULL, hInst, NULL);
    
    if (!hMainGUI) return 1;

    hUIFont = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    hTitleFont = CreateFont(22, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");

    hBtnZoomIn = CreateWindow("BUTTON", "+", WS_CHILD | WS_VISIBLE, 10, 12, 28, 26, hMainGUI, (HMENU)101, hInst, NULL);
    hBtnZoomOut = CreateWindow("BUTTON", "-", WS_CHILD | WS_VISIBLE, 42, 12, 28, 26, hMainGUI, (HMENU)102, hInst, NULL);
    hBtnPrev = CreateWindow("BUTTON", "< Prev", WS_CHILD | WS_VISIBLE, 80, 12, 60, 26, hMainGUI, (HMENU)103, hInst, NULL);
    hBtnNext = CreateWindow("BUTTON", "Next >", WS_CHILD | WS_VISIBLE, 145, 12, 60, 26, hMainGUI, (HMENU)104, hInst, NULL);
    hBtnPrevDay = CreateWindow("BUTTON", "< Day", WS_CHILD | WS_VISIBLE, 215, 12, 50, 26, hMainGUI, (HMENU)105, hInst, NULL);
    hBtnNextDay = CreateWindow("BUTTON", "Day >", WS_CHILD | WS_VISIBLE, 270, 12, 50, 26, hMainGUI, (HMENU)106, hInst, NULL);
    hBtnPrint = CreateWindow("BUTTON", "Print", WS_CHILD | WS_VISIBLE, 330, 12, 55, 26, hMainGUI, (HMENU)107, hInst, NULL);
    hBtnExport = CreateWindow("BUTTON", "Export", WS_CHILD | WS_VISIBLE, 390, 12, 55, 26, hMainGUI, (HMENU)108, hInst, NULL);

    hComboView = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 455, 13, 140, 200, hMainGUI, (HMENU)109, hInst, NULL);
    for (i = 0; i < 7; i++) SendMessage(hComboView, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)views[i]);
    SendMessage(hComboView, CB_SETCURSEL, 0, 0);

    hBtnDelete = CreateWindow("BUTTON", "Delete", WS_CHILD | WS_VISIBLE, 605, 12, 55, 26, hMainGUI, (HMENU)110, hInst, NULL);
    hLblDateTitle = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 675, 10, 275, 32, hMainGUI, NULL, hInst, NULL);

    lpfnSetFont = MakeProcInstance((FARPROC)SetFontEnumProc, hInst);
    EnumChildWindows(hMainGUI, (WNDENUMPROC)lpfnSetFont, (LPARAM)hUIFont);
    FreeProcInstance(lpfnSetFont);

    SendMessage(hLblDateTitle, WM_SETFONT, (WPARAM)hTitleFont, 0);

    hCanvas = CreateWindow("CSVCalendarCanvas", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                           0, iHeaderH, iClientW, iClientH - iHeaderH, hMainGUI, NULL, hInst, NULL);

    hInPlaceEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                                -500, -500, 100, 100, hCanvas, NULL, hInst, NULL);
    SendMessage(hInPlaceEdit, WM_SETFONT, (WPARAM)hUIFont, 0);

    UpdateDateTitle();
    UpdateScrollBars();

    if (iWinMax) {
        ShowWindow(hMainGUI, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(hMainGUI, SW_SHOW);
    }
    UpdateWindow(hMainGUI);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (iWinX != g_savedWinX || iWinY != g_savedWinY || iWinW != g_savedWinW || 
        iWinH != g_savedWinH || iWinMax != g_savedWinMax || iMyNodeID != g_savedNodeID) {
        SaveINI();
    }
    SaveCSV();

    for (i = 0; i < numEvents; i++) {
        if (aEvents[i]) free(aEvents[i]);
    }

    DeleteObject(hUIFont);
    DeleteObject(hTitleFont);

    return 0;
}

LRESULT CALLBACK __export MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            iClientW = LOWORD(lParam);
            iClientH = HIWORD(lParam);
            if (hCanvas) {
                MoveWindow(hCanvas, 0, iHeaderH, iClientW, iClientH - iHeaderH, TRUE);
                UpdateScrollBars();
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = wParam;
            HWND hwndCtl = (HWND)LOWORD(lParam);
            int notifyCode = HIWORD(lParam);

            if (notifyCode == CBN_SELCHANGE && hwndCtl == hComboView) {
                CloseInPlaceEdit(1);
                iSelectedForDelete = -1;
                iViewMode = (int)SendMessage(hComboView, CB_GETCURSEL, 0, 0) + 1;
                UpdateDateTitle();
                UpdateScrollBars();
                InvalidateRect(hCanvas, NULL, TRUE);
                SetFocus(hMainGUI);
            }
            if (id >= 101 && id <= 111) CloseInPlaceEdit(1);
            if (id == 101) SetZoom(fZoom + 0.2f);
            if (id == 102) SetZoom(fZoom - 0.2f);
            if (id == 103 || id == 104) {
                int dir = (id == 103) ? -1 : 1;
                iSelectedForDelete = -1;
                if (iViewMode == 1 || iViewMode == 4 || iViewMode == 5) DateAdd(sCurrentDate, 'd', 1 * dir);
                else if (iViewMode == 2) DateAdd(sCurrentDate, 'd', 4 * dir);
                else if (iViewMode == 3) DateAdd(sCurrentDate, 'd', 7 * dir);
                else if (iViewMode >= 6) DateAdd(sCurrentDate, 'M', 1 * dir);
                UpdateDateTitle(); UpdateScrollBars(); InvalidateRect(hCanvas, NULL, TRUE);
            }
            if (id == 105 || id == 106) {
                iSelectedForDelete = -1;
                DateAdd(sCurrentDate, 'd', (id == 105) ? -1 : 1);
                UpdateDateTitle(); InvalidateRect(hCanvas, NULL, TRUE);
            }
            if (id == 107) PrintSchedule();
            if (id == 108) ExportUpcomingSchedule();
            if (id == 110) {
                if (iSelectedForDelete != -1 && iSelectedForDelete < numEvents) {
                    aEvents[iSelectedForDelete]->color = 2;
                    MarkEventModified(iSelectedForDelete);
                    if (iEditingIndex == iSelectedForDelete) CloseInPlaceEdit(0);
                    iSelectedForDelete = -1;
                    UpdateScrollBars();
                    InvalidateRect(hCanvas, NULL, TRUE);
                }
            }
            return 0;
        }
        case WM_DESTROY: {
            WINDOWPLACEMENT wp;
            wp.length = sizeof(WINDOWPLACEMENT);
            if (GetWindowPlacement(hWnd, &wp)) {
                iWinMax = (wp.showCmd == SW_SHOWMAXIMIZED);
                iWinX = wp.rcNormalPosition.left;
                iWinY = wp.rcNormalPosition.top;
                iWinW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                iWinH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void GetEventScreenRect(int eIdx, RECT FAR* r) {
    int cw = iClientW;
    int ch = iClientH;

    SetRectEmpty(r);
    if (eIdx < 0 || eIdx >= numEvents) return;
    if (aEvents[eIdx]->color == 2) return;

    if (hCanvas != NULL) {
        RECT rcCanvas;
        GetClientRect(hCanvas, &rcCanvas);
        if (rcCanvas.right > 0) {
            cw = rcCanvas.right - rcCanvas.left;
            ch = rcCanvas.bottom - rcCanvas.top;
        }
    }

    if (iViewMode <= 5) {
        int colIdx = GetEventColumnIndex(eIdx);
        int effW = CSV_MAX(cw, iCanvasWidth);
        int dColW = (effW - iTimeColWidth) / GetColCount();
        if (colIdx < 0) return;
        r->left = iTimeColWidth + (colIdx * dColW) - iScrollX + 4;
        r->top = ROUND_F(aEvents[eIdx]->startMin * fZoom) - iScrollY + iSubHeaderH;
        r->right = r->left + dColW - 8;
        r->bottom = ROUND_F((aEvents[eIdx]->startMin + aEvents[eIdx]->duration) * fZoom) - iScrollY + iSubHeaderH;
    } else if (iViewMode == 6) {
        int y, m, d, curY, curM, curD;
        int startDay, cellIdx, row, col, colW, rowH;
        int dayEvents[100]; 
        int count = 0, maxVisible, i, j, p;

        sscanf(aEvents[eIdx]->date, "%d/%d/%d", &y, &m, &d);
        sscanf(sCurrentDate, "%d/%d/%d", &curY, &curM, &curD);
        if (y != curY || m != curM) return;

        startDay = GetDayOfWeek(y, m, 1);
        cellIdx = d + startDay - 1;
        row = (cellIdx - 1) / 7;
        col = (cellIdx - 1) % 7;
        colW = cw / 7;
        rowH = (ch - iSubHeaderH) / 6;

        for (i = 0; i < numEvents && count < 100; i++) {
            if (aEvents[i]->color != 2 && strcmp(aEvents[i]->date, aEvents[eIdx]->date) == 0) dayEvents[count++] = i;
        }
        for (i = 0; i < count - 1; i++) {
            for (j = i + 1; j < count; j++) {
                if (aEvents[dayEvents[j]]->startMin < aEvents[dayEvents[i]]->startMin) {
                    int tmp = dayEvents[i]; dayEvents[i] = dayEvents[j]; dayEvents[j] = tmp;
                }
            }
        }
        
        maxVisible = (rowH - 22) / 15;
        if (count > maxVisible) maxVisible -= 1;
        
        for (p = 0; p < count; p++) {
            if (dayEvents[p] == eIdx) {
                if (p < maxVisible) {
                    r->left = (col * colW) + 4;
                    r->right = r->left + colW - 8;
                    r->top = iSubHeaderH + (row * rowH) + 22 + (p * 15);
                    r->bottom = r->top + 13;
                }
                break;
            }
        }
    }
}

int GetDateFromMonthXY(int x, int y, char* outDate) {
    int cw = iClientW;
    int ch = iClientH;
    int colW, rowH, col, row, yr, m, d, startDay, cellIdx, dayNum;

    if (y < iSubHeaderH) return 0;

    if (hCanvas != NULL) {
        RECT rcCanvas;
        GetClientRect(hCanvas, &rcCanvas);
        if (rcCanvas.right > 0) {
            cw = rcCanvas.right - rcCanvas.left;
            ch = rcCanvas.bottom - rcCanvas.top;
        }
    }

    colW = cw / 7;
    rowH = (ch - iSubHeaderH) / 6;
    col = x / colW;
    row = (y - iSubHeaderH) / rowH;

    sscanf(sCurrentDate, "%d/%d/%d", &yr, &m, &d);
    startDay = GetDayOfWeek(yr, m, 1);
    cellIdx = (row * 7) + col + 1;
    dayNum = cellIdx - startDay + 1;
    if (dayNum >= 1 && dayNum <= GetDaysInMonth(yr, m)) {
        sprintf(outDate, "%04d/%02d/%02d", yr, m, dayNum);
        return 1;
    }
    return 0;
}

void CloseInPlaceEdit(int bSave) {
    if (iEditingIndex != -1) {
        if (bSave) {
            char FAR* newText = (char FAR*)malloc(1024);
            if (newText) {
                GetWindowText(hInPlaceEdit, newText, 1024);
                if (strcmp(newText, aEvents[iEditingIndex]->title) != 0) {
                    lstrcpy(aEvents[iEditingIndex]->title, newText);
                    MarkEventModified(iEditingIndex);
                    SaveCSV();
                }
                free(newText);
            }
        }
        ShowWindow(hInPlaceEdit, SW_HIDE);
        MoveWindow(hInPlaceEdit, -500, -500, 10, 10, FALSE);
        iEditingIndex = -1;
        InvalidateRect(hCanvas, NULL, TRUE);
    } 
    else if (iEditingPersonIdx != -1) {
        if (bSave) {
            char newText[64];
            char* nl;
            GetWindowText(hInPlaceEdit, newText, 64);
            
            nl = strpbrk(newText, "\r\n");
            if (nl) *nl = '\0';

            if (strcmp(newText, aPeople[iEditingPersonIdx]) != 0 && strlen(newText) > 0) {
                strncpy(aPeople[iEditingPersonIdx], newText, 63);
                aPeople[iEditingPersonIdx][63] = '\0';
                SaveINI();
            }
        }
        ShowWindow(hInPlaceEdit, SW_HIDE);
        MoveWindow(hInPlaceEdit, -500, -500, 10, 10, FALSE);
        iEditingPersonIdx = -1;
        InvalidateRect(hCanvas, NULL, TRUE);
    }
}

LRESULT CALLBACK __export CanvasWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hDC = BeginPaint(hWnd, &ps);
            DrawCalendar(hDC);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEWHEEL: {
            short delta;
            if (iViewMode == 6) return 0;
            CloseInPlaceEdit(1);
            delta = (short)wParam; 
            iScrollY -= (delta > 0) ? 60 : -60;
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        case WM_VSCROLL:
        case WM_HSCROLL: {
            int req = wParam; 
            int* pScroll = (msg == WM_VSCROLL) ? &iScrollY : &iScrollX;
            int page = (msg == WM_VSCROLL) ? (iClientH - iHeaderH - iSubHeaderH) : iClientW;
            CloseInPlaceEdit(1);
            if (req == SB_LINEUP) *pScroll -= 30;
            if (req == SB_LINEDOWN) *pScroll += 30;
            if (req == SB_PAGEUP) *pScroll -= page;
            if (req == SB_PAGEDOWN) *pScroll += page;
            if (req == SB_THUMBTRACK || req == SB_THUMBPOSITION) *pScroll = (int)LOWORD(lParam);
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            int i;
            CloseInPlaceEdit(1);
            if (my < iSubHeaderH && iViewMode != 7) return 0;

            for (i = numEvents - 1; i >= 0; i--) {
                RECT r; 
                GetEventScreenRect(i, &r);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    iSelectedForDelete = i;
                    iDragIndex = i; iOrigStart = aEvents[i]->startMin; iOrigDuration = aEvents[i]->duration; bCopyTriggered = 0;
                    if (iViewMode >= 6) iDragMode = 1;
                    else if (my - r.top <= 6) iDragMode = 2;
                    else if (r.bottom - my <= 6) iDragMode = 3;
                    else { iDragMode = 1; iDragOffsetY = my - r.top; }
                    break;
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int mx = (short)LOWORD(lParam) + iScrollX, my = (short)HIWORD(lParam) + iScrollY - iSubHeaderH;
            if (iDragMode > 0 && iDragIndex != -1 && iViewMode != 7) {
                if (iViewMode == 6) {
                    char hoverDate[16];
                    if (GetDateFromMonthXY((short)LOWORD(lParam), (short)HIWORD(lParam), hoverDate)) {
                        lstrcpy(aEvents[iDragIndex]->date, hoverDate);
                    }
                } else {
                    int curMin = ROUND_F(((double)my / fZoom) / 15.0) * 15;
                    if (iDragMode == 1) {
                        int nStart = ROUND_F((((double)(short)HIWORD(lParam) + iScrollY - iSubHeaderH - iDragOffsetY) / fZoom) / 15.0) * 15;
                        nStart = CSV_MAX(0, CSV_MIN(1440 - aEvents[iDragIndex]->duration, nStart));
                        aEvents[iDragIndex]->startMin = nStart;
                        if (mx > iTimeColWidth) {
                            int cw = iClientW;
                            int col;
                            if (hCanvas != NULL) {
                                RECT rcCanvas;
                                GetClientRect(hCanvas, &rcCanvas);
                                if (rcCanvas.right > 0) {
                                    cw = rcCanvas.right - rcCanvas.left;
                                }
                            }
                            col = (mx - iTimeColWidth) / ((CSV_MAX(cw, iCanvasWidth) - iTimeColWidth) / GetColCount());
                            if (col >= 0 && col < GetColCount()) {
                                if (IsPeopleView()) aEvents[iDragIndex]->personIdx = col;
                                else { lstrcpy(aEvents[iDragIndex]->date, sCurrentDate); DateAdd(aEvents[iDragIndex]->date, 'd', col); }
                            }
                        }
                    } else if (iDragMode == 2) {
                        curMin = CSV_MAX(0, curMin);
                        if (iOrigStart + iOrigDuration - curMin >= 15) {
                            aEvents[iDragIndex]->startMin = curMin;
                            aEvents[iDragIndex]->duration = iOrigStart + iOrigDuration - curMin;
                        }
                    } else if (iDragMode == 3) {
                        curMin = CSV_MIN(1440, curMin);
                        if (curMin - iOrigStart >= 15) aEvents[iDragIndex]->duration = curMin - iOrigStart;
                    }
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (iDragMode > 0) {
                if (iDragIndex != -1 && iDragIndex < numEvents) MarkEventModified(iDragIndex);
                SaveCSV();
            }
            iDragMode = 0; iDragIndex = -1;
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            int i;
            COLORREF colors[5] = {RGB_HEX(0x039BE5), RGB_HEX(0x33B679), RGB_HEX(0x8E24AA), RGB_HEX(0xF4511E), RGB_HEX(0xE67C73)};
            COLORREF col;

            if (my < iSubHeaderH && iViewMode != 7) {
                if (IsPeopleView()) {
                    int cw = iClientW;
                    int effW, cols, dColW;
                    if (hCanvas != NULL) {
                        RECT rcCanvas;
                        GetClientRect(hCanvas, &rcCanvas);
                        if (rcCanvas.right > 0) cw = rcCanvas.right - rcCanvas.left;
                    }
                    effW = CSV_MAX(cw, iCanvasWidth);
                    cols = GetColCount();
                    dColW = (effW - iTimeColWidth) / cols;
                    
                    if (mx + iScrollX > iTimeColWidth) {
                        int colIdx = (mx + iScrollX - iTimeColWidth) / dColW;
                        if (colIdx >= 0 && colIdx < cols && colIdx < numPeople) {
                            int hx;
                            CloseInPlaceEdit(1);
                            iEditingPersonIdx = colIdx;
                            hx = iTimeColWidth + (colIdx * dColW) - iScrollX;
                            SetWindowText(hInPlaceEdit, aPeople[colIdx]);
                            MoveWindow(hInPlaceEdit, hx + 4, 4, dColW - 8, iSubHeaderH - 8, TRUE);
                            ShowWindow(hInPlaceEdit, SW_SHOW);
                            SetFocus(hInPlaceEdit);
                        }
                    }
                }
                return 0;
            }
            
            for (i = numEvents - 1; i >= 0; i--) {
                RECT r; 
                GetEventScreenRect(i, &r);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    iSelectedForDelete = i;
                    OpenInPlaceEdit(i);
                    return 0;
                }
            }
            
            col = colors[numEvents % 5];

            if (iViewMode == 6) {
                char clickDate[16];
                if (GetDateFromMonthXY(mx, my, clickDate)) {
                    char newID[64] = {0};
                    GenerateEventID(newID);
                    AddEvent(newID, "New Event", 540, 60, col, clickDate, 0, 1, -1);
                    SaveCSV();
                    OpenInPlaceEdit(numEvents - 1);
                }
            } else if (iViewMode == 7) {
                return 0;
            } else if (mx + iScrollX > iTimeColWidth) {
                int cw = iClientW;
                int effW, dColW, colIdx, stMin;
                char dt[16]; 
                char newID[64] = {0};
                
                if (hCanvas != NULL) {
                    RECT rcCanvas;
                    GetClientRect(hCanvas, &rcCanvas);
                    if (rcCanvas.right > 0) cw = rcCanvas.right - rcCanvas.left;
                }

                effW = CSV_MAX(cw, iCanvasWidth);
                dColW = (effW - iTimeColWidth) / GetColCount();
                colIdx = (mx + iScrollX - iTimeColWidth) / dColW;
                if (colIdx >= GetColCount()) colIdx = GetColCount() - 1;

                stMin = ROUND_F((((my + iScrollY - iSubHeaderH) / fZoom) / 30.0f)) * 30;
                if (stMin < 0) stMin = 0;
                if (stMin > 1440 - 60) stMin = 1440 - 60;

                lstrcpy(dt, sCurrentDate);
                if (!IsPeopleView()) DateAdd(dt, 'd', colIdx);
                
                GenerateEventID(newID);
                AddEvent(newID, "New Event", stMin, 60, col, dt, IsPeopleView() ? colIdx : 0, 1, -1);
                SaveCSV();
                OpenInPlaceEdit(numEvents - 1);
            }
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
/* EOF */