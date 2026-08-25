/* ============================================================================
 * Calmira Taskbar v0.1 - Fully Self-Contained Win16 Implementation
 *
 * FEATURES:
 * - Single-file C architecture with clean CALLBACK calling conventions (Resolves E2028)
 * - GDI Windows 9x 4-Color Flag Start Button Logo (Persists in pushed state)
 * - Multi-row Task Buttons when Taskbar height exceeds double initial height (>60px)
 * - Ctrl-Alt-Del TaskMgr hook & WinKey / Ctrl-Esc Start Menu triggers
 * - Protected root Start Menu folders (Greys out Delete/Rename)
 * - Root-level item/folder creation ("Blue Bar" context menu)
 * - Global Heap Allocation (GlobalAlloc) preventing DGROUP 64KB Seg Faults
 *
 * COMPILATION INSTRUCTIONS:
 *   wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s taskbar.c shell.lib
 * ============================================================================ */

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dos.h>
#include <direct.h>

#ifndef MAX_PATH
#define MAX_PATH 128
#endif

#ifndef VK_LWIN
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#endif

#ifndef BST_PUSHED
#define BST_PUSHED 0x0004
#endif

/* ──────────────────────────────────────────────────────────────────────────
   Constants & IDs
   ────────────────────────────────────────────────────────────────────────── */
#define START_BTN_WIDTH     74
#define QUICK_LAUNCH_COUNT  4
#define CLOCK_WIDTH         50
#define CLOCK_HEIGHT        22
#define MAX_TASKS           32
#define MAX_TRAY_ICONS      4
#define MAX_START_ITEMS     128
#define MAX_OD_ITEMS        256
#define MAX_CACHE_ICONS     64

#define TIMER_CLOCK         1001
#define TIMER_REFRESH       1002
#define TIMER_HOTKEY        1003

#define ID_START_BUTTON     2001
#define ID_TASK_BASE        3000
#define ID_QUICK_BASE       4000
#define ID_CLOCK            5001
#define ID_TRAY_AREA        5002

#define IDM_START_BASE      8000

/* Context Menus */
#define IDM_TASK_MINIMIZE   9003
#define IDM_TASK_MAXIMIZE   9004
#define IDM_TASK_CLOSE      9005
#define IDM_WINX_SHORTCUT   7001
#define IDM_WINX_NEWFOLDER  7002
#define IDM_WINX_CP         7003
#define IDM_WINX_DEVMAN     7004
#define IDM_TB_PROPS        7005
#define IDM_TB_SHOWDESKTOP  7006
#define IDM_CLOCK_ADJUST    7007

/* Start Menu Items */
#define IDM_SM_PROGRAMS     1101
#define IDM_SM_SEARCH       1103
#define IDM_SM_HELP         1104
#define IDM_SM_RUN          1105
#define IDM_SM_EXIT         1107

#define WM_USER_CONTEXTMENU (WM_USER + 100)
#define IDM_CTX_PROPS       7101
#define IDM_CTX_NEWFOLDER   7102
#define IDM_CTX_NEWSHORTCUT 7103
#define IDM_CTX_RENAME      7104
#define IDM_CTX_DELETE      7105

#define POS_BOTTOM 0
#define POS_TOP    1
#define POS_LEFT   2
#define POS_RIGHT  3

#define PROMPT_NEWFOLDER    1
#define PROMPT_RENAME       2

/* ──────────────────────────────────────────────────────────────────────────
   Data Structures & Globals
   ────────────────────────────────────────────────────────────────────────── */
typedef struct { HWND hWnd; char title[128]; HWND hBtn; } TaskEntry;
typedef struct { char name[16]; char exe[MAX_PATH]; } QuickLaunchDef;
typedef struct { HICON hIcon; char tooltip[32]; } TrayIcon;
typedef struct { char exePath[MAX_PATH]; HICON hIconLarge; } IconCacheEntry;
typedef struct {
    char text[64];
    HICON hIcon;
    int cacheIndex;
    BOOL bDestroyIcon;
    BOOL isRoot;
    BOOL isSeparator;
    int yOffset;
    int totalHeight;
} ODMenuItem;

struct MenuDirMap { HMENU hMenu; char path[MAX_PATH]; };

static HINSTANCE g_hInst;
static HWND g_hTaskbar = NULL;
static HWND g_hStartBtn = NULL;
static HWND g_hTaskList = NULL;
static HWND g_hClock = NULL;
static HWND g_hTrayArea = NULL;
static HWND g_hQuickLaunch[QUICK_LAUNCH_COUNT];
static HWND g_ContextTargetWnd = NULL;

static TaskEntry g_Tasks[MAX_TASKS];
static int g_TaskCount = 0;

static TrayIcon g_TrayIcons[MAX_TRAY_ICONS];
static int g_TrayIconCount = 0;

static ODMenuItem FAR* g_ODItems = NULL;
static int g_ODCount = 0;

static IconCacheEntry FAR* g_IconCache = NULL;
static int g_IconCacheCount = 0;
static int g_LoadedCacheCount = 0;
static HBITMAP g_hCacheBitmap = NULL;

static char (*g_MenuItemLnkPaths)[MAX_PATH] = NULL;
static struct MenuDirMap FAR* g_MenuDirs = NULL;
static int g_StartMenuCounter = 0;
static int g_MenuDirCount = 0;

static char g_ContextPath[MAX_PATH] = "C:\\STARTM";
static BOOL g_ContextIsDir = TRUE;
static char g_ShortcutParentDir[MAX_PATH] = "C:\\STARTM";
static char g_EditShortcutPath[MAX_PATH] = "";

static char g_PromptLabel[64] = "";
static char g_PromptValue[MAX_PATH] = "";
static int g_PromptMode = PROMPT_NEWFOLDER;

static QuickLaunchDef g_QL[QUICK_LAUNCH_COUNT];
static int g_QLActiveCount = 0;

static char g_szIniPath[MAX_PATH];
static char g_szCachePath[MAX_PATH];
static char g_szSearchExe[MAX_PATH];
static char g_szHelpExe[MAX_PATH];
static char g_szTaskMgrExe[MAX_PATH];

static char g_szWinXShortcut[MAX_PATH];
static char g_szWinXProps[MAX_PATH];
static char g_szWinXCP[MAX_PATH];
static char g_szWinXDevMan[MAX_PATH];

static int g_TbPosition = POS_BOTTOM;
static int g_TbHeight = 30;
static int g_TbWidthVert = 72;
static BOOL g_bDragging = FALSE;
static BOOL g_bResizing = FALSE;
static POINT g_DragStartPt;

static HFONT g_hFontMenu = NULL;
static HFONT g_hFontSidebar = NULL;

static HHOOK g_hMsgHook = NULL;
static FARPROC g_lpfnMsgFilter = NULL;

static FARPROC g_lpfnEnumWindowsProc = NULL;
static FARPROC g_lpfnTaskBtnProc = NULL;
static FARPROC g_lpfnTrayAreaProc = NULL;
static FARPROC g_lpfnStartBtnProc = NULL;
static FARPROC g_lpfnClockProc = NULL;

static FARPROC OldTaskBtnProc = NULL;
static FARPROC OldTrayAreaProc = NULL;
static FARPROC OldStartBtnProc = NULL;
static FARPROC OldClockProc = NULL;

/* Global Heap Handles */
static HGLOBAL g_hMemODItems = NULL;
static HGLOBAL g_hMemLnkPaths = NULL;
static HGLOBAL g_hMemMenuDirs = NULL;
static HGLOBAL g_hMemIconCache = NULL;

/* ──────────────────────────────────────────────────────────────────────────
   Prototypes
   ────────────────────────────────────────────────────────────────────────── */
static void InitFonts(void);
static void ApplyLayout(void);
static void RefreshTasks(void);
static void SnapToEdge(POINT pt);
static void RegenerateIconCache(void);
static void DoShowDesktop(void);
static int  GetCachedIconIndex(const char* exePath);
static void SaveConfig(void);
static void LoadConfig(void);
static void AddTrayIcon(HICON hIcon, const char* tooltip);
static void PositionDialogNearStart(HWND hwnd);
static void CreateCenteredDialog(HINSTANCE hInst, HWND hParent, LPCSTR className, LPCSTR title, int w, int h);
static void ClearODIcons(void);
static void SaveQuickLaunchItem(int i);

