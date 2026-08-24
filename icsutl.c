/* ============================================================================
 * Calendar Database Manager & ICS Tool - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s icsutl.c commdlg.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s icsutl.c
 *     wlink system windows option quiet option packcode option stack=16k name icsutl.exe file icsutl.obj library windows.lib library commdlg.lib
 *
 * REQUIREMENTS: Windows 3.1x (Win16)
 * DEPENDENCIES: USER, GDI, COMDLG
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

#ifndef MAX_PATH
#define MAX_PATH 128
#endif

#ifndef BST_CHECKED
#define BST_CHECKED 1
#endif

#ifndef BST_UNCHECKED
#define BST_UNCHECKED 0
#endif

#define MAX_EVENTS 2048

// --- GUI Control IDs ---
#define IDC_INPUT_CSV     101
#define IDC_BTN_BROWSE    102
#define IDC_BTN_EXPORT    103
#define IDC_BTN_IMPORT    104
#define IDC_CHK_SAMENAME  105
#define IDC_BTN_REBUILD   106
#define IDC_BTN_CLEANUP   107
#define IDC_LBL_STATUS    108

// --- Data Structures ---
typedef struct {
    char id[64];
    char title[256];
    int startMin;
    int duration;
    long color;          // Stored as long to protect 32-bit RGB hex values
    char date[16];       // YYYY/MM/DD
    int personIdx;
    int version;
    int lastModifiedBy;
} Event;

// --- Globals ---
Event FAR* g_events[MAX_EVENTS];
int g_eventCount = 0;
char g_szCSVFile[MAX_PATH] = "calendar.csv";

HWND hMain, hInputCSV, hLblStatus, hChkSameName;
HFONT hUIFont = NULL;

// --- Helper Functions ---
void SetStatus(const char* msg) {
    SetWindowText(hLblStatus, msg);
}

void FreeEvents(void) {
    int i;
    for (i = 0; i < g_eventCount; i++) {
        if (g_events[i]) {
            free(g_events[i]);
            g_events[i] = NULL;
        }
    }
    g_eventCount = 0;
}

// String replace utility (allocates new string on the heap, must be freed)
char* StrReplace(const char* orig, const char* rep, const char* with) {
    char *result;
    const char *ins;
    char *tmp;
    int len_rep, len_with, len_front, count;

    if (!orig || !rep) return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0) return NULL;
    if (!with) with = "";
    len_with = strlen(with);

    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = (char*)malloc(strlen(orig) + (len_with - len_rep) * count + 1);
    if (!result) return NULL;

    while (count--) {
        ins = strstr(orig, rep);
        len_front = (int)(ins - orig);
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep;
    }
    strcpy(tmp, orig);
    return result;
}

// --- Date Math Utilities (Bypasses 16-bit time.h limitations) ---
long DateToAbsolute(int y, int m, int d) {
    if (m < 3) { y--; m += 12; }
    return 365L * y + y/4 - y/100 + y/400 + (153L * m - 457)/5 + d - 306;
}

void AbsoluteToDate(long absDate, int* y, int* m, int* d) {
    long l = absDate + 68569 + 306;
    long n = (4 * l) / 146097;
    long i, j;
    l = l - (146097 * n + 3) / 4;
    i = (4000 * (l + 1)) / 1461001;
    l = l - (1461 * i) / 4 + 31;
    j = (80 * l) / 2447;
    *d = (int)(l - (2447 * j) / 80);
    l = j / 11;
    *m = (int)(j + 2 - (12 * l));
    *y = (int)(100 * (n - 49) + i + l);
}

void AddDays(const char* yyyymmdd, int days, char* outStr) {
    int y, m, d;
    if (sscanf(yyyymmdd, "%4d/%2d/%2d", &y, &m, &d) == 3 || sscanf(yyyymmdd, "%4d%2d%2d", &y, &m, &d) == 3) {
        long absDate = DateToAbsolute(y, m, d) + days;
        AbsoluteToDate(absDate, &y, &m, &d);
        sprintf(outStr, "%04d%02d%02d", y, m, d);
    } else {
        lstrcpy(outStr, yyyymmdd);
    }
}

// Character-strict token parser bypasses all codepage literal evaluation bugs
char* GetNextCSVToken(char** context) {
    char* start;
    char* p;
    
    if (!context || !*context) return NULL;
    start = *context;
    p = start;
    
    while (*p && (unsigned char)*p != 0xA6 && *p != '|') {
        if ((unsigned char)*p == 0xC2 && (unsigned char)*(p+1) == 0xA6) {
            *p = '\0';
            *context = p + 2;
            return start;
        }
        p++;
    }

    if ((unsigned char)*p == 0xA6 || *p == '|') {
        *p = '\0';
        *context = p + 1;
    } else {
        char* nl = strpbrk(start, "\r\n");
        if (nl) *nl = '\0';
        *context = NULL;
    }
    return start;
}

// --- Core Database Functions ---
void LoadCSVToMemory(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    char FAR* line;
    int isFirst = 1;
    char msg[128];

    FreeEvents();

    if (!f) {
        SetStatus("Status: CSV file not found. A new one will be created on save.");
        return;
    }

    line = (char FAR*)malloc(2048);
    if (!line) { fclose(f); return; }

    while (fgets(line, 2048, f)) {
        char* ctx = line;
        char* t_id, *t_title, *t_start, *t_dur, *t_col, *t_date, *t_pidx, *t_ver, *t_lmod;
        Event FAR* ev;

        if (isFirst) { isFirst = 0; continue; } // Skip header
        if (lstrlen(line) < 10 || g_eventCount >= MAX_EVENTS) continue;

        t_id = GetNextCSVToken(&ctx);
        t_title = GetNextCSVToken(&ctx);
        t_start = GetNextCSVToken(&ctx);
        t_dur = GetNextCSVToken(&ctx);
        t_col = GetNextCSVToken(&ctx);
        t_date = GetNextCSVToken(&ctx);
        t_pidx = GetNextCSVToken(&ctx);
        t_ver = GetNextCSVToken(&ctx);
        t_lmod = GetNextCSVToken(&ctx);

        if (!t_date) continue;

        g_events[g_eventCount] = (Event FAR*)malloc(sizeof(Event));
        if (!g_events[g_eventCount]) continue;
        memset(g_events[g_eventCount], 0, sizeof(Event));

        ev = g_events[g_eventCount];
        strncpy(ev->id, t_id ? t_id : "", 63);
        strncpy(ev->title, t_title ? t_title : "", 255);
        ev->startMin = t_start ? atoi(t_start) : 0;
        ev->duration = t_dur ? atoi(t_dur) : 60;
        ev->color = t_col ? atol(t_col) : 0;
        strncpy(ev->date, t_date ? t_date : "", 15);
        ev->personIdx = t_pidx ? atoi(t_pidx) : 0;
        ev->version = t_ver ? atoi(t_ver) : 1;
        ev->lastModifiedBy = t_lmod ? atoi(t_lmod) : 1;

        g_eventCount++;
    }
    free(line);
    fclose(f);

    sprintf(msg, "Status: Loaded %d event(s) into memory.", g_eventCount);
    SetStatus(msg);
}

int SaveMemoryToCSV(const char* filepath) {
    FILE* f = fopen(filepath, "w");
    int i;
    if (!f) {
        MessageBox(hMain, "Could not open file for writing.", "Error", MB_ICONHAND);
        return 0;
    }

    fprintf(f, "ID%cTitle%cStartMin%cDuration%cColor%cDate%cPersonIdx%cVersion%cLastModifiedBy\n",
            0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6);
            
    for (i = 0; i < g_eventCount; i++) {
        Event FAR* ev = g_events[i];
        fprintf(f, "%s%c%s%c%d%c%d%c%ld%c%s%c%d%c%d%c%d\n",
            ev->id, 0xA6, ev->title, 0xA6, ev->startMin, 0xA6, ev->duration, 0xA6, ev->color, 0xA6, 
            ev->date, 0xA6, ev->personIdx, 0xA6, ev->version, 0xA6, ev->lastModifiedBy);
    }
    fclose(f);
    return 1;
}

// QSort compare function for Rebuild
int CompareEvents(const void* a, const void* b) {
    Event FAR* ea = *(Event FAR**)a;
    Event FAR* eb = *(Event FAR**)b;
    int dateCmp = lstrcmp(ea->date, eb->date);
    if (dateCmp != 0) return dateCmp;
    return ea->startMin - eb->startMin;
}

void RebuildCSV(void) {
    LoadCSVToMemory(g_szCSVFile);
    if (g_eventCount <= 1) {
        SetStatus("Status: Not enough events in memory to sort.");
        return;
    }
    qsort(g_events, g_eventCount, sizeof(Event FAR*), CompareEvents);
    if (SaveMemoryToCSV(g_szCSVFile)) {
        char msg[128];
        sprintf(msg, "Status: Successfully rebuilt and sorted %d event(s) by date.", g_eventCount);
        SetStatus(msg);
        MessageBox(hMain, "CSV database has been successfully rebuilt and sorted by date.", "Rebuild Complete", MB_ICONASTERISK);
    }
}

void CleanupCSV(void) {
    int initialCount, keepCount, removed, i;
    char msg[128];

    LoadCSVToMemory(g_szCSVFile);
    initialCount = g_eventCount;
    keepCount = 0;

    for (i = 0; i < g_eventCount; i++) {
        if (g_events[i]->color != 2) {
            g_events[keepCount++] = g_events[i];
        } else {
            free(g_events[i]);
        }
    }
    g_eventCount = keepCount;
    removed = initialCount - keepCount;

    if (SaveMemoryToCSV(g_szCSVFile)) {
        sprintf(msg, "Status: Cleanup complete. Erased %d event(s) marked with color 2.", removed);
        SetStatus(msg);
        MessageBox(hMain, msg, "Cleanup Complete", MB_ICONASTERISK);
    }
}

// --- ICS Export / Import ---
void ExportToICS(void) {
    char szSavePath[MAX_PATH];
    FILE* f;
    time_t now;
    struct tm* tm_now;
    char dtstamp[32];
    int i;

    LoadCSVToMemory(g_szCSVFile);
    if (g_eventCount == 0) {
        MessageBox(hMain, "No events loaded in memory to export.", "Export ICS", MB_ICONEXCLAMATION);
        return;
    }

    memset(szSavePath, 0, sizeof(szSavePath));
    if (SendMessage(hChkSameName, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        char* dot;
        lstrcpy(szSavePath, g_szCSVFile);
        dot = strrchr(szSavePath, '.');
        if (dot) *dot = '\0';
        lstrcat(szSavePath, ".ics");
    } else {
        OPENFILENAME ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hMain;
        ofn.lpstrFilter = "iCalendar Files (*.ics)\0*.ics\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szSavePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = "ics";
        if (!GetSaveFileName(&ofn)) return;
    }

    f = fopen(szSavePath, "w");
    if (!f) {
        MessageBox(hMain, "Could not create ICS file at specified path.", "Error", MB_ICONHAND);
        return;
    }

    fprintf(f, "BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//CSV Calendar Utility//EN\n");

    now = time(NULL);
    tm_now = gmtime(&now);
    strftime(dtstamp, sizeof(dtstamp), "%Y%m%dT%H%M%SZ", tm_now);

    for (i = 0; i < g_eventCount; i++) {
        Event FAR* ev = g_events[i];
        char* title1;
        char* title2;
        char sDateStr[16];
        char* slash;
        int startHour, startMin;
        int endTotalMin, endDayExtra, endMinOfDay, endHour, endMinute;
        char sEndDateStr[32];

        if (ev->color == 2) continue; // Skip deleted

        title1 = StrReplace(ev->title, "%0A", "\\n");
        title2 = StrReplace(title1 ? title1 : ev->title, "%2C", ",");

        lstrcpy(sDateStr, ev->date);
        while ((slash = strchr(sDateStr, '/')) != NULL) *slash = '*'; 
        while ((slash = strchr(sDateStr, '*')) != NULL) memmove(slash, slash + 1, strlen(slash));

        startHour = ev->startMin / 60;
        startMin = ev->startMin % 60;
        
        endTotalMin = ev->startMin + ev->duration;
        endDayExtra = endTotalMin / 1440;
        endMinOfDay = endTotalMin % 1440;
        endHour = endMinOfDay / 60;
        endMinute = endMinOfDay % 60;

        lstrcpy(sEndDateStr, sDateStr);
        if (endDayExtra > 0) AddDays(ev->date, endDayExtra, sEndDateStr);

        fprintf(f, "BEGIN:VEVENT\n");
        fprintf(f, "UID:%s@csvcalendar\n", ev->id);
        fprintf(f, "DTSTAMP:%s\n", dtstamp);
        fprintf(f, "DTSTART:%sT%02d%02d00\n", sDateStr, startHour, startMin);
        fprintf(f, "DTEND:%sT%02d%02d00\n", sEndDateStr, endHour, endMinute);
        fprintf(f, "SUMMARY:%s\n", title2 ? title2 : ev->title);
        fprintf(f, "X-CSV-COLOR:%ld\n", ev->color);
        fprintf(f, "END:VEVENT\n");

        if (title1) free(title1);
        if (title2) free(title2);
    }
    fprintf(f, "END:VCALENDAR\n");
    fclose(f);
    
    SetStatus("Status: Successfully exported events.");
    MessageBox(hMain, "Successfully exported events to ICS.", "Export Successful", MB_ICONASTERISK);
}

void ExtractICSElement(const char* block, const char* key, char* out, int outMax) {
    char search[128];
    const char* start;
    out[0] = '\0';
    sprintf(search, "\n%s:", key);
    
    start = strstr(block, search);
    if (!start) {
        sprintf(search, "%s:", key);
        if (strncmp(block, search, strlen(search)) == 0) start = block;
    } else {
        start++; // Skip newline
    }

    if (start) {
        int len;
        const char* end;
        start += strlen(key) + 1;
        end = strpbrk(start, "\r\n");
        len = end ? (int)(end - start) : (int)strlen(start);
        if (len >= outMax) len = outMax - 1;
        strncpy(out, start, len);
        out[len] = '\0';
    }
}

void ImportFromICS(void) {
    char szOpenPath[MAX_PATH];
    OPENFILENAME ofn;
    FILE* f;
    long fsize;
    char FAR* icsContent;
    int added = 0, updated = 0;
    char* block;

    memset(szOpenPath, 0, sizeof(szOpenPath));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = "iCalendar Files (*.ics)\0*.ics\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szOpenPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileName(&ofn)) return;

    f = fopen(szOpenPath, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 60000L) {
        fclose(f);
        MessageBox(hMain, "ICS file is empty or exceeds 60KB limit for Win16.", "Import ICS", MB_ICONEXCLAMATION);
        return;
    }

    icsContent = (char FAR*)malloc((size_t)fsize + 1);
    if (!icsContent) {
        fclose(f);
        MessageBox(hMain, "Not enough memory to load ICS file.", "Import ICS", MB_ICONHAND);
        return;
    }

    fread(icsContent, 1, (size_t)fsize, f);
    fclose(f);
    icsContent[(size_t)fsize] = '\0';

    block = strstr(icsContent, "BEGIN:VEVENT");
    
    while (block) {
        char* nextBlock = strstr(block + 1, "BEGIN:VEVENT");
        char* endBlock = strstr(block, "END:VEVENT");
        
        if (endBlock && (!nextBlock || endBlock < nextBlock)) {
            char summary[256], dtstart[64], dtend[64], color[16], uid[128];
            char dateFormatted[16];
            int startMin = 0;
            int duration = 60;
            long colorVal = 0;
            char* fmtSummary;
            int exists = 0;
            char* atSign;
            char* tPtr;
            int j;

            *endBlock = '\0';
            
            ExtractICSElement(block, "SUMMARY", summary, sizeof(summary));
            ExtractICSElement(block, "DTSTART", dtstart, sizeof(dtstart));
            ExtractICSElement(block, "DTEND", dtend, sizeof(dtend));
            ExtractICSElement(block, "X-CSV-COLOR", color, sizeof(color));
            ExtractICSElement(block, "UID", uid, sizeof(uid));
            
            *endBlock = 'E';
            
            if (uid[0] == '\0') sprintf(uid, "%u%ld", (unsigned)rand(), (long)time(NULL));
            atSign = strchr(uid, '@');
            if (atSign) *atSign = '\0';

            if (dtstart[0] != '\0' && summary[0] != '\0') {
                sprintf(dateFormatted, "%.4s/%.2s/%.2s", dtstart, dtstart+4, dtstart+6);
                
                tPtr = strchr(dtstart, 'T');
                if (tPtr && strlen(tPtr) >= 5) {
                    char hh[3], mm[3];
                    hh[0] = tPtr[1]; hh[1] = tPtr[2]; hh[2] = '\0';
                    mm[0] = tPtr[3]; mm[1] = tPtr[4]; mm[2] = '\0';
                    startMin = (atoi(hh) * 60) + atoi(mm);
                }

                tPtr = strchr(dtend, 'T');
                if (tPtr && strlen(tPtr) >= 5) {
                    char hh[3], mm[3];
                    int endMin;
                    hh[0] = tPtr[1]; hh[1] = tPtr[2]; hh[2] = '\0';
                    mm[0] = tPtr[3]; mm[1] = tPtr[4]; mm[2] = '\0';
                    endMin = (atoi(hh) * 60) + atoi(mm);
                    if (endMin > startMin) duration = endMin - startMin;
                }

                colorVal = color[0] != '\0' ? atol(color) : 0;
                fmtSummary = StrReplace(summary, ",", "%2C");
                
                for (j = 0; j < g_eventCount; j++) {
                    if (strcmp(g_events[j]->id, uid) == 0) {
                        lstrcpy(g_events[j]->title, fmtSummary ? fmtSummary : summary);
                        g_events[j]->startMin = startMin;
                        g_events[j]->duration = duration;
                        if (color[0] != '\0') g_events[j]->color = colorVal;
                        lstrcpy(g_events[j]->date, dateFormatted);
                        g_events[j]->version++;
                        exists = 1;
                        updated++;
                        break;
                    }
                }

                if (!exists && g_eventCount < MAX_EVENTS) {
                    g_events[g_eventCount] = (Event FAR*)malloc(sizeof(Event));
                    if (g_events[g_eventCount]) {
                        Event FAR* ev;
                        memset(g_events[g_eventCount], 0, sizeof(Event));
                        ev = g_events[g_eventCount];
                        strncpy(ev->id, uid, 63);
                        strncpy(ev->title, fmtSummary ? fmtSummary : summary, 255);
                        ev->startMin = startMin;
                        ev->duration = duration;
                        ev->color = colorVal;
                        strncpy(ev->date, dateFormatted, 15);
                        ev->personIdx = 0;
                        ev->version = 1;
                        ev->lastModifiedBy = 1;
                        g_eventCount++;
                        added++;
                    }
                }
                
                if (fmtSummary) free(fmtSummary);
            }
        }
        block = nextBlock;
    }
    
    free(icsContent);

    if (added + updated > 0) {
        SaveMemoryToCSV(g_szCSVFile);
        char msg[128];
        sprintf(msg, "Status: Imported ICS. Added %d new, Updated %d existing.", added, updated);
        SetStatus(msg);
        MessageBox(hMain, msg, "Import Successful", MB_ICONASTERISK);
    } else {
        MessageBox(hMain, "No valid VEVENT blocks found.", "Import ICS", MB_ICONEXCLAMATION);
    }
}

// --- Windows Message Loop ---
LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND hLbl, hBtnBrw, hBtnExp, hBtnImp, hBtnReb, hBtnCln;

            hUIFont = CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 
                                  0, 0, 0, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
                                      
            // Row 1
            hLbl = CreateWindow("STATIC", "Source CSV File:", WS_CHILD | WS_VISIBLE, 
                                20, 20, 90, 20, hwnd, NULL, NULL, NULL);
            if (hUIFont) SendMessage(hLbl, WM_SETFONT, (WPARAM)hUIFont, TRUE);
            
            hInputCSV = CreateWindow("EDIT", g_szCSVFile, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
                                     120, 18, 270, 24, hwnd, (HMENU)IDC_INPUT_CSV, NULL, NULL);
            if (hUIFont) SendMessage(hInputCSV, WM_SETFONT, (WPARAM)hUIFont, TRUE);
            
            hBtnBrw = CreateWindow("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                   400, 17, 95, 26, hwnd, (HMENU)IDC_BTN_BROWSE, NULL, NULL);
            if (hUIFont) SendMessage(hBtnBrw, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            // Row 2
            hBtnExp = CreateWindow("BUTTON", "Export to ICS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                   20, 70, 230, 36, hwnd, (HMENU)IDC_BTN_EXPORT, NULL, NULL);
            if (hUIFont) SendMessage(hBtnExp, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            hBtnImp = CreateWindow("BUTTON", "Import from ICS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                   265, 70, 230, 36, hwnd, (HMENU)IDC_BTN_IMPORT, NULL, NULL);
            if (hUIFont) SendMessage(hBtnImp, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            // Row 2.5
            hChkSameName = CreateWindow("BUTTON", "Use same base filename for Export (.ics)", 
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 
                                        20, 115, 300, 20, hwnd, (HMENU)IDC_CHK_SAMENAME, NULL, NULL);
            if (hUIFont) SendMessage(hChkSameName, WM_SETFONT, (WPARAM)hUIFont, TRUE);
            SendMessage(hChkSameName, BM_SETCHECK, BST_CHECKED, 0);

            // Row 3
            hBtnReb = CreateWindow("BUTTON", "Rebuild CSV (Sort by Date)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                   20, 145, 230, 36, hwnd, (HMENU)IDC_BTN_REBUILD, NULL, NULL);
            if (hUIFont) SendMessage(hBtnReb, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            hBtnCln = CreateWindow("BUTTON", "Cleanup CSV (Remove Color 2)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                   265, 145, 230, 36, hwnd, (HMENU)IDC_BTN_CLEANUP, NULL, NULL);
            if (hUIFont) SendMessage(hBtnCln, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            // Status
            hLblStatus = CreateWindow("STATIC", "Ready. Select a CSV file to begin.", WS_CHILD | WS_VISIBLE, 
                                      20, 205, 475, 40, hwnd, (HMENU)IDC_LBL_STATUS, NULL, NULL);
            if (hUIFont) SendMessage(hLblStatus, WM_SETFONT, (WPARAM)hUIFont, TRUE);
            break;
        }
        case WM_COMMAND: {
            int wmId = wParam; // Win16 parameter unpacking
            switch (wmId) {
                case IDC_BTN_BROWSE: {
                    OPENFILENAME ofn;
                    char szFile[MAX_PATH];
                    memset(szFile, 0, sizeof(szFile));
                    lstrcpy(szFile, g_szCSVFile);
                    memset(&ofn, 0, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) {
                        lstrcpy(g_szCSVFile, szFile);
                        SetWindowText(hInputCSV, g_szCSVFile);
                        LoadCSVToMemory(g_szCSVFile);
                    }
                    break;
                }
                case IDC_BTN_EXPORT: ExportToICS(); break;
                case IDC_BTN_IMPORT: ImportFromICS(); break;
                case IDC_BTN_REBUILD: RebuildCSV(); break;
                case IDC_BTN_CLEANUP: CleanupCSV(); break;
            }
            break;
        }
        case WM_DESTROY:
            if (hUIFont) DeleteObject(hUIFont);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "CalendarManagerClass";
    WNDCLASS wc;
    DWORD style;
    RECT rect;
    MSG msg;

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    rect.left = 0; rect.top = 0; rect.right = 520; rect.bottom = 260;
    AdjustWindowRect(&rect, style, FALSE);

    hMain = CreateWindow(CLASS_NAME, "Calendar Database Utility", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL);

    if (hMain == NULL) return 0;

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);
    
    // Initial Load
    LoadCSVToMemory(g_szCSVFile);

    memset(&msg, 0, sizeof(MSG));
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    FreeEvents();
    return 0;
}
/* EOF */