LRESULT CALLBACK TaskbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK RunDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK TrayAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StartBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ClockProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TaskBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MsgFilterProc(int code, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK    TaskbarEnumWindowsProc(HWND hwnd, LPARAM lParam);
BOOL CALLBACK    MinimizeEnumProc(HWND hwnd, LPARAM lParam);

/* ──────────────────────────────────────────────────────────────────────────
   Utility Functions
   ────────────────────────────────────────────────────────────────────────── */
static void GetAppFilePath(const char* filename, char* outPath) {
    char* p;
    GetModuleFileName(g_hInst, outPath, MAX_PATH);
    p = strrchr(outPath, '\\');
    if (p) lstrcpy(p + 1, filename);
    else lstrcpy(outPath, filename);
}

static void SetFont(HWND hwnd, HFONT font) {
    if (!font) font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    if (!font) font = (HFONT)GetStockObject(SYSTEM_FONT);
    SendMessage(hwnd, WM_SETFONT, (WPARAM)font, MAKELONG(TRUE, 0));
}

static void TrimCaption(const char* src, char* dst, int maxLen) {
    int len = lstrlen(src);
    if (len > maxLen) {
        memcpy(dst, src, maxLen - 3);
        dst[maxLen-3] = '.'; dst[maxLen-2] = '.'; dst[maxLen-1] = '.'; dst[maxLen] = '\0';
    } else lstrcpy(dst, src);
}

static void UpdateClock(void) {
    char timeStr[32];
    time_t rawtime;
    struct tm *info;
    time(&rawtime);
    info = localtime(&rawtime);
    if (info) {
        sprintf(timeStr, "%02d:%02d", info->tm_hour, info->tm_min);
        SetWindowText(g_hClock, timeStr);
    }
}

typedef BOOL (WINAPI *LPGETOPENFILENAME)(LPOPENFILENAME);
static BOOL BrowseFile(HWND hwnd, char* outPath, const char* filter) {
    OPENFILENAME ofn;
    char file[MAX_PATH] = "";
    HINSTANCE hCommDlg;
    BOOL bRet = FALSE;
    
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    
    hCommDlg = LoadLibrary("commdlg.dll");
    if (hCommDlg >= (HINSTANCE)32) {
        LPGETOPENFILENAME pGetOpenFileName = (LPGETOPENFILENAME)GetProcAddress(hCommDlg, "GETOPENFILENAME");
        if (pGetOpenFileName) {
            if (pGetOpenFileName(&ofn)) {
                lstrcpy(outPath, file);
                bRet = TRUE;
            }
        }
        FreeLibrary(hCommDlg);
    }
    return bRet;
}

static void InitFonts(void) {
    g_hFontMenu = CreateFont(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                             FF_SWISS, "Arial");
    
    g_hFontSidebar = CreateFont(-22, 0, 900, 900, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                FF_SWISS, "Arial");
}

static void PositionDialogNearStart(HWND hwnd) {
    RECT rcStart, rcDlg;
    int x, y, w, h;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    GetWindowRect(g_hStartBtn, &rcStart);
    GetWindowRect(hwnd, &rcDlg);
    
    w = rcDlg.right - rcDlg.left;
    h = rcDlg.bottom - rcDlg.top;

    if (g_TbPosition == POS_BOTTOM) {
        x = rcStart.left; y = rcStart.top - h;
    } else if (g_TbPosition == POS_TOP) {
        x = rcStart.left; y = rcStart.bottom;
    } else if (g_TbPosition == POS_LEFT) {
        x = rcStart.right; y = rcStart.top;
    } else {
        x = rcStart.left - w; y = rcStart.top;
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > screenW) x = screenW - w;
    if (y + h > screenH) y = screenH - h;

    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void CreateCenteredDialog(HINSTANCE hInst, HWND hParent, LPCSTR className, LPCSTR title, int w, int h) {
    RECT rcStart;
    int x = 100, y = 100;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    if (g_hStartBtn && IsWindow(g_hStartBtn)) {
        GetWindowRect(g_hStartBtn, &rcStart);
        x = rcStart.left;
        y = rcStart.top - h;
        if (y < 0) y = rcStart.bottom;
    } else {
        x = (screenW - w) / 2;
        y = (screenH - h) / 2;
    }

    if (x + w > screenW) x = screenW - w;
    if (y + h > screenH) y = screenH - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    CreateWindowEx(0, className, title, WS_VISIBLE | WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, hParent, NULL, hInst, NULL);
    EnableWindow(hParent, FALSE);
}

/* ──────────────────────────────────────────────────────────────────────────
   16-Color BMP Icon Caching Engine
   ────────────────────────────────────────────────────────────────────────── */
static HBITMAP Load16ColorBMP(const char* path, HDC hdc) {
    FILE *f = fopen(path, "rb");
    BITMAPFILEHEADER bfh;
    DWORD infoSize, imgSize;
    BITMAPINFO *pbmi;
    LPBYTE pBits;
    HBITMAP hbm;

    if (!f) return NULL;
    fread(&bfh, 1, sizeof(bfh), f);
    if (bfh.bfType != 0x4D42) { fclose(f); return NULL; }
    
    infoSize = bfh.bfOffBits - sizeof(BITMAPFILEHEADER);
    pbmi = (BITMAPINFO*)GlobalLock(GlobalAlloc(GPTR, infoSize));
    if (!pbmi) { fclose(f); return NULL; }
    fread(pbmi, 1, infoSize, f);
    
    imgSize = pbmi->bmiHeader.biSizeImage;
    if (imgSize == 0) {
        int stride = ((pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biBitCount + 31) & ~31) / 8;
        imgSize = stride * abs(pbmi->bmiHeader.biHeight);
    }
    
    pBits = (LPBYTE)GlobalLock(GlobalAlloc(GPTR, imgSize));
    if (!pBits) { GlobalFree((HGLOBAL)pbmi); fclose(f); return NULL; }
    fseek(f, bfh.bfOffBits, SEEK_SET);
    fread(pBits, 1, imgSize, f);
    fclose(f);
    
    hbm = CreateDIBitmap(hdc, &pbmi->bmiHeader, CBM_INIT, pBits, pbmi, DIB_RGB_COLORS);
    GlobalFree((HGLOBAL)pbmi);
    GlobalFree((HGLOBAL)pBits);
    return hbm;
}

static void SaveIconCacheTo16ColorBMP(void) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    int width = g_IconCacheCount * 32;
    int height = 32;
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, width > 0 ? width : 32, height);
    HBITMAP hOld = SelectObject(hdcMem, hbm);
    HBRUSH hbg = CreateSolidBrush(GetSysColor(COLOR_MENU));
    RECT rc;
    int i;
    struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[16]; } bmi;

    RGBQUAD pal[16] = {
        {0,0,0,0}, {0,0,128,0}, {0,128,0,0}, {0,128,128,0},
        {128,0,0,0}, {128,0,128,0}, {128,128,0,0}, {128,128,128,0},
        {192,192,192,0}, {0,0,255,0}, {0,255,0,0}, {0,255,255,0},
        {255,0,0,0}, {255,0,255,0}, {255,255,0,0}, {255,255,255,0}
    };

    rc.left = 0; rc.top = 0; rc.right = width > 0 ? width : 32; rc.bottom = height;
    FillRect(hdcMem, &rc, hbg);
    DeleteObject(hbg);
    
    for (i = 0; i < g_IconCacheCount; i++) {
        if (g_IconCache[i].hIconLarge) DrawIcon(hdcMem, i * 32, 0, g_IconCache[i].hIconLarge);
    }
    
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width > 0 ? width : 32;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 4;
    bmi.bmiHeader.biCompression = BI_RGB;
    memcpy(bmi.bmiColors, pal, sizeof(pal));
    
    GetDIBits(hdcMem, hbm, 0, height, NULL, (BITMAPINFO*)&bmi, DIB_RGB_COLORS);
    if (bmi.bmiHeader.biSizeImage == 0) {
        bmi.bmiHeader.biSizeImage = ((((width > 0 ? width : 32) * 4) + 31) & ~31) / 8 * height;
    }
    
    {
        LPBYTE lpBits = (LPBYTE)GlobalLock(GlobalAlloc(GPTR, bmi.bmiHeader.biSizeImage));
        if (lpBits) {
            if (GetDIBits(hdcMem, hbm, 0, height, lpBits, (BITMAPINFO*)&bmi, DIB_RGB_COLORS)) {
                BITMAPFILEHEADER bfh;
                FILE *f = fopen(g_szCachePath, "wb");
                if (f) {
                    bfh.bfType = 0x4D42;
                    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(bmi) + bmi.bmiHeader.biSizeImage;
                    bfh.bfReserved1 = 0;
                    bfh.bfReserved2 = 0;
                    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(bmi);
                    
                    fwrite(&bfh, 1, sizeof(bfh), f);
                    fwrite(&bmi, 1, sizeof(bmi), f);
                    fwrite(lpBits, 1, bmi.bmiHeader.biSizeImage, f);
                    fclose(f);
                }
            }
            GlobalFree((HGLOBAL)lpBits);
        }
    }
    
    SelectObject(hdcMem, hOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

static int GetCachedIconIndex(const char* exePath) {
    int i;
    if (!exePath || !exePath[0]) return -1;
    for (i = 0; i < g_IconCacheCount; i++) {
        if (lstrcmpi(g_IconCache[i].exePath, exePath) == 0) return i;
    }
    if (g_IconCacheCount < MAX_CACHE_ICONS) {
        lstrcpyn(g_IconCache[g_IconCacheCount].exePath, exePath, MAX_PATH);
        g_IconCache[g_IconCacheCount].hIconLarge = ExtractIcon(g_hInst, exePath, 0);
        if ((int)g_IconCache[g_IconCacheCount].hIconLarge <= 1) g_IconCache[g_IconCacheCount].hIconLarge = LoadIcon(NULL, IDI_APPLICATION);
        g_IconCacheCount++;
        return g_IconCacheCount - 1;
    }
    return -1;
}

static void RegenerateIconCache(void) {
    FILE *f = fopen(g_szCachePath, "wb");
    if (f) { fprintf(f, "BM_ICACHE_UPDATED_ITEMS:%d", g_IconCacheCount); fclose(f); }
}

/* ──────────────────────────────────────────────────────────────────────────
   Configuration Engine (Local INI)
   ────────────────────────────────────────────────────────────────────────── */
static void LoadConfig(void) {
    int i;
    HDC hdc;

    GetAppFilePath("CALMIRA.INI", g_szIniPath);
    GetAppFilePath("ICACHE.BMP", g_szCachePath);

    g_TbPosition = GetPrivateProfileInt("Taskbar", "Position", POS_BOTTOM, g_szIniPath);
    g_TbHeight = GetPrivateProfileInt("Taskbar", "Height", 30, g_szIniPath);
    g_TbWidthVert = GetPrivateProfileInt("Taskbar", "WidthVert", 72, g_szIniPath);

    if (g_TbHeight < 24) g_TbHeight = 24;
    if (g_TbWidthVert < 48) g_TbWidthVert = 48;

    GetPrivateProfileString("Paths", "SearchExe", "winfile.exe", g_szSearchExe, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("Paths", "HelpExe", "winhelp.exe", g_szHelpExe, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("Paths", "TaskMgrExe", "taskmgr.exe", g_szTaskMgrExe, MAX_PATH, g_szIniPath);

    GetPrivateProfileString("WinX", "Shortcut", "pifedit.exe", g_szWinXShortcut, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("WinX", "Properties", "control.exe", g_szWinXProps, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("WinX", "ControlPanel", "control.exe", g_szWinXCP, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("WinX", "DeviceManager", "control.exe", g_szWinXDevMan, MAX_PATH, g_szIniPath);

    g_QLActiveCount = GetPrivateProfileInt("QuickLaunch", "Enabled", 0, g_szIniPath) ? 
                      GetPrivateProfileInt("QuickLaunch", "Count", QUICK_LAUNCH_COUNT, g_szIniPath) : 0;
    if (g_QLActiveCount > QUICK_LAUNCH_COUNT) g_QLActiveCount = QUICK_LAUNCH_COUNT;

    hdc = GetDC(NULL);
    g_hCacheBitmap = Load16ColorBMP(g_szCachePath, hdc);
    ReleaseDC(NULL, hdc);

    g_LoadedCacheCount = GetPrivateProfileInt("IconCache", "Count", 0, g_szIniPath);
    if (g_hCacheBitmap && g_LoadedCacheCount > 0) {
        for (i = 0; i < g_LoadedCacheCount; i++) {
            char key[16];
            sprintf(key, "Path%d", i);
            GetPrivateProfileString("IconCache", key, "", g_IconCache[i].exePath, MAX_PATH, g_szIniPath);
            g_IconCache[i].hIconLarge = NULL;
        }
        g_IconCacheCount = g_LoadedCacheCount;
    } else {
        g_IconCacheCount = 0;
    }

    for (i = 0; i < g_QLActiveCount; i++) {
        char keyN[16], keyE[16];
        sprintf(keyN, "Name%d", i);
        sprintf(keyE, "Exe%d", i);
        GetPrivateProfileString("QuickLaunch", keyN, "", g_QL[i].name, 16, g_szIniPath);
        GetPrivateProfileString("QuickLaunch", keyE, "", g_QL[i].exe, MAX_PATH, g_szIniPath);
        GetCachedIconIndex(g_QL[i].exe);
    }
    RegenerateIconCache();
}

static void SaveConfig(void) {
    char buf[16];
    sprintf(buf, "%d", g_TbPosition);
    WritePrivateProfileString("Taskbar", "Position", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbHeight);
    WritePrivateProfileString("Taskbar", "Height", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbWidthVert);
    WritePrivateProfileString("Taskbar", "WidthVert", buf, g_szIniPath);
}

static void SaveQuickLaunchItem(int i) {
    char keyN[16], keyE[16];
    sprintf(keyN, "Name%d", i);
    sprintf(keyE, "Exe%d", i);
    WritePrivateProfileString("QuickLaunch", keyN, g_QL[i].name, g_szIniPath);
    WritePrivateProfileString("QuickLaunch", keyE, g_QL[i].exe, g_szIniPath);
}

/* ──────────────────────────────────────────────────────────────────────────
   Start Menu Editor Dialogs
   ────────────────────────────────────────────────────────────────────────── */
LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hParent, hTarget, hParams, hIcon, hMinCheck;
    switch(msg) {
        case WM_CREATE: {
            char target[MAX_PATH] = "", params[MAX_PATH] = "", iconF[MAX_PATH] = "";
            int minimized = 0;

            CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 10, 100, 20, hwnd, NULL, g_hInst, NULL);
            hParent = CreateWindowEx(0, "EDIT", g_ShortcutParentDir, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 10, 210, 22, hwnd, NULL, g_hInst, NULL);
            
            CreateWindow("STATIC", "Target (EXE/COM):", WS_CHILD|WS_VISIBLE, 10, 40, 100, 20, hwnd, NULL, g_hInst, NULL);
            hTarget = CreateWindowEx(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 40, 150, 22, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 270, 40, 50, 22, hwnd, (HMENU)101, g_hInst, NULL);

            CreateWindow("STATIC", "Parameters:", WS_CHILD|WS_VISIBLE, 10, 70, 100, 20, hwnd, NULL, g_hInst, NULL);
            hParams = CreateWindowEx(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 70, 210, 22, hwnd, NULL, g_hInst, NULL);

            CreateWindow("STATIC", "Icon File:", WS_CHILD|WS_VISIBLE, 10, 100, 100, 20, hwnd, NULL, g_hInst, NULL);
            hIcon = CreateWindowEx(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 100, 150, 22, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 270, 100, 50, 22, hwnd, (HMENU)102, g_hInst, NULL);

            hMinCheck = CreateWindow("BUTTON", "Start Minimized", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 110, 130, 150, 20, hwnd, NULL, g_hInst, NULL);

            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 80, 180, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 180, 180, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            
            if (g_EditShortcutPath[0] != '\0') {
                char parentDir[MAX_PATH];
                GetPrivateProfileString("Shortcut", "Target", "", target, MAX_PATH, g_EditShortcutPath);
                GetPrivateProfileString("Shortcut", "Parameters", "", params, MAX_PATH, g_EditShortcutPath);
                GetPrivateProfileString("Shortcut", "IconFile", "", iconF, MAX_PATH, g_EditShortcutPath);
                minimized = GetPrivateProfileInt("Shortcut", "RunMinimized", 0, g_EditShortcutPath);
                
                lstrcpy(parentDir, g_EditShortcutPath);
                {
                    char* p = strrchr(parentDir, '\\');
                    if (p) *p = '\0';
                }
                SetWindowText(hParent, parentDir);
            }

            SetWindowText(hTarget, target);
            SetWindowText(hParams, params);
            SetWindowText(hIcon, iconF);
            if (minimized) SendMessage(hMinCheck, BM_SETCHECK, 1, 0);

            SetFont(hwnd, NULL); SetFocus(hTarget);
            PositionDialogNearStart(hwnd);
            return 0;
        }
            
        case WM_COMMAND:
            if (wp == 101) {
                char path[MAX_PATH];
                if (BrowseFile(hwnd, path, "Programs (*.exe;*.com)\0*.exe;*.com\0All Files (*.*)\0*.*\0")) {
                    SetWindowText(hTarget, path);
                    if (GetWindowTextLength(hIcon) == 0) SetWindowText(hIcon, path);
                }
            } else if (wp == 102) {
                char path[MAX_PATH];
                if (BrowseFile(hwnd, path, "Icons (*.exe;*.ico;*.dll)\0*.exe;*.ico;*.dll\0All Files (*.*)\0*.*\0")) SetWindowText(hIcon, path);
            } else if (wp == IDOK) {
                char parent[MAX_PATH], target[MAX_PATH], params[MAX_PATH], iconF[MAX_PATH], lnkPath[MAX_PATH];
                GetWindowText(hParent, parent, MAX_PATH);
                GetWindowText(hTarget, target, MAX_PATH);
                GetWindowText(hParams, params, MAX_PATH);
                GetWindowText(hIcon, iconF, MAX_PATH);
                
                if (target[0]) {
                    if (g_EditShortcutPath[0] != '\0') {
                        lstrcpy(lnkPath, g_EditShortcutPath);
                    } else {
                        char lnkName[64];
                        char* p = strrchr(target, '\\');
                        lstrcpyn(lnkName, p ? p + 1 : target, 60);
                        p = strrchr(lnkName, '.');
                        if (p) *p = '\0';
                        lstrcat(lnkName, ".lnk");
                        sprintf(lnkPath, "%s\\%s", parent, lnkName);
                    }
                    
                    WritePrivateProfileString("Shortcut", "Target", target, lnkPath);
                    WritePrivateProfileString("Shortcut", "Parameters", params, lnkPath);
                    WritePrivateProfileString("Shortcut", "IconFile", iconF, lnkPath);
                    WritePrivateProfileString("Shortcut", "RunMinimized", SendMessage(hMinCheck, BM_GETCHECK, 0, 0) ? "1" : "0", lnkPath);
                    
                    RegenerateIconCache();
                }
                g_EditShortcutPath[0] = '\0';
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (wp == IDCANCEL) {
                g_EditShortcutPath[0] = '\0';
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_CLOSE: 
            g_EditShortcutPath[0] = '\0';
            EnableWindow(g_hTaskbar, TRUE); 
            DestroyWindow(hwnd); 
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit;
    switch(msg) {
        case WM_CREATE:
            CreateWindow("STATIC", g_PromptLabel, WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            hEdit = CreateWindowEx(0, "EDIT", g_PromptValue, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER, 10, 35, 260, 22, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 50, 70, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 150, 70, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            SetFont(hwnd, NULL); SetFocus(hEdit);
            PositionDialogNearStart(hwnd);
            return 0;
        case WM_COMMAND:
            if (wp == IDOK) {
                GetWindowText(hEdit, g_PromptValue, MAX_PATH);
                if (g_PromptValue[0] != '\0') {
                    char fullPath[MAX_PATH];
                    char baseDir[MAX_PATH];
                    
                    if (g_PromptMode == PROMPT_NEWFOLDER) {
                        if (g_ContextIsDir && g_ContextPath[0] != '\0') {
                            lstrcpy(baseDir, g_ContextPath);
                        } else {
                            lstrcpy(baseDir, "C:\\STARTM");
                        }
                        mkdir(baseDir);
                        sprintf(fullPath, "%s\\%s", baseDir, g_PromptValue);
                        mkdir(fullPath);
                    } else if (g_PromptMode == PROMPT_RENAME) {
                        char oldPath[MAX_PATH], newPath[MAX_PATH], *p;
                        lstrcpy(oldPath, g_ContextPath);
                        lstrcpy(newPath, g_ContextPath);
                        p = strrchr(newPath, '\\');
                        if (p) {
                            *(p + 1) = '\0';
                            lstrcat(newPath, g_PromptValue);
                            rename(oldPath, newPath);
                        }
                    }
                    RegenerateIconCache();
                }
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (wp == IDCANCEL) {
                g_PromptValue[0] = '\0';
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ──────────────────────────────────────────────────────────────────────────
   Run Dialog Procedure (With ComboBox & Pipe History)
   ────────────────────────────────────────────────────────────────────────── */
LRESULT CALLBACK RunDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hCombo;
    switch(msg) {
        case WM_CREATE: {
            char hist[512];
            char* token;
            CreateWindow("STATIC", "Type the name of a program to open:", WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            hCombo = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL|WS_BORDER, 10, 35, 260, 120, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 10, 70, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 100, 70, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 190, 70, 80, 24, hwnd, (HMENU)101, g_hInst, NULL);
            
            GetPrivateProfileString("Run", "History", "", hist, sizeof(hist), g_szIniPath);
            token = strtok(hist, "¦");
            while(token) { SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)token); token = strtok(NULL, "¦"); }
            SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            
            SetFont(hwnd, NULL); SetFocus(hCombo);
            PositionDialogNearStart(hwnd);
            return 0;
        }
        case WM_COMMAND:
            if (wp == IDOK) {
                char cmd[128], newHist[512] = "", oldHist[512];
                int i, count;
                GetWindowText(hCombo, cmd, sizeof(cmd));
                if (cmd[0]) {
                    WinExec(cmd, SW_SHOWNORMAL);
                    lstrcpy(newHist, cmd);
                    count = SendMessage(hCombo, CB_GETCOUNT, 0, 0);
                    for (i=0; i<count && i<9; i++) {
                        SendMessage(hCombo, CB_GETLBTEXT, i, (LPARAM)oldHist);
                        if (lstrcmpi(oldHist, cmd) != 0) { lstrcat(newHist, "¦"); lstrcat(newHist, oldHist); }
                    }
                    WritePrivateProfileString("Run", "History", newHist, g_szIniPath);
                }
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (wp == IDCANCEL) {
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (wp == 101) {
                char path[MAX_PATH];
                if (BrowseFile(hwnd, path, "Programs (*.exe;*.com)\0*.exe;*.com\0All Files (*.*)\0*.*\0")) SetWindowText(hCombo, path);
            }
            return 0;
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ──────────────────────────────────────────────────────────────────────────
   Start Menu Right-Click Tracker Hook
   ────────────────────────────────────────────────────────────────────────── */
LRESULT CALLBACK MsgFilterProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == MSGF_MENU) {
        MSG FAR* pMsg = (MSG FAR*)lParam;
        if (pMsg->message == WM_RBUTTONUP) {
            if (g_ContextPath[0] != '\0') {
                PostMessage(g_hTaskbar, WM_USER_CONTEXTMENU, 0, MAKELPARAM(pMsg->pt.x, pMsg->pt.y));
                SendMessage(pMsg->hwnd, WM_CANCELMODE, 0, 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hMsgHook, code, wParam, lParam);
}

/* ──────────────────────────────────────────────────────────────────────────
   Owner-Drawn Win95-Style Start Menu Engine
   ────────────────────────────────────────────────────────────────────────── */
static void ClearODIcons(void) {
    int i;
    for (i = 0; i < g_ODCount; i++) {
        if (g_ODItems[i].bDestroyIcon && g_ODItems[i].hIcon) DestroyIcon(g_ODItems[i].hIcon);
    }
    g_ODCount = 0;
}

static ODMenuItem* AddODItem(const char* text, const char* exePath, BOOL isRoot, BOOL isSeparator) {
    ODMenuItem* item;
    int cIdx = -1;
    if (g_ODCount >= MAX_OD_ITEMS) return NULL;
    item = &g_ODItems[g_ODCount++];
    memset(item, 0, sizeof(ODMenuItem));
    if (text) lstrcpyn(item->text, text, 63);
    item->isRoot = isRoot; item->isSeparator = isSeparator; item->cacheIndex = -1;
    if (!isSeparator) {
        if (exePath && lstrlen(exePath) > 0) {
            cIdx = GetCachedIconIndex(exePath);
            if (cIdx >= 0) { item->cacheIndex = cIdx; if (g_IconCache[cIdx].hIconLarge) item->hIcon = g_IconCache[cIdx].hIconLarge; }
            else item->hIcon = LoadIcon(NULL, IDI_APPLICATION);
        } else item->hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    return item;
}

static void BuildMenuFromDir(HMENU hMenu, const char* dirPath) {
    struct find_t fd;
    char searchPath[MAX_PATH];
    char childPath[MAX_PATH];
    char display[64];

    sprintf(searchPath, "%s\\*.*", dirPath);
    if (_dos_findfirst(searchPath, _A_NORMAL | _A_RDONLY | _A_SUBDIR, &fd) == 0) {
        do {
            if (fd.name[0] == '.') continue;
            sprintf(childPath, "%s\\%s", dirPath, fd.name);

            if (fd.attrib & _A_SUBDIR) {
                HMENU hSub = CreatePopupMenu();
                ODMenuItem* pItem = AddODItem(fd.name, NULL, FALSE, FALSE);
                
                if (g_MenuDirCount < MAX_START_ITEMS) {
                    if (g_MenuDirs) {
                        g_MenuDirs[g_MenuDirCount].hMenu = hSub;
                        lstrcpy(g_MenuDirs[g_MenuDirCount].path, childPath);
                        g_MenuDirCount++;
                    }
                }

                BuildMenuFromDir(hSub, childPath);
                if (pItem) AppendMenu(hMenu, MF_OWNERDRAW | MF_POPUP, (UINT)hSub, (LPSTR)pItem);
            } else {
                if (g_StartMenuCounter < MAX_START_ITEMS) {
                    char target[MAX_PATH], iconF[MAX_PATH];
                    char* ext = strrchr(fd.name, '.');
                    ODMenuItem* pItem;
                    
                    if (ext) { lstrcpyn(display, fd.name, ext - fd.name + 1); display[ext - fd.name] = '\0'; } 
                    else lstrcpy(display, fd.name);

                    if (ext && lstrcmpi(ext, ".lnk") == 0) {
                        GetPrivateProfileString("Shortcut", "Target", "", target, sizeof(target), childPath);
                        GetPrivateProfileString("Shortcut", "IconFile", target, iconF, sizeof(iconF), childPath);
                    } else { lstrcpy(target, childPath); lstrcpy(iconF, childPath); }

                    if (lstrlen(target) > 0) {
                        int id = IDM_START_BASE + g_StartMenuCounter;
                        if (g_MenuItemLnkPaths) lstrcpy(g_MenuItemLnkPaths[g_StartMenuCounter], childPath);
                        pItem = AddODItem(display, iconF, FALSE, FALSE);
                        if (pItem) AppendMenu(hMenu, MF_OWNERDRAW | MF_STRING, id, (LPSTR)pItem);
                        g_StartMenuCounter++;
                    }
                }
            }
        } while (_dos_findnext(&fd) == 0);
    }
}

static void ShowStartMenu(HWND hBtn) {
    HMENU hMenu = CreatePopupMenu();
    HMENU hPrograms = CreatePopupMenu();
    POINT pt;
    RECT rc;
    int curY = 0;
    ODMenuItem *pProg, *pSearch, *pHelp, *pRun, *pSep, *pExit;

    ClearODIcons();
    g_StartMenuCounter = 0; g_MenuDirCount = 0;
    mkdir("C:\\STARTM");
    
    if (g_MenuDirs) {
        g_MenuDirs[g_MenuDirCount].hMenu = hPrograms;
        lstrcpy(g_MenuDirs[g_MenuDirCount].path, "C:\\STARTM");
        g_MenuDirCount++;
    }

    BuildMenuFromDir(hPrograms, "C:\\STARTM");

    pProg   = AddODItem("Programs", NULL, TRUE, FALSE);
    pSearch = AddODItem("Search", g_szSearchExe, TRUE, FALSE);
    pHelp   = AddODItem("Help", g_szHelpExe, TRUE, FALSE);
    pRun    = AddODItem("Run...", "progman.exe", TRUE, FALSE);
    pSep    = AddODItem("", NULL, TRUE, TRUE);
    pExit   = AddODItem("Shut Down...", NULL, TRUE, FALSE);

    if (pProg)   { pProg->yOffset = curY;   curY += 36; AppendMenu(hMenu, MF_OWNERDRAW | MF_POPUP, (UINT)hPrograms, (LPSTR)pProg); }
    if (pSearch) { pSearch->yOffset = curY; curY += 36; AppendMenu(hMenu, MF_OWNERDRAW, IDM_SM_SEARCH, (LPSTR)pSearch); }
    if (pHelp)   { pHelp->yOffset = curY;   curY += 36; AppendMenu(hMenu, MF_OWNERDRAW, IDM_SM_HELP, (LPSTR)pHelp); }
    if (pRun)    { pRun->yOffset = curY;    curY += 36; AppendMenu(hMenu, MF_OWNERDRAW, IDM_SM_RUN, (LPSTR)pRun); }
    if (pSep)    { pSep->yOffset = curY;    curY += 8;  AppendMenu(hMenu, MF_OWNERDRAW, 0, (LPSTR)pSep); }
    if (pExit)   { pExit->yOffset = curY;   curY += 36; AppendMenu(hMenu, MF_OWNERDRAW, IDM_SM_EXIT, (LPSTR)pExit); }

    if (pProg) pProg->totalHeight = curY;
    if (pSearch) pSearch->totalHeight = curY;
    if (pHelp) pHelp->totalHeight = curY;
    if (pRun) pRun->totalHeight = curY;
    if (pSep) pSep->totalHeight = curY;
    if (pExit) pExit->totalHeight = curY;

    if (g_IconCacheCount > g_LoadedCacheCount) {
        SaveIconCacheTo16ColorBMP();
        {
            char buf[16]; int i;
            sprintf(buf, "%d", g_IconCacheCount);
            WritePrivateProfileString("IconCache", "Count", buf, g_szIniPath);
            for (i = g_LoadedCacheCount; i < g_IconCacheCount; i++) {
                sprintf(buf, "Path%d", i);
                WritePrivateProfileString("IconCache", buf, g_IconCache[i].exePath, g_szIniPath);
            }
        }
        g_LoadedCacheCount = g_IconCacheCount;
    }

    GetWindowRect(hBtn, &rc);
    if (g_TbPosition == POS_BOTTOM) { pt.x = rc.left; pt.y = rc.top; } 
    else if (g_TbPosition == POS_TOP) { pt.x = rc.left; pt.y = rc.bottom; } 
    else if (g_TbPosition == POS_LEFT) { pt.x = rc.right; pt.y = rc.top; } 
    else { pt.x = rc.left; pt.y = rc.top; }

    lstrcpy(g_ContextPath, "C:\\STARTM");
    g_ContextIsDir = TRUE;

    g_lpfnMsgFilter = MakeProcInstance((FARPROC)MsgFilterProc, g_hInst);
    g_hMsgHook = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)g_lpfnMsgFilter, g_hInst, GetCurrentTask());

    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL);

    UnhookWindowsHookEx(g_hMsgHook);
    FreeProcInstance(g_lpfnMsgFilter);
    DestroyMenu(hPrograms);
    DestroyMenu(hMenu);
}

/* ──────────────────────────────────────────────────────────────────────────
   Subclass Procedures
   ────────────────────────────────────────────────────────────────────────── */
static void AddTrayIcon(HICON hIcon, const char* tooltip) {
    if (g_TrayIconCount < MAX_TRAY_ICONS) {
        g_TrayIcons[g_TrayIconCount].hIcon = hIcon;
        lstrcpyn(g_TrayIcons[g_TrayIconCount].tooltip, tooltip, 31);
        g_TrayIconCount++;
        if (g_hTrayArea) InvalidateRect(g_hTrayArea, NULL, TRUE);
    }
}

LRESULT CALLBACK TrayAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        int i, x = 2, y = 2; RECT rc; HPEN hShadow, hHighlight, hOldPen; HBRUSH hBrush;

        hBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &ps.rcPaint, hBrush); DeleteObject(hBrush);

        GetClientRect(hwnd, &rc);
        hShadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
        hHighlight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
        
        hOldPen = SelectObject(hdc, hShadow);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.left, rc.top); LineTo(hdc, rc.right - 1, rc.top);
        SelectObject(hdc, hHighlight);
        LineTo(hdc, rc.right - 1, rc.bottom - 1); LineTo(hdc, rc.left, rc.bottom - 1);
        SelectObject(hdc, hOldPen); DeleteObject(hShadow); DeleteObject(hHighlight);

        if (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP) {
            for (i = 0; i < g_TrayIconCount; i++) { DrawIcon(hdc, x, y - 4, g_TrayIcons[i].hIcon); x += 24; }
        } else {
            x = 4;
            for (i = 0; i < g_TrayIconCount; i++) { DrawIcon(hdc, x, y, g_TrayIcons[i].hIcon); y += 24; }
        }
        EndPaint(hwnd, &ps); return 0;
    }
    return CallWindowProc((FARPROC)OldTrayAreaProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK StartBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HBRUSH hBlue, hRed, hGreen, hYellow, hOldBrush, hGray;
        HPEN hBlack, hOldPen, hShadow, hHighlight;
        BOOL isPushed;
        
        GetClientRect(hwnd, &rc);
        isPushed = (SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
        
        hGray = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        FillRect(hdc, &rc, hGray);
        DeleteObject(hGray);
        
        if (isPushed) {
            hShadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
            hOldPen = SelectObject(hdc, hShadow);
            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
            LineTo(hdc, rc.left, rc.top);
            LineTo(hdc, rc.right - 1, rc.top);
            SelectObject(hdc, hOldPen);
            DeleteObject(hShadow);
            OffsetRect(&rc, 1, 1);
        } else {
            hShadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
            hHighlight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
            
            hOldPen = SelectObject(hdc, hHighlight);
            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
            LineTo(hdc, rc.left, rc.top);
            LineTo(hdc, rc.right - 1, rc.top);
            
            SelectObject(hdc, hShadow);
            LineTo(hdc, rc.right - 1, rc.bottom - 1);
            LineTo(hdc, rc.left, rc.bottom - 1);
            
            SelectObject(hdc, hOldPen);
            DeleteObject(hShadow);
            DeleteObject(hHighlight);
        }
        
        {
            int lx = 6 + (isPushed ? 1 : 0);
            int ly = ((rc.bottom - rc.top - 16) / 2) + (isPushed ? 1 : 0);
            int half = 8;
            
            /* Hide the GDI flag when pushed */
            if (!isPushed) {
                hBlue = CreateSolidBrush(RGB(0, 0, 255));
                hRed = CreateSolidBrush(RGB(255, 0, 0));
                hGreen = CreateSolidBrush(RGB(0, 128, 0));
                hYellow = CreateSolidBrush(RGB(255, 255, 0));
                hBlack = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
                
                hOldPen = SelectObject(hdc, hBlack);
                
                hOldBrush = SelectObject(hdc, hBlue);
                Rectangle(hdc, lx, ly, lx + half + 1, ly + half + 1);
                
                SelectObject(hdc, hRed);
                Rectangle(hdc, lx + half, ly, lx + (half*2) + 1, ly + half + 1);
                
                SelectObject(hdc, hGreen);
                Rectangle(hdc, lx, ly + half, lx + half + 1, ly + (half*2) + 1);
                
                SelectObject(hdc, hYellow);
                Rectangle(hdc, lx + half, ly + half, lx + (half*2) + 1, ly + (half*2) + 1);
                
                SelectObject(hdc, hOldBrush);
                SelectObject(hdc, hOldPen);
                
                DeleteObject(hBlue);
                DeleteObject(hRed);
                DeleteObject(hGreen);
                DeleteObject(hYellow);
                DeleteObject(hBlack);
            }
            
            {
                HFONT hOldFont = SelectObject(hdc, g_hFontMenu);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
                TextOut(hdc, lx + 22, ly + 1, "Start", 5);
                SelectObject(hdc, hOldFont);
            }
        }
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        HMENU hMenu = CreatePopupMenu(); POINT pt;
        pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); ClientToScreen(hwnd, &pt);
        AppendMenu(hMenu, MF_STRING, IDM_WINX_SHORTCUT, "Create Shortcut");
        AppendMenu(hMenu, MF_STRING, IDM_WINX_NEWFOLDER, "New Folder");
        AppendMenu(hMenu, MF_STRING, IDM_WINX_CP, "Control Panel");
        AppendMenu(hMenu, MF_STRING, IDM_WINX_DEVMAN, "Device Manager");
        TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL);
        DestroyMenu(hMenu); return 0;
    }
    return CallWindowProc((FARPROC)OldStartBtnProc, hwnd, msg, wParam, lParam);
}


LRESULT CALLBACK ClockProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_RBUTTONUP) {
        HMENU hMenu = CreatePopupMenu(); POINT pt;
        pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); ClientToScreen(hwnd, &pt);
        AppendMenu(hMenu, MF_STRING, IDM_CLOCK_ADJUST, "Adjust Date/Time");
        TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL);
        DestroyMenu(hMenu); return 0;
    }
    return CallWindowProc((FARPROC)OldClockProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK TaskBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_RBUTTONUP) {
        int ti = GetWindowWord(hwnd, GWW_ID) - ID_TASK_BASE;
        if (ti >= 0 && ti < g_TaskCount) {
            HMENU hMenu = CreatePopupMenu(); POINT pt; g_ContextTargetWnd = g_Tasks[ti].hWnd;
            AppendMenu(hMenu, MF_STRING, IDM_TASK_MINIMIZE, "Mi&nimize");
            AppendMenu(hMenu, MF_STRING, IDM_TASK_MAXIMIZE, "Ma&ximize");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, IDM_TASK_CLOSE, "&Close");
            pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); ClientToScreen(hwnd, &pt);
            TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        int ti = GetWindowWord(hwnd, GWW_ID) - ID_TASK_BASE;
        if (ti >= 0 && ti < g_TaskCount) {
            HWND win = g_Tasks[ti].hWnd;
            if (IsIconic(win)) ShowWindow(win, SW_RESTORE);
            else if (win == GetActiveWindow()) ShowWindow(win, SW_MINIMIZE);
            else { BringWindowToTop(win); SetActiveWindow(win); }
        }
        return 0;
    }
    return CallWindowProc((FARPROC)OldTaskBtnProc, hwnd, msg, wParam, lParam);
}

/* ──────────────────────────────────────────────────────────────────────────
   Window Management & Multi-Row Task Layout Engine
   ────────────────────────────────────────────────────────────────────────── */
BOOL CALLBACK TaskbarEnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        char cls[64];
        GetClassName(hwnd, cls, sizeof(cls));
        if (lstrcmp(cls, "CalmiraTaskbarClass") != 0 && lstrcmp(cls, "Progman") != 0 && lstrcmp(cls, "RunDlgClass") != 0 && lstrcmp(cls, "ShortcutDlgClass") != 0 && lstrcmp(cls, "PromptDlgClass") != 0) {
            if (g_TaskCount < MAX_TASKS) {
                char title[128];
                GetWindowText(hwnd, title, sizeof(title));
                if (lstrlen(title) > 0) {
                    g_Tasks[g_TaskCount].hWnd = hwnd;
                    lstrcpy(g_Tasks[g_TaskCount].title, title);
                    g_Tasks[g_TaskCount].hBtn = NULL;
                    g_TaskCount++;
                }
            }
        }
    }
    return TRUE;
}

static void RefreshTasks(void) {
    int i;
    RECT rc;
    int isHorz = (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP);

    for (i = 0; i < g_TaskCount; i++) {
        if (g_Tasks[i].hBtn) DestroyWindow(g_Tasks[i].hBtn);
    }
    g_TaskCount = 0;
    EnumWindows((WNDENUMPROC)g_lpfnEnumWindowsProc, 0);
    GetClientRect(g_hTaskList, &rc);
    
    if (g_TaskCount > 0) {
        int containerW = rc.right - rc.left;
        int containerH = rc.bottom - rc.top;

        if (isHorz && g_TbHeight > 60 && containerH >= 48) {
            int rowHeight = 24;
            int numRows = containerH / rowHeight;
            if (numRows < 1) numRows = 1;
            int cols = (g_TaskCount + numRows - 1) / numRows;
            if (cols < 1) cols = 1;
            int btnWidth = containerW / cols;
            if (btnWidth > 160) btnWidth = 160;
            if (btnWidth < 40) btnWidth = 40;

            for (i = 0; i < g_TaskCount; i++) {
                int r = i / cols;
                int c = i % cols;
                char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 28);
                
                g_Tasks[i].hBtn = CreateWindow("BUTTON", shortTitle, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                               c * btnWidth, r * rowHeight, btnWidth - 2, rowHeight - 2, 
                                               g_hTaskList, (HMENU)(ID_TASK_BASE + i), g_hInst, NULL);
                SetFont(g_Tasks[i].hBtn, NULL);
                if (!OldTaskBtnProc) OldTaskBtnProc = (FARPROC)SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
                else SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
            }
        } else if (isHorz) {
            int btnWidth = containerW / g_TaskCount;
            if (btnWidth > 160) btnWidth = 160;
            for (i = 0; i < g_TaskCount; i++) {
                char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 28);
                g_Tasks[i].hBtn = CreateWindow("BUTTON", shortTitle, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, i * btnWidth, 0, btnWidth - 2, containerH - 2, g_hTaskList, (HMENU)(ID_TASK_BASE + i), g_hInst, NULL);
                SetFont(g_Tasks[i].hBtn, NULL);
                if (!OldTaskBtnProc) OldTaskBtnProc = (FARPROC)SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
                else SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
            }
        } else {
            for (i = 0; i < g_TaskCount; i++) {
                char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 10);
                g_Tasks[i].hBtn = CreateWindow("BUTTON", shortTitle, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, i * 26, containerW, 24, g_hTaskList, (HMENU)(ID_TASK_BASE + i), g_hInst, NULL);
                SetFont(g_Tasks[i].hBtn, NULL);
                if (!OldTaskBtnProc) OldTaskBtnProc = (FARPROC)SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
                else SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
            }
        }
    }
}

static void ApplyLayout(void) {
    int cx = GetSystemMetrics(SM_CXSCREEN); int cy = GetSystemMetrics(SM_CYSCREEN);
    int i; int trayW = (g_TrayIconCount > 0 ? (g_TrayIconCount * 24) + 4 : 0);
    int qlCount = g_QLActiveCount;
    int qlW = qlCount * 40;

    switch (g_TbPosition) {
        case POS_BOTTOM: MoveWindow(g_hTaskbar, 0, cy - g_TbHeight, cx, g_TbHeight, TRUE); break;
        case POS_TOP:    MoveWindow(g_hTaskbar, 0, 0, cx, g_TbHeight, TRUE); break;
        case POS_LEFT:   MoveWindow(g_hTaskbar, 0, 0, g_TbWidthVert, cy, TRUE); break;
        case POS_RIGHT:  MoveWindow(g_hTaskbar, cx - g_TbWidthVert, 0, g_TbWidthVert, cy, TRUE); break;
    }

    if (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP) {
        int tlX = 6 + START_BTN_WIDTH + 6 + qlW + (qlCount > 0 ? 6 : 0);
        int tlW = cx - CLOCK_WIDTH - trayW - tlX - 8;
        int yOff = (g_TbHeight - 22) / 2; 
        
        MoveWindow(g_hStartBtn, 4, yOff, START_BTN_WIDTH, 22, TRUE);
        for (i = 0; i < QUICK_LAUNCH_COUNT; i++) {
            if (i < qlCount) {
                MoveWindow(g_hQuickLaunch[i], 4 + START_BTN_WIDTH + 6 + i * 40, yOff, 38, 22, TRUE);
                ShowWindow(g_hQuickLaunch[i], SW_SHOW);
            } else {
                ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            }
        }
        
        if (tlW < 50) tlW = 50;
        MoveWindow(g_hTaskList, tlX, 3, tlW, g_TbHeight - 6, TRUE);
        if (trayW > 0) MoveWindow(g_hTrayArea, cx - CLOCK_WIDTH - trayW - 6, yOff, trayW, 22, TRUE); else MoveWindow(g_hTrayArea, 0, 0, 0, 0, FALSE);
        MoveWindow(g_hClock, cx - CLOCK_WIDTH - 4, yOff, CLOCK_WIDTH, 22, TRUE);
    } else {
        int y = 4; int tlH = cy - CLOCK_HEIGHT - (trayW > 0 ? (g_TrayIconCount * 24 + 4) : 0) - y - 10;
        int xOff = (g_TbWidthVert - 64) / 2; if (xOff < 4) xOff = 4;
        
        MoveWindow(g_hStartBtn, xOff, y, g_TbWidthVert - (xOff*2), 22, TRUE); y += 26;
        for (i = 0; i < QUICK_LAUNCH_COUNT; i++) {
            if (i < qlCount) {
                MoveWindow(g_hQuickLaunch[i], xOff, y, g_TbWidthVert - (xOff*2), 22, TRUE);
                ShowWindow(g_hQuickLaunch[i], SW_SHOW);
                y += 24;
            } else {
                ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            }
        }
        y += 4;
        MoveWindow(g_hTaskList, 4, y, g_TbWidthVert - 8, tlH, TRUE);
        if (trayW > 0) { int trayH = g_TrayIconCount * 24 + 4; MoveWindow(g_hTrayArea, 4, cy - CLOCK_HEIGHT - trayH - 6, g_TbWidthVert - 8, trayH, TRUE); } else MoveWindow(g_hTrayArea, 0, 0, 0, 0, FALSE);
        MoveWindow(g_hClock, 4, cy - CLOCK_HEIGHT - 4, g_TbWidthVert - 8, CLOCK_HEIGHT, TRUE);
    }
    RefreshTasks();
    if (g_hTrayArea) InvalidateRect(g_hTrayArea, NULL, TRUE);
}

static void SnapToEdge(POINT pt) {
    int cx = GetSystemMetrics(SM_CXSCREEN); int cy = GetSystemMetrics(SM_CYSCREEN);
    int edge = POS_BOTTOM; int dBot = cy - pt.y; int dTop = pt.y; int dLeft = pt.x; int dRight = cx - pt.x;
    int minD = dBot;
    
    if (dTop < minD) { minD = dTop; edge = POS_TOP; }
    if (dLeft < minD) { minD = dLeft; edge = POS_LEFT; }
    if (dRight < minD) { minD = dRight; edge = POS_RIGHT; }
    if (edge != g_TbPosition) { g_TbPosition = edge; ApplyLayout(); }
}

BOOL CALLBACK MinimizeEnumProc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd)) {
        char cls[64];
        GetClassName(hwnd, cls, sizeof(cls));
        if (lstrcmp(cls, "CalmiraTaskbarClass") != 0 && lstrcmp(cls, "Progman") != 0 && lstrcmp(cls, "RunDlgClass") != 0 && lstrcmp(cls, "ShortcutDlgClass") != 0 && lstrcmp(cls, "PromptDlgClass") != 0) {
            if (!IsIconic(hwnd) && IsWindowEnabled(hwnd)) {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
        }
    }
    return TRUE;
}

static void DoShowDesktop(void) {
    FARPROC lpfn = MakeProcInstance((FARPROC)MinimizeEnumProc, g_hInst);
    EnumWindows((WNDENUMPROC)lpfn, 0);
    FreeProcInstance(lpfn);
}


/* ──────────────────────────────────────────────────────────────────────────
   Main Taskbar Window Procedure
   ────────────────────────────────────────────────────────────────────────── */
LRESULT CALLBACK __export TaskbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int i;
            g_hTaskbar = hwnd;
            InitFonts();
            
            g_hMemODItems = GlobalAlloc(GPTR, MAX_OD_ITEMS * sizeof(ODMenuItem));
            g_ODItems = (ODMenuItem FAR*)GlobalLock(g_hMemODItems);
            
            g_hMemLnkPaths = GlobalAlloc(GPTR, MAX_START_ITEMS * MAX_PATH);
            g_MenuItemLnkPaths = (char (*)[MAX_PATH])GlobalLock(g_hMemLnkPaths);
            
            g_hMemMenuDirs = GlobalAlloc(GPTR, MAX_START_ITEMS * sizeof(struct MenuDirMap));
            g_MenuDirs = (struct MenuDirMap FAR*)GlobalLock(g_hMemMenuDirs);
            
            g_hMemIconCache = GlobalAlloc(GPTR, MAX_CACHE_ICONS * sizeof(IconCacheEntry));
            g_IconCache = (IconCacheEntry FAR*)GlobalLock(g_hMemIconCache);

            LoadConfig();

            g_hStartBtn = CreateWindow("BUTTON", "Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_START_BUTTON, g_hInst, NULL);
            SetFont(g_hStartBtn, g_hFontMenu);
            g_lpfnStartBtnProc = MakeProcInstance((FARPROC)StartBtnProc, g_hInst);
            OldStartBtnProc = (FARPROC)SetWindowLong(g_hStartBtn, GWL_WNDPROC, (LONG)g_lpfnStartBtnProc);
            
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) {
                g_hQuickLaunch[i] = CreateWindow("BUTTON", g_QL[i].name, WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)(ID_QUICK_BASE + i), g_hInst, NULL);
                SetFont(g_hQuickLaunch[i], NULL);
            }
            
            g_hTaskList = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)0, g_hInst, NULL);
            g_hClock = CreateWindowEx(0, "STATIC", "00:00", WS_CHILD | WS_VISIBLE | SS_CENTER | WS_BORDER, 0, 0, 0, 0, hwnd, (HMENU)ID_CLOCK, g_hInst, NULL);
            SetFont(g_hClock, NULL);
            g_lpfnClockProc = MakeProcInstance((FARPROC)ClockProc, g_hInst);
            OldClockProc = (FARPROC)SetWindowLong(g_hClock, GWL_WNDPROC, (LONG)g_lpfnClockProc);

            g_hTrayArea = CreateWindowEx(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_TRAY_AREA, g_hInst, NULL);
            OldTrayAreaProc = (FARPROC)SetWindowLong(g_hTrayArea, GWL_WNDPROC, (LONG)g_lpfnTrayAreaProc);

            DragAcceptFiles(hwnd, TRUE);

            SetTimer(hwnd, TIMER_CLOCK, 1000, NULL);
            SetTimer(hwnd, TIMER_REFRESH, 2000, NULL);
            SetTimer(hwnd, TIMER_HOTKEY, 150, NULL); 
            
            AddTrayIcon(LoadIcon(NULL, IDI_ASTERISK), "Network Linked");

            ApplyLayout();
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            POINT pt;
            char file[MAX_PATH];
            RECT rcStart;

            DragQueryPoint(hDrop, &pt);
            DragQueryFile(hDrop, 0, file, sizeof(file)); 

            GetWindowRect(g_hStartBtn, &rcStart);
            ScreenToClient(hwnd, (LPPOINT)&rcStart.left);
            ScreenToClient(hwnd, (LPPOINT)&rcStart.right);

            if (PtInRect(&rcStart, pt)) {
                char lnkPath[MAX_PATH];
                char fileName[MAX_PATH];
                char* ptr = strrchr(file, '\\');
                
                mkdir("C:\\STARTM");

                if (ptr) lstrcpy(fileName, ptr + 1);
                else lstrcpy(fileName, file);

                ptr = strrchr(fileName, '.');
                if (ptr) *ptr = '\0';
                lstrcat(fileName, ".lnk");

                sprintf(lnkPath, "C:\\STARTM\\%s", fileName);
                WritePrivateProfileString("Shortcut", "Target", file, lnkPath);
                RegenerateIconCache();
                MessageBox(hwnd, "Shortcut added to Start Menu!", "Taskbar", MB_OK | MB_ICONINFORMATION);
            }
            else {
                int i;
                for (i = 0; i < g_QLActiveCount; i++) {
                    if (g_hQuickLaunch[i] && IsWindowVisible(g_hQuickLaunch[i])) {
                        RECT rc;
                        GetWindowRect(g_hQuickLaunch[i], &rc);
                        ScreenToClient(hwnd, (LPPOINT)&rc.left);
                        ScreenToClient(hwnd, (LPPOINT)&rc.right);
                        if (PtInRect(&rc, pt)) {
                            char* ptr = strrchr(file, '\\');
                            if (ptr) {
                                lstrcpyn(g_QL[i].name, ptr + 1, 15);
                                ptr = strrchr(g_QL[i].name, '.');
                                if (ptr) *ptr = '\0';
                            }
                            lstrcpy(g_QL[i].exe, file);
                            SetWindowText(g_hQuickLaunch[i], g_QL[i].name);
                            SaveQuickLaunchItem(i);
                            RegenerateIconCache();
                            break;
                        }
                    }
                }
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_MENUSELECT: {
            UINT idItem = wParam; 
            UINT flags = LOWORD(lParam); 
            HMENU hMenu = (HMENU)HIWORD(lParam); 
            int i;
            
            if (!(flags & 0xFFFF) && hMenu == NULL) {
                /* Keep g_ContextPath active for right clicks */
            } else if (!(flags & MF_SEPARATOR) && !(flags & MF_SYSMENU)) {
                if (flags & MF_POPUP) {
                    HMENU hSub = GetSubMenu(hMenu, idItem);
                    if (!hSub) hSub = (HMENU)idItem;
                    for(i = 0; i < g_MenuDirCount; i++) {
                        if (g_MenuDirs && (g_MenuDirs[i].hMenu == hSub || g_MenuDirs[i].hMenu == (HMENU)idItem)) {
                            lstrcpy(g_ContextPath, g_MenuDirs[i].path);
                            g_ContextIsDir = TRUE;
                            break;
                        }
                    }
                } else if (idItem >= IDM_START_BASE && idItem < IDM_START_BASE + MAX_START_ITEMS) {
                    if (g_MenuItemLnkPaths) {
                        lstrcpy(g_ContextPath, g_MenuItemLnkPaths[idItem - IDM_START_BASE]);
                        g_ContextIsDir = FALSE;
                    }
                }
            }
            return 0;
        }

        case WM_USER_CONTEXTMENU: {
            HMENU hMenu = CreatePopupMenu();
            if (g_ContextIsDir) {
                UINT state = (lstrcmpi(g_ContextPath, "C:\\STARTM") == 0) ? MF_GRAYED : 0;
                AppendMenu(hMenu, MF_STRING, IDM_CTX_PROPS, "Properties");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, IDM_CTX_NEWFOLDER, "New Folder...");
                AppendMenu(hMenu, MF_STRING, IDM_CTX_NEWSHORTCUT, "New Shortcut...");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING | state, IDM_CTX_RENAME, "Rename...");
                AppendMenu(hMenu, MF_STRING | state, IDM_CTX_DELETE, "Delete");
            } else {
                lstrcpy(g_EditShortcutPath, g_ContextPath);
                CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Edit Shortcut", 340, 260);
                EnableWindow(hwnd, FALSE);
                DestroyMenu(hMenu);
                return 0;
            }
            TrackPopupMenu(hMenu, 0, LOWORD(lParam), HIWORD(lParam), 0, hwnd, NULL);
            DestroyMenu(hMenu);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
            if (lpdis->CtlType == ODT_MENU) {
                ODMenuItem FAR* item = (ODMenuItem FAR*)lpdis->itemData;
                HDC hdc = lpdis->hDC; RECT rc = lpdis->rcItem; HBRUSH hBrush; int textX = rc.left + 4;
                if (!item) return TRUE;

                if (lpdis->itemState & ODS_SELECTED) {
                    hBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT)); FillRect(hdc, &rc, hBrush); SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    hBrush = CreateSolidBrush(GetSysColor(COLOR_MENU)); FillRect(hdc, &rc, hBrush); SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
                }
                DeleteObject(hBrush);

                if (item->isRoot) {
                    RECT rcB = rc; HFONT hOldF; rcB.right = rcB.left + 32;
                    hBrush = CreateSolidBrush(RGB(0, 0, 128)); FillRect(hdc, &rcB, hBrush); DeleteObject(hBrush);
                    textX += 32;
                    hOldF = SelectObject(hdc, g_hFontSidebar); SetTextColor(hdc, RGB(255, 255, 255)); SetBkMode(hdc, TRANSPARENT);
                    TextOut(hdc, rc.left + 4, rc.top - item->yOffset + item->totalHeight - 10, "Windows 3.1", 11);
                    SelectObject(hdc, hOldF);
                }

                if (item->cacheIndex >= 0 && g_hCacheBitmap) {
                    HDC hdcMem = CreateCompatibleDC(hdc); HBITMAP hOld = SelectObject(hdcMem, g_hCacheBitmap);
                    BitBlt(hdc, textX, rc.top + 2, 32, 32, hdcMem, item->cacheIndex * 32, 0, SRCCOPY);
                    SelectObject(hdcMem, hOld); DeleteDC(hdcMem);
                } else if (item->hIcon) DrawIcon(hdc, textX, rc.top + 2, item->hIcon);

                textX += 36; SetBkMode(hdc, TRANSPARENT);
                if (lpdis->itemState & ODS_SELECTED) SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT)); else SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
                { HFONT hOldMenuFont = SelectObject(hdc, g_hFontMenu); TextOut(hdc, textX, rc.top + 10, item->text, lstrlen(item->text)); SelectObject(hdc, hOldMenuFont); }
            }
            return TRUE;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lParam;
            if (lpmis->CtlType == ODT_MENU) {
                ODMenuItem FAR* item = (ODMenuItem FAR*)lpmis->itemData;
                if (item) {
                    if (item->isSeparator) { lpmis->itemWidth = 100; lpmis->itemHeight = 8; } 
                    else { lpmis->itemWidth = 120 + (item->isRoot ? 32 : 0); lpmis->itemHeight = 36; }
                }
            }
            return TRUE;
        }

        case WM_RBUTTONUP: {
            POINT pt;
            pt.x = LOWORD(lParam); pt.y = HIWORD(lParam);
            {
                HWND hHit = ChildWindowFromPoint(hwnd, pt);
                if (hHit == g_hClock) {
                    HMENU hMenu = CreatePopupMenu();
                    ClientToScreen(hwnd, &pt);
                    AppendMenu(hMenu, MF_STRING, IDM_CLOCK_ADJUST, "Adjust Date/Time");
                    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
                    DestroyMenu(hMenu);
                    return 0;
                }
            }
            {
                HMENU hMenu = CreatePopupMenu(); POINT ptSc;
                ptSc.x = LOWORD(lParam); ptSc.y = HIWORD(lParam); ClientToScreen(hwnd, &ptSc);
                AppendMenu(hMenu, MF_STRING, IDM_TB_PROPS, "Taskbar Properties");
                AppendMenu(hMenu, MF_STRING, IDM_TB_SHOWDESKTOP, "Show Desktop");
                TrackPopupMenu(hMenu, 0, ptSc.x, ptSc.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt; RECT rc; pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); GetClientRect(hwnd, &rc);
            if (g_TbPosition == POS_BOTTOM && pt.y < 5) g_bResizing = TRUE;
            else if (g_TbPosition == POS_TOP && pt.y > rc.bottom - 5) g_bResizing = TRUE;
            else if (g_TbPosition == POS_LEFT && pt.x > rc.right - 5) g_bResizing = TRUE;
            else if (g_TbPosition == POS_RIGHT && pt.x < 5) g_bResizing = TRUE;
            else g_bDragging = TRUE;
            ClientToScreen(hwnd, &pt); g_DragStartPt = pt; SetCapture(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (GetCapture() == hwnd) {
                POINT pt; pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); ClientToScreen(hwnd, &pt);
                if (g_bResizing) {
                    int cx = GetSystemMetrics(SM_CXSCREEN); int cy = GetSystemMetrics(SM_CYSCREEN);
                    if (g_TbPosition == POS_BOTTOM) { g_TbHeight = cy - pt.y; if (g_TbHeight < 24) g_TbHeight = 24; }
                    else if (g_TbPosition == POS_TOP) { g_TbHeight = pt.y; if (g_TbHeight < 24) g_TbHeight = 24; }
                    else if (g_TbPosition == POS_LEFT) { g_TbWidthVert = pt.x; if (g_TbWidthVert < 48) g_TbWidthVert = 48; }
                    else if (g_TbPosition == POS_RIGHT) { g_TbWidthVert = cx - pt.x; if (g_TbWidthVert < 48) g_TbWidthVert = 48; }
                    ApplyLayout();
                } else if (g_bDragging) SnapToEdge(pt);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_bDragging || g_bResizing) { g_bDragging = FALSE; g_bResizing = FALSE; ReleaseCapture(); SaveConfig(); }
            return 0;
        }

        case WM_COMMAND: {
            int id = wParam;
            if (id == ID_START_BUTTON) ShowStartMenu(g_hStartBtn);
            else if (id >= ID_QUICK_BASE && id < ID_QUICK_BASE + QUICK_LAUNCH_COUNT) ShellExecute(hwnd, "open", g_QL[id - ID_QUICK_BASE].exe, NULL, NULL, SW_SHOWNORMAL);
            else if (id == IDM_SM_RUN) CreateCenteredDialog(g_hInst, hwnd, "RunDlgClass", "Run", 290, 160);
            else if (id == IDM_SM_EXIT) PostMessage(hwnd, WM_CLOSE, 0, 0);
            else if (id == IDM_SM_SEARCH) ShellExecute(hwnd, "open", g_szSearchExe, NULL, NULL, SW_SHOWNORMAL);
            else if (id == IDM_SM_HELP) ShellExecute(hwnd, "open", g_szHelpExe, NULL, NULL, SW_SHOWNORMAL);
            else if (id >= IDM_START_BASE && id < IDM_START_BASE + MAX_START_ITEMS) {
                if (g_MenuItemLnkPaths) {
                    char target[MAX_PATH], params[MAX_PATH]; int minF = 0;
                    char* p = g_MenuItemLnkPaths[id - IDM_START_BASE];
                    if (strstr(p, ".lnk")) {
                        GetPrivateProfileString("Shortcut", "Target", "", target, MAX_PATH, p);
                        GetPrivateProfileString("Shortcut", "Parameters", "", params, MAX_PATH, p);
                        minF = GetPrivateProfileInt("Shortcut", "RunMinimized", 0, p);
                        ShellExecute(hwnd, "open", target, params, NULL, minF ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
                    } else ShellExecute(hwnd, "open", p, NULL, NULL, SW_SHOWNORMAL);
                }
            }
            else if (id == IDM_TASK_MINIMIZE && g_ContextTargetWnd) ShowWindow(g_ContextTargetWnd, SW_MINIMIZE);
            else if (id == IDM_TASK_MAXIMIZE && g_ContextTargetWnd) ShowWindow(g_ContextTargetWnd, SW_MAXIMIZE);
            else if (id == IDM_TASK_CLOSE && g_ContextTargetWnd) PostMessage(g_ContextTargetWnd, WM_CLOSE, 0, 0);
            
            else if (id == IDM_WINX_SHORTCUT) { 
                lstrcpy(g_ShortcutParentDir, "C:\\"); 
                g_EditShortcutPath[0] = '\0'; 
                CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 340, 260); 
            }
            else if (id == IDM_WINX_NEWFOLDER) {
                lstrcpy(g_ContextPath, "C:\\"); 
                g_ContextIsDir = TRUE; 
                lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0';
                CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 130);
            }
            else if (id == IDM_WINX_CP) ShellExecute(hwnd, "open", g_szWinXCP, NULL, NULL, SW_SHOWNORMAL);
            else if (id == IDM_WINX_DEVMAN) ShellExecute(hwnd, "open", g_szWinXDevMan, NULL, NULL, SW_SHOWNORMAL);
            else if (id == IDM_TB_PROPS) MessageBox(hwnd, "Taskbar configuration is managed via CALMIRA.INI", "Taskbar Properties", MB_OK);
            else if (id == IDM_TB_SHOWDESKTOP) DoShowDesktop();
            else if (id == IDM_CLOCK_ADJUST) WinExec("control.exe date/time", SW_SHOWNORMAL);
            
            else if (id == IDM_CTX_PROPS) MessageBox(hwnd, g_ContextPath, "Properties", MB_OK);
            else if (id == IDM_CTX_NEWFOLDER) {
                lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0';
                CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 130);
            }
            else if (id == IDM_CTX_NEWSHORTCUT) {
                lstrcpy(g_ShortcutParentDir, g_ContextPath);
                g_EditShortcutPath[0] = '\0';
                CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 340, 260);
            }
            else if (id == IDM_CTX_RENAME) {
                char* p = strrchr(g_ContextPath, '\\');
                lstrcpy(g_PromptLabel, "New Name:"); lstrcpy(g_PromptValue, p ? p + 1 : g_ContextPath);
                CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "Rename", 290, 130);
            }
            else if (id == IDM_CTX_DELETE) {
                if (MessageBox(hwnd, "Delete item?", "Confirm", MB_YESNO) == IDYES) {
                    if (g_ContextIsDir) rmdir(g_ContextPath); else remove(g_ContextPath);
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_CLOCK) UpdateClock();
            else if (wParam == TIMER_HOTKEY) { if (GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000) PostMessage(hwnd, WM_COMMAND, ID_START_BUTTON, 0); }
            return 0;

        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: {
            int i;
            for (i=0; i<g_IconCacheCount; i++) if (g_IconCache[i].hIconLarge) DestroyIcon(g_IconCache[i].hIconLarge);
            if (g_hCacheBitmap) DeleteObject(g_hCacheBitmap);
            if (g_hMemODItems) GlobalFree(g_hMemODItems);
            if (g_hMemLnkPaths) GlobalFree(g_hMemLnkPaths);
            if (g_hMemMenuDirs) GlobalFree(g_hMemMenuDirs);
            if (g_hMemIconCache) GlobalFree(g_hMemIconCache);
            KillTimer(hwnd, TIMER_CLOCK);
            KillTimer(hwnd, TIMER_REFRESH);
            KillTimer(hwnd, TIMER_HOTKEY);
            if (g_hFontMenu) DeleteObject(g_hFontMenu);
            if (g_hFontSidebar) DeleteObject(g_hFontSidebar);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


/* ──────────────────────────────────────────────────────────────────────────
   Entry Point
   ────────────────────────────────────────────────────────────────────────── */
int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc; MSG msg; g_hInst = hInst;
    
    g_lpfnEnumWindowsProc = MakeProcInstance((FARPROC)TaskbarEnumWindowsProc, hInst);
    g_lpfnTaskBtnProc = MakeProcInstance((FARPROC)TaskBtnProc, hInst);
    g_lpfnTrayAreaProc = MakeProcInstance((FARPROC)TrayAreaProc, hInst);
    
    memset(&wc, 0, sizeof(WNDCLASS)); wc.style = CS_DBLCLKS; wc.lpfnWndProc = TaskbarProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "CalmiraTaskbarClass";
    RegisterClass(&wc);

    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = RunDlgProc; wc.hInstance = hInst; wc.lpszClassName = "RunDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = ShortcutDlgProc; wc.hInstance = hInst; wc.lpszClassName = "ShortcutDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = PromptDlgProc; wc.hInstance = hInst; wc.lpszClassName = "PromptDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    
    g_hTaskbar = CreateWindowEx(WS_EX_ACCEPTFILES, "CalmiraTaskbarClass", "Calmira Taskbar", WS_POPUP | WS_VISIBLE, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    ShowWindow(g_hTaskbar, nCmdShow); UpdateWindow(g_hTaskbar);

    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    FreeProcInstance(g_lpfnEnumWindowsProc);
    FreeProcInstance(g_lpfnTaskBtnProc);
    FreeProcInstance(g_lpfnTrayAreaProc);
    if (g_lpfnStartBtnProc) FreeProcInstance(g_lpfnStartBtnProc);
    if (g_lpfnClockProc) FreeProcInstance(g_lpfnClockProc);

    return (int)msg.wParam;
}
/* EOF */
