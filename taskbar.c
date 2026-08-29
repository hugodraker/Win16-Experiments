/* ============================================================================
 * Calmira Taskbar v0.8 - Fully Self-Contained Win16 Implementation
 *
 * FEATURES:
 * - Single-file C architecture with clean CALLBACK calling conventions
 * - Root-level items correctly align with the Windows 9x Sidebar
 * - In-Memory A-Z Alphabetical Sorting (Folders first, then Files)
 * - True GDI Polygon rendering for system root icons (Scale-Mapped)
 * - Dynamic Search/Autocomplete ListBox with 250ms delayed filtering
 * - Native Properties Dialog supports live Name/Folder editing
 * - Thread-Safe Menu Locks preventing Task-Refresh destruction
 * - Win16 MAKELONG WM_COMMAND mapping
 * - C89/C90 Strict Compiler Compliant
 *
 * COMPILATION INSTRUCTIONS:
 *   wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s taskbar.c shell.lib commdlg.lib
 * ============================================================================ */

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dos.h>

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
#define MAX_INI_SHORTCUTS   256

#define TIMER_CLOCK         1001
#define TIMER_REFRESH       1002
#define TIMER_HOTKEY        1003
#define TIMER_SEARCH        1004

#define ID_START_BUTTON     2001
#define ID_TASK_BASE        3000
#define ID_QUICK_BASE       4000
#define ID_CLOCK            5001
#define ID_TRAY_AREA        5002

#define IDM_START_BASE      8000
#define IDM_SEARCH_EDIT     4999
#define IDM_SEARCH_LIST     4998

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
#define IDM_TB_TASKMGR 7008
#define IDM_FS_BASE 15000

/* ──────────────────────────────────────────────────────────────────────────
   Data Structures & Globals
   ────────────────────────────────────────────────────────────────────────── */
typedef struct { HWND hWnd; char title[128]; HWND hBtn; } TaskEntry;
typedef struct { char name[16]; char exe[MAX_PATH]; } QuickLaunchDef;
typedef struct { HICON hIcon; char tooltip[32]; } TrayIcon;
typedef struct {
    char text[64];
    HICON hIcon;
    int cacheIndex;
    BOOL bDestroyIcon;
    BOOL isRoot;
    BOOL isSeparator;
    int polyIcon;
    int yOffset;
    int totalHeight;
    char targetPath[MAX_PATH];
    BOOL isFsFolder;
    UINT cmdId;
} ODMenuItem;


typedef struct {
    char id[12];
    char name[64];
    char exe[MAX_PATH];
    char params[MAX_PATH];
    char icon[MAX_PATH];
    char minimizedStr[4];
    char parentId[12];
    BOOL minimized;
    BOOL isFolder;
    HMENU hMenu;
    char hotkey[16];
} IniShortcut;


static HINSTANCE g_hInst;
static HWND g_hTaskbar = NULL;
static HWND g_hStartBtn = NULL;
static HWND g_hTaskList = NULL;
static HWND g_hClock = NULL;
static HWND g_hTrayArea = NULL;
static HWND g_hQuickLaunch[QUICK_LAUNCH_COUNT];
static HWND g_ContextTargetWnd = NULL;
static HWND g_hSearchBox = NULL;
static HWND g_hSearchList = NULL;

static FARPROC g_lpfnTaskListProc = NULL;
static FARPROC OldTaskListProc = NULL;

static TaskEntry g_Tasks[MAX_TASKS];
static int g_TaskCount = 0;

static TrayIcon g_TrayIcons[MAX_TRAY_ICONS];
static int g_TrayIconCount = 0;

static ODMenuItem FAR* g_ODItems = NULL;
static int g_ODCount = 0;

static IniShortcut FAR* g_IniShortcuts = NULL;
static int g_IniShortcutCount = 0;

static HBITMAP g_hCacheBitmap = NULL;
static int g_MaxCacheId = 0;

static char g_ContextId[16] = "";
static BOOL g_ContextIsFolder = FALSE;
static char g_EditShortcutId[16] = "";
static BOOL g_MenuOpen = FALSE;

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
static FARPROC g_lpfnSearchBoxProc = NULL;
static FARPROC g_lpfnSearchListProc = NULL;

static FARPROC OldTaskBtnProc = NULL;
static FARPROC OldTrayAreaProc = NULL;
static FARPROC OldStartBtnProc = NULL;
static FARPROC OldClockProc = NULL;
static FARPROC OldSearchBoxProc = NULL;
static FARPROC OldSearchListProc = NULL;
//static FARPROC g_lpfnHotkeyProc = NULL;
//static FARPROC OldHotkeyProc = NULL;

/* Global Heap Handles */
static HGLOBAL g_hMemODItems = NULL;
static HGLOBAL g_hMemIniShortcuts = NULL;
static void ShowFolderMenu(HWND hAnchor, const char* folderId);
static void LaunchShortcut(HWND hwnd, IniShortcut* sh);
static FARPROC g_lpfnHotkeyProc = NULL;
static FARPROC OldHotkeyProc = NULL;

/* ──────────────────────────────────────────────────────────────────────────
   Prototypes
   ────────────────────────────────────────────────────────────────────────── */
static void InitFonts(void);
static void ApplyLayout(void);
static void RefreshTasks(void);
static void SnapToEdge(POINT pt);
static void EnsureCacheBitmapSize(int maxId);
static void SaveIconCacheTo16ColorBMP(int maxId);
static void UpdateIconInCache(int id, const char* iconPath, const char* exePath, BOOL isFolder);
static void DoShowDesktop(void);
static void SaveConfig(void);
static void LoadConfig(void);
static void AddTrayIcon(HICON hIcon, const char* tooltip);
static void PositionDialogNearStart(HWND hwnd);
static void CreateCenteredDialog(HINSTANCE hInst, HWND hParent, LPCSTR className, LPCSTR title, int w, int h);
static void ClearODIcons(void);
static void SaveQuickLaunchItem(int i);
static void LoadIniShortcuts(void);
static void RunStartupItems(void);

static void ShowFolderMenu(HWND hAnchor, const char* folderId);
static void LaunchShortcut(HWND hwnd, IniShortcut* sh);
static ODMenuItem* AddODItem(const char* text, const char* itemIdStr, const char* fallbackExe, BOOL isRoot, BOOL isSeparator, int polyIcon);

LRESULT CALLBACK DateTimeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK TaskbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK RunDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK TrayAreaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StartBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ClockProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TaskBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK SearchListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK MsgFilterProc(int code, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK    TaskbarEnumWindowsProc(HWND hwnd, LPARAM lParam);
BOOL CALLBACK    MinimizeEnumProc(HWND hwnd, LPARAM lParam);

/* ──────────────────────────────────────────────────────────────────────────
   Utility Functions & Icon Drawing Engine
   ────────────────────────────────────────────────────────────────────────── */
static void GetAppFilePath(const char* filename, char* outPath) {
    char* p; GetModuleFileName(g_hInst, outPath, MAX_PATH);
    p = strrchr(outPath, '\\'); if (p) lstrcpy(p + 1, filename); else lstrcpy(outPath, filename);
}
static void SetFont(HWND hwnd, HFONT font) {
    if (!font) font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    if (!font) font = (HFONT)GetStockObject(SYSTEM_FONT);
    SendMessage(hwnd, WM_SETFONT, (WPARAM)font, MAKELONG(TRUE, 0));
}
static void TrimCaption(const char* src, char* dst, int maxLen) {
    int len = lstrlen(src);
    if (len > maxLen) { memcpy(dst, src, maxLen - 3); dst[maxLen-3] = '.'; dst[maxLen-2] = '.'; dst[maxLen-1] = '.'; dst[maxLen] = '\0'; } 
    else lstrcpy(dst, src);
}
static void UpdateClock(void) {
    char timeStr[32]; struct dostime_t t;
    _dos_gettime(&t);
    sprintf(timeStr, "%02d:%02d", t.hour, t.minute);
    if (g_hClock) SetWindowText(g_hClock, timeStr);
}

typedef BOOL (WINAPI *LPGETOPENFILENAME)(LPOPENFILENAME);
static BOOL BrowseFile(HWND hwnd, char* outPath, const char* filter) {
    OPENFILENAME ofn; char file[MAX_PATH] = ""; HINSTANCE hCommDlg; BOOL bRet = FALSE;
    memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    hCommDlg = LoadLibrary("commdlg.dll");
    if (hCommDlg >= (HINSTANCE)32) {
        LPGETOPENFILENAME pGetOpenFileName = (LPGETOPENFILENAME)GetProcAddress(hCommDlg, "GETOPENFILENAME");
        if (pGetOpenFileName) { if (pGetOpenFileName(&ofn)) { lstrcpy(outPath, file); bRet = TRUE; } }
        FreeLibrary(hCommDlg);
    }
    return bRet;
}

static void InitFonts(void) {
    g_hFontMenu = CreateFont(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_SWISS, "Arial");
    g_hFontSidebar = CreateFont(-22, 0, 900, 900, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_SWISS, "Arial");
}

static void PositionDialogNearStart(HWND hwnd) {
    RECT rcStart, rcDlg; int x, y, w, h;
    int screenW = GetSystemMetrics(SM_CXSCREEN); int screenH = GetSystemMetrics(SM_CYSCREEN);
    GetWindowRect(g_hStartBtn, &rcStart); GetWindowRect(hwnd, &rcDlg);
    w = rcDlg.right - rcDlg.left; h = rcDlg.bottom - rcDlg.top;
    if (g_TbPosition == POS_BOTTOM) { x = rcStart.left; y = rcStart.top - h; } 
    else if (g_TbPosition == POS_TOP) { x = rcStart.left; y = rcStart.bottom; } 
    else if (g_TbPosition == POS_LEFT) { x = rcStart.right; y = rcStart.top; } 
    else { x = rcStart.left - w; y = rcStart.top; }
    if (x < 0) x = 0; if (y < 0) y = 0; if (x + w > screenW) x = screenW - w; if (y + h > screenH) y = screenH - h;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void CreateCenteredDialog(HINSTANCE hInst, HWND hParent, LPCSTR className, LPCSTR title, int w, int h) {
    RECT rcStart; int x = 100, y = 100, screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
    if (g_hStartBtn && IsWindow(g_hStartBtn)) { GetWindowRect(g_hStartBtn, &rcStart); x = rcStart.left; y = rcStart.top - h; if (y < 0) y = rcStart.bottom; } 
    else { x = (screenW - w) / 2; y = (screenH - h) / 2; }
    if (x + w > screenW) x = screenW - w; if (y + h > screenH) y = screenH - h; if (x < 0) x = 0; if (y < 0) y = 0;
    CreateWindowEx(0, className, title, WS_VISIBLE | WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, hParent, NULL, hInst, NULL);
    EnableWindow(hParent, FALSE);
}

/* ──────────────────────────────────────────────────────────────────────────
   GDI Polygons scaled dynamically by 50% for Menu Draw Area
   ────────────────────────────────────────────────────────────────────────── */
static void DrawScaledPolygon(HDC hdc, const POINT* pts, int count, int dx, int dy) {
    POINT* temp = (POINT*)malloc(sizeof(POINT) * count); int i;
    for(i = 0; i < count; ++i) { temp[i].x = (pts[i].x / 2) + dx; temp[i].y = (pts[i].y / 2) + dy; }
    Polygon(hdc, temp, count); free(temp);
}

#define DRAW_POLY(pts, count, r, g, b) \
    do { \
        COLORREF color = RGB(r, g, b); \
        HBRUSH hBr = CreateSolidBrush(color); \
        HPEN hPn = CreatePen(PS_SOLID, 1, color); \
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr); \
        HPEN hOldPn = (HPEN)SelectObject(hdc, hPn); \
        DrawScaledPolygon(hdc, pts, count, x, y); \
        SelectObject(hdc, hOldBr); SelectObject(hdc, hOldPn); \
        DeleteObject(hBr); DeleteObject(hPn); \
    } while(0)

static void DrawIconFolder(HDC hdc, int x, int y) {
    POINT fBack[] = {{8,12}, {24,12}, {28,16}, {50,16}, {50,44}, {8,44}}; DRAW_POLY(fBack, 6, 230, 190, 40);
    POINT fIn[] = {{12,18}, {48,18}, {48,42}, {12,42}}; DRAW_POLY(fIn, 4, 200, 150, 20);
    POINT fFront[] = {{4,46}, {16,24}, {48,24}, {36,46}}; DRAW_POLY(fFront, 4, 255, 220, 70);
}
static void DrawIconSearch(HDC hdc, int x, int y) {
    POINT pShad[] = {{16,12}, {44,12}, {52,20}, {52,60}, {16,60}}; DRAW_POLY(pShad, 5, 180, 180, 180);
    POINT pBase[] = {{12,8}, {40,8}, {48,16}, {48,56}, {12,56}}; DRAW_POLY(pBase, 5, 250, 250, 250);
    POINT pFold[] = {{40,8}, {40,16}, {48,16}}; DRAW_POLY(pFold, 3, 220, 220, 220);
    POINT t1[] = {{18,22}, {40,22}, {40,26}, {18,26}}; DRAW_POLY(t1, 4, 200, 200, 200);
    POINT hShad[] = {{38,38}, {58,58}, {52,64}, {32,44}}; DRAW_POLY(hShad, 4, 50, 50, 50);
    POINT hBase[] = {{36,36}, {56,56}, {50,62}, {30,42}}; DRAW_POLY(hBase, 4, 90, 90, 90);
    POINT rimOut[] = {{20,20}, {28,16}, {38,16}, {46,24}, {46,34}, {38,42}, {28,42}, {20,34}}; DRAW_POLY(rimOut, 8, 160, 160, 170);
    POINT glass[] = {{24,24}, {28,22}, {34,22}, {39,27}, {39,31}, {34,36}, {28,36}, {24,31}}; DRAW_POLY(glass, 8, 180, 230, 255);
}
static void DrawIconSettings(HDC hdc, int x, int y) {
    POINT fBack[] = {{8,12}, {24,12}, {28,16}, {50,16}, {50,44}, {8,44}}; DRAW_POLY(fBack, 6, 230, 190, 40);
    POINT fIn[] = {{12,18}, {48,18}, {48,42}, {12,42}}; DRAW_POLY(fIn, 4, 200, 150, 20);
    POINT docDShad[] = {{32,20}, {48,20}, {56,28}, {56,56}, {32,56}}; DRAW_POLY(docDShad, 5, 180, 180, 180);
    POINT doc[] = {{30,18}, {46,18}, {54,26}, {54,54}, {30,54}}; DRAW_POLY(doc, 5, 250, 250, 250);
    POINT docFold[] = {{46,18}, {46,26}, {54,26}}; DRAW_POLY(docFold, 3, 210, 210, 210);
    POINT fFront[] = {{4,46}, {16,24}, {48,24}, {36,46}}; DRAW_POLY(fFront, 4, 255, 220, 70);
    POINT gShad[] = {{22,26}, {26,26}, {28,30}, {34,30}, {36,26}, {40,26}, {40,32}, {44,34}, {48,34}, {48,38}, {44,40}, {44,44}, {40,46}, {40,52}, {36,52}, {34,48}, {28,48}, {26,52}, {22,52}, {22,46}, {18,44}, {14,44}, {14,40}, {18,38}, {18,34}, {22,32}}; DRAW_POLY(gShad, 26, 130, 130, 130);
    POINT gTop[] = {{20,24}, {24,24}, {26,28}, {32,28}, {34,24}, {38,24}, {38,30}, {42,32}, {46,32}, {46,36}, {42,38}, {42,42}, {38,44}, {38,50}, {34,50}, {32,46}, {26,46}, {24,50}, {20,50}, {20,44}, {16,42}, {12,42}, {12,38}, {16,36}, {16,32}, {20,30}}; DRAW_POLY(gTop, 26, 220, 220, 220);
    POINT gInRing[] = {{24,32}, {34,32}, {38,36}, {38,40}, {34,44}, {24,44}, {20,40}, {20,36}}; DRAW_POLY(gInRing, 8, 180, 180, 180);
    POINT gHole[] = {{26,35}, {32,35}, {35,38}, {32,41}, {26,41}, {23,38}}; DRAW_POLY(gHole, 6, 100, 100, 100);
}
static void DrawIconHelp(HDC hdc, int x, int y) {
    POINT spine[] = {{10,34}, {28,48}, {54,34}, {34,20}}; DRAW_POLY(spine, 4, 80, 20, 100);
    POINT pagesBase[] = {{12,41}, {28,52}, {52,38}, {34,26}}; DRAW_POLY(pagesBase, 4, 180, 180, 180);
    POINT coverTop[] = {{8,32}, {26,46}, {50,32}, {32,18}}; DRAW_POLY(coverTop, 4, 140, 50, 180);
    POINT qS1[] = {{22,26}, {30,22}, {38,26}, {36,32}, {30,34}, {28,40}, {24,38}, {26,32}, {32,30}, {34,28}, {30,26}, {24,28}}; DRAW_POLY(qS1, 12, 180, 110, 0);
    POINT qSDot[] = {{26,42}, {30,44}, {28,46}, {24,44}}; DRAW_POLY(qSDot, 4, 180, 110, 0);
    POINT qT1[] = {{22,24}, {30,20}, {38,24}, {36,30}, {30,32}, {28,38}, {24,36}, {26,30}, {32,28}, {34,26}, {30,24}, {24,26}}; DRAW_POLY(qT1, 12, 255, 220, 40);
    POINT qTDot[] = {{26,40}, {30,42}, {28,44}, {24,42}}; DRAW_POLY(qTDot, 4, 255, 220, 40);
}
static void DrawIconRun(HDC hdc, int x, int y) {
    POINT wBase[] = {{18,14}, {62,14}, {62,42}, {18,42}}; DRAW_POLY(wBase, 4, 245, 245, 245);
    POINT hwTop[] = {{6,14}, {34,14}, {34,18}, {6,18}}; DRAW_POLY(hwTop, 4, 170, 110, 50);
    POINT hwBot[] = {{6,54}, {34,54}, {34,58}, {6,58}}; DRAW_POLY(hwBot, 4, 140, 90, 40);
    POINT gBack[] = {{10,18}, {30,18}, {24,36}, {30,52}, {10,52}, {16,36}}; DRAW_POLY(gBack, 6, 200, 220, 230);
    POINT sandTop[] = {{12,24}, {28,24}, {24,34}, {16,34}}; DRAW_POLY(sandTop, 4, 220, 190, 120);
    POINT sandBot[] = {{20,38}, {24,46}, {28,52}, {12,52}, {16,46}}; DRAW_POLY(sandBot, 5, 200, 170, 100);
    POINT gFront1[] = {{10,18}, {16,18}, {20,30}, {18,36}, {14,30}}; DRAW_POLY(gFront1, 5, 240, 250, 255);
}
static void DrawIconMonitor(HDC hdc, int x, int y) {
    POINT cRight[] = {{34,14}, {54,18}, {54,40}, {38,44}}; DRAW_POLY(cRight, 4, 140, 140, 140);
    POINT cTop[] = {{10,24}, {34,14}, {54,18}, {30,28}}; DRAW_POLY(cTop, 4, 230, 230, 230);
    POINT neck[] = {{24,46}, {30,44}, {30,54}, {24,56}}; DRAW_POLY(neck, 4, 120, 120, 120);
    POINT base[] = {{18,52}, {38,48}, {48,52}, {28,58}}; DRAW_POLY(base, 4, 180, 180, 180);
    POINT mFront[] = {{8,26}, {32,16}, {36,46}, {12,54}}; DRAW_POLY(mFront, 4, 190, 190, 190);
    POINT screen[] = {{14,29}, {28,23}, {30,42}, {16,47}}; DRAW_POLY(screen, 4, 20, 40, 120);
    POINT gOut[] = {{18,34}, {22,29}, {26,29}, {28,34}, {24,39}, {20,39}}; DRAW_POLY(gOut, 6, 0, 200, 255);
}

/* ──────────────────────────────────────────────────────────────────────────
   16-Color BMP Icon Caching Engine
   ────────────────────────────────────────────────────────────────────────── */
static HBITMAP Load16ColorBMP(const char* path, HDC hdc) {
    FILE *f = fopen(path, "rb"); BITMAPFILEHEADER bfh; DWORD infoSize, imgSize; BITMAPINFO *pbmi; LPBYTE pBits; HBITMAP hbm;
    if (!f) return NULL;
    fread(&bfh, 1, sizeof(bfh), f); if (bfh.bfType != 0x4D42) { fclose(f); return NULL; }
    infoSize = bfh.bfOffBits - sizeof(BITMAPFILEHEADER); pbmi = (BITMAPINFO*)GlobalLock(GlobalAlloc(GPTR, infoSize));
    if (!pbmi) { fclose(f); return NULL; } fread(pbmi, 1, infoSize, f);
    imgSize = pbmi->bmiHeader.biSizeImage; if (imgSize == 0) imgSize = (((pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biBitCount + 31) & ~31) / 8) * abs(pbmi->bmiHeader.biHeight);
    pBits = (LPBYTE)GlobalLock(GlobalAlloc(GPTR, imgSize)); if (!pBits) { GlobalFree((HGLOBAL)pbmi); fclose(f); return NULL; }
    fseek(f, bfh.bfOffBits, SEEK_SET); fread(pBits, 1, imgSize, f); fclose(f);
    hbm = CreateDIBitmap(hdc, &pbmi->bmiHeader, CBM_INIT, pBits, pbmi, DIB_RGB_COLORS);
    GlobalFree((HGLOBAL)pbmi); GlobalFree((HGLOBAL)pBits); return hbm;
}

static void EnsureCacheBitmapSize(int maxId) {
    int reqWidth = (maxId + 1) * 32, curWidth = 0; BITMAP bm; HDC hdcScreen, hdcMemOld, hdcMemNew; HBITMAP hNewBmp; HBRUSH hbg;
    if (g_hCacheBitmap) { GetObject(g_hCacheBitmap, sizeof(bm), &bm); curWidth = bm.bmWidth; if (curWidth >= reqWidth) return; }
    hdcScreen = GetDC(NULL); hdcMemOld = CreateCompatibleDC(hdcScreen); hdcMemNew = CreateCompatibleDC(hdcScreen);
    hNewBmp = CreateCompatibleBitmap(hdcScreen, reqWidth, 32); SelectObject(hdcMemNew, hNewBmp);
    hbg = CreateSolidBrush(GetSysColor(COLOR_MENU)); { RECT rc; rc.left = 0; rc.top = 0; rc.right = reqWidth; rc.bottom = 32; FillRect(hdcMemNew, &rc, hbg); } DeleteObject(hbg);
    if (g_hCacheBitmap) { SelectObject(hdcMemOld, g_hCacheBitmap); BitBlt(hdcMemNew, 0, 0, curWidth, 32, hdcMemOld, 0, 0, SRCCOPY); SelectObject(hdcMemOld, NULL); DeleteObject(g_hCacheBitmap); }
    g_hCacheBitmap = hNewBmp; DeleteDC(hdcMemOld); DeleteDC(hdcMemNew); ReleaseDC(NULL, hdcScreen);
}

static void SaveIconCacheTo16ColorBMP(int maxId) {
    HDC hdcScreen = GetDC(NULL); int width = (maxId + 1) * 32, height = 32; struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[16]; } bmi;
    RGBQUAD pal[16] = {{0,0,0,0}, {0,0,128,0}, {0,128,0,0}, {0,128,128,0}, {128,0,0,0}, {128,0,128,0}, {128,128,0,0}, {128,128,128,0}, {192,192,192,0}, {0,0,255,0}, {0,255,0,0}, {0,255,255,0}, {255,0,0,0}, {255,0,255,0}, {255,255,0,0}, {255,255,255,0}};
    if (!g_hCacheBitmap) { ReleaseDC(NULL, hdcScreen); return; }
    memset(&bmi, 0, sizeof(bmi)); bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = width; bmi.bmiHeader.biHeight = height; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 4; bmi.bmiHeader.biCompression = BI_RGB; memcpy(bmi.bmiColors, pal, sizeof(pal));
    GetDIBits(hdcScreen, g_hCacheBitmap, 0, height, NULL, (BITMAPINFO*)&bmi, DIB_RGB_COLORS);
    if (bmi.bmiHeader.biSizeImage == 0) bmi.bmiHeader.biSizeImage = (((width * 4) + 31) & ~31) / 8 * height;
    { LPBYTE lpBits = (LPBYTE)GlobalLock(GlobalAlloc(GPTR, bmi.bmiHeader.biSizeImage));
      if (lpBits) {
          if (GetDIBits(hdcScreen, g_hCacheBitmap, 0, height, lpBits, (BITMAPINFO*)&bmi, DIB_RGB_COLORS)) {
              BITMAPFILEHEADER bfh; FILE *f = fopen(g_szCachePath, "wb");
              if (f) { bfh.bfType = 0x4D42; bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(bmi) + bmi.bmiHeader.biSizeImage; bfh.bfReserved1 = 0; bfh.bfReserved2 = 0; bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(bmi);
                  fwrite(&bfh, 1, sizeof(bfh), f); fwrite(&bmi, 1, sizeof(bmi), f); fwrite(lpBits, 1, bmi.bmiHeader.biSizeImage, f); fclose(f); } } GlobalFree((HGLOBAL)lpBits); } }
    ReleaseDC(NULL, hdcScreen);
}

static void UpdateIconInCache(int id, const char* iconPath, const char* exePath, BOOL isFolder) {
    HDC hdcScreen, hdcMem; HICON hIcon = NULL; HBRUSH hbg; RECT rc;
    EnsureCacheBitmapSize(id);
    if (iconPath && iconPath[0]) hIcon = ExtractIcon(g_hInst, iconPath, 0);
    if ((int)hIcon <= 1 && exePath && exePath[0]) hIcon = ExtractIcon(g_hInst, exePath, 0);
    if ((int)hIcon <= 1) { if (isFolder) hIcon = LoadIcon(g_hInst, IDI_APPLICATION); else hIcon = LoadIcon(NULL, IDI_APPLICATION); }
    hdcScreen = GetDC(NULL); hdcMem = CreateCompatibleDC(hdcScreen); SelectObject(hdcMem, g_hCacheBitmap);
    rc.left = id * 32; rc.top = 0; rc.right = rc.left + 32; rc.bottom = 32;
    hbg = CreateSolidBrush(GetSysColor(COLOR_MENU)); FillRect(hdcMem, &rc, hbg); DeleteObject(hbg);
    if (hIcon) { DrawIcon(hdcMem, id * 32, 0, hIcon); DestroyIcon(hIcon); }
    DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
    if (id > g_MaxCacheId) g_MaxCacheId = id;
    SaveIconCacheTo16ColorBMP(g_MaxCacheId);
}

/* ──────────────────────────────────────────────────────────────────────────
   Configuration Engine & INI Parsing (taskbar.ini)
   ────────────────────────────────────────────────────────────────────────── */

static void BuildMenuFromFileSystemFolder(HMENU hMenu, const char* dirPath) {
    struct find_t file;
    char searchPath[MAX_PATH];
    int result, totalItems = 0, currentItem = 0, itemsPerCol;

    lstrcpy(searchPath, dirPath);
    if (searchPath[lstrlen(searchPath) - 1] != '\\') {
        lstrcat(searchPath, "\\");
    }
    lstrcat(searchPath, "*.*");

    /* First pass: Count items to calculate column breaks */
    result = _dos_findfirst(searchPath, _A_NORMAL | _A_SUBDIR | _A_RDONLY | _A_ARCH, &file);
    while (result == 0) {
        if (lstrcmp(file.name, ".") != 0 && lstrcmp(file.name, "..") != 0) {
            totalItems++;
        }
        result = _dos_findnext(&file);
    }

    if (totalItems == 0) return;

    /* Calculate items per column to achieve a maximum of 5 columns */
    itemsPerCol = (totalItems + 4) / 5;
    if (itemsPerCol < 1) itemsPerCol = 1;

    /* Second pass: Build the menu */
    result = _dos_findfirst(searchPath, _A_NORMAL | _A_SUBDIR | _A_RDONLY | _A_ARCH, &file);
    while (result == 0) {
        if (lstrcmp(file.name, ".") != 0 && lstrcmp(file.name, "..") != 0) {
            char fullPath[MAX_PATH];
            ODMenuItem* pItem;
            UINT flags = MF_OWNERDRAW | MF_STRING;
            
            /* Apply column break if we reached the itemsPerCol threshold */
            if (currentItem > 0 && (currentItem % itemsPerCol) == 0) {
                flags |= MF_MENUBARBREAK;
            }

            lstrcpy(fullPath, dirPath);
            if (fullPath[lstrlen(fullPath) - 1] != '\\') lstrcat(fullPath, "\\");
            lstrcat(fullPath, file.name);

            if (file.attrib & _A_SUBDIR) {
                /* Use icon 0 (Folder) instead of 6 (Monitor) */
                pItem = AddODItem(file.name, NULL, NULL, FALSE, FALSE, 0);
                if (pItem) {
                    lstrcpy(pItem->targetPath, fullPath);
                    pItem->isFsFolder = TRUE;
                    AppendMenu(hMenu, flags, pItem->cmdId, (LPSTR)pItem);
                }
            } else {
                HICON hFileIcon = ExtractIcon(g_hInst, fullPath, 0);
                if ((int)hFileIcon <= 1) hFileIcon = LoadIcon(NULL, IDI_APPLICATION);
                
                pItem = AddODItem(file.name, NULL, NULL, FALSE, FALSE, -1);
                if (pItem) {
                    pItem->hIcon = hFileIcon;
                    pItem->bDestroyIcon = TRUE;
                    lstrcpy(pItem->targetPath, fullPath);
                    pItem->isFsFolder = FALSE;
                    AppendMenu(hMenu, flags, pItem->cmdId, (LPSTR)pItem);
                }
            }
            currentItem++;
        }
        result = _dos_findnext(&file);
    }
}

LRESULT CALLBACK HotkeyEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        int vk = wp;
        if (vk == VK_ESCAPE || vk == VK_BACK || vk == VK_DELETE) {
            SetWindowText(hwnd, "");
            RemoveProp(hwnd, "VK");
            return 0;
        }
        if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
            char name[64];
            SetProp(hwnd, "VK", (HANDLE)vk);
            /* lp already contains the scan code and extended flag needed by GetKeyNameText */
            GetKeyNameText(lp, name, sizeof(name));
            SetWindowText(hwnd, name);
        }
        return 0;
    }
    if (msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_KEYUP || msg == WM_SYSKEYUP) return 0;
    return CallWindowProc((FARPROC)OldHotkeyProc, hwnd, msg, wp, lp);
}
static void ParseIniEntry(const char* id, const char* val, IniShortcut* out) {
    const char* p = val; int i; char* dests[7];
    dests[0] = out->name; dests[1] = out->exe; dests[2] = out->params; dests[3] = out->icon; dests[4] = out->minimizedStr; dests[5] = out->parentId; dests[6] = out->hotkey;
    lstrcpy(out->id, id);
    for (i = 0; i < 7; i++) {
        char* d = dests[i]; int c = 0;
        while (*p && *p != '|') { if (c < MAX_PATH - 1) d[c++] = *p; p++; }
        d[c] = '\0'; if (*p == '|') p++;
    }
    {
        int flags = atoi(out->minimizedStr);
        out->minimized = (flags & 1) != 0;
        out->isFolder = (flags & 2) != 0;
        if (out->exe[0] == '\0' && flags == 0 && lstrcmp(out->minimizedStr, "0") == 0) {
            out->isFolder = TRUE; /* Legacy support for older INI formats where empty exe implied folder */
        }
    }
    out->hMenu = NULL;
}


static void GetNewIniId(char* outId) {
    char keys[4096], *pKey; int maxId = 0;
    GetPrivateProfileString("Shortcut", NULL, "", keys, sizeof(keys), g_szIniPath); pKey = keys;
    while (*pKey) { int id = atoi(pKey); if (id > maxId) maxId = id; pKey += lstrlen(pKey) + 1; }
    sprintf(outId, "%08d", maxId + 1);
}

static void SaveIniEntry(IniShortcut* item) {
    char val[1024]; 
    int flags = (item->minimized ? 1 : 0) | (item->isFolder ? 2 : 0);
    sprintf(val, "%s|%s|%s|%s|%d|%s|%s", item->name, item->exe, item->params, item->icon, flags, item->parentId, item->hotkey);
    WritePrivateProfileString("Shortcut", item->id, val, g_szIniPath);
}

int CompareIni(const void* a, const void* b) {
    IniShortcut* sa = (IniShortcut*)a;
    IniShortcut* sb = (IniShortcut*)b;
    int cmp = lstrcmp(sa->parentId, sb->parentId);
    
    if (cmp == 0) {
        if (lstrcmp(sa->parentId, "0") == 0) {
            int idA = atoi(sa->id);
            int idB = atoi(sb->id);
            
            /* Shutdown (ID 8) is always the absolute bottom */
            if (idA == 8 && idB != 8) return 1;
            if (idB == 8 && idA != 8) return -1;
            
            /* Custom items (ID > 8) go above standard OS items (ID <= 8) */
            if (idA > 8 && idB <= 8) return -1;
            if (idB > 8 && idA <= 8) return 1;
            
            /* Newer custom items go on top of older custom items (LIFO / Descending) */
            if (idA > 8 && idB > 8) return idB - idA;
            
            /* Standard OS items (1 to 7) remain in their ascending original order */
            return idA - idB;
        }
        
        /* Non-root items: folders first, then alphabetically */
        if (sa->isFolder != sb->isFolder) return sb->isFolder - sa->isFolder;
        cmp = lstrcmpi(sa->name, sb->name);
    }
    return cmp;
}

static void LoadIniShortcuts(void) {
    char keys[4096], *pKey; g_IniShortcutCount = 0;
    GetPrivateProfileString("Shortcut", NULL, "", keys, sizeof(keys), g_szIniPath);
    if (keys[0] == '\0') {
        WritePrivateProfileString("Shortcut", "00000001", "Programs||||2|0|", g_szIniPath);
        WritePrivateProfileString("Shortcut", "00000002", "Startup||||2|00000001|", g_szIniPath);
        WritePrivateProfileString("Shortcut", "00000003", "-||||0|0|", g_szIniPath);
        WritePrivateProfileString("Shortcut", "00000004", "Settings|||3|2|0|", g_szIniPath);
        WritePrivateProfileString("Shortcut", "00000005", "Search|winfile.exe||2|0|0|", g_szIniPath);
        WritePrivateProfileString("Shortcut", "00000006", "Help|winhelp.exe||4|0|0|", g_szIniPath);
        WritePrivateProfileString("Shortcut", "00000007", "Run...|run||5|0|0|", g_szIniPath);
        WritePrivateProfileString("Shortcut", "00000008", "Shut Down...|shutdown||6|0|0|", g_szIniPath);
        
        /* Custom mapping of the C: Drive added as the highest element above standard programs */
        WritePrivateProfileString("Shortcut", "00000009", "C: Drive|C:\\||0|2|0|", g_szIniPath);
        
        UpdateIconInCache(1, NULL, NULL, TRUE); 
        UpdateIconInCache(2, NULL, NULL, TRUE); 
        UpdateIconInCache(4, NULL, NULL, TRUE);
        UpdateIconInCache(9, NULL, NULL, TRUE);
        
        GetPrivateProfileString("Shortcut", NULL, "", keys, sizeof(keys), g_szIniPath);
    }
    pKey = keys;
    while (*pKey) {
        char val[512];
        if (g_IniShortcutCount < MAX_INI_SHORTCUTS) {
            GetPrivateProfileString("Shortcut", pKey, "", val, sizeof(val), g_szIniPath);
            ParseIniEntry(pKey, val, &g_IniShortcuts[g_IniShortcutCount]); g_IniShortcutCount++;
        }
        pKey += lstrlen(pKey) + 1;
    }
    qsort(g_IniShortcuts, g_IniShortcutCount, sizeof(IniShortcut), CompareIni);
}
static void RunStartupItems(void) {
    int i, j; char startupId[16] = "";
    for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmpi(g_IniShortcuts[i].name, "Startup") == 0) { lstrcpy(startupId, g_IniShortcuts[i].id); break; } }
    if (startupId[0] != '\0') {
        for (j = 0; j < g_IniShortcutCount; j++) {
            if (lstrcmp(g_IniShortcuts[j].parentId, startupId) == 0 && !g_IniShortcuts[j].isFolder) {
                ShellExecute(NULL, "open", g_IniShortcuts[j].exe, g_IniShortcuts[j].params, NULL, g_IniShortcuts[j].minimized ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
            }
        }
    }
}

static void LoadConfig(void) {
    int i; HDC hdc;
    GetAppFilePath("taskbar.ini", g_szIniPath); GetAppFilePath("ICACHE.BMP", g_szCachePath);
    g_TbPosition = GetPrivateProfileInt("Taskbar", "Position", POS_BOTTOM, g_szIniPath);
    g_TbHeight = GetPrivateProfileInt("Taskbar", "Height", 30, g_szIniPath);
    g_TbWidthVert = GetPrivateProfileInt("Taskbar", "WidthVert", 72, g_szIniPath);
    if (g_TbHeight < 24) g_TbHeight = 24; if (g_TbWidthVert < 48) g_TbWidthVert = 48;
    GetPrivateProfileString("Paths", "SearchExe", "winfile.exe", g_szSearchExe, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("Paths", "HelpExe", "winhelp.exe", g_szHelpExe, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("Paths", "TaskMgrExe", "taskmgr.exe", g_szTaskMgrExe, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("WinX", "ControlPanel", "control.exe", g_szWinXCP, MAX_PATH, g_szIniPath);
    GetPrivateProfileString("WinX", "DeviceManager", "control.exe", g_szWinXDevMan, MAX_PATH, g_szIniPath);
    g_QLActiveCount = GetPrivateProfileInt("QuickLaunch", "Enabled", 0, g_szIniPath) ? GetPrivateProfileInt("QuickLaunch", "Count", QUICK_LAUNCH_COUNT, g_szIniPath) : 0;
    if (g_QLActiveCount > QUICK_LAUNCH_COUNT) g_QLActiveCount = QUICK_LAUNCH_COUNT;
    hdc = GetDC(NULL); g_hCacheBitmap = Load16ColorBMP(g_szCachePath, hdc); ReleaseDC(NULL, hdc);
    if (g_hCacheBitmap) { BITMAP bm; GetObject(g_hCacheBitmap, sizeof(bm), &bm); g_MaxCacheId = (bm.bmWidth / 32) - 1; if (g_MaxCacheId < 0) g_MaxCacheId = 0; } else g_MaxCacheId = 0;
    for (i = 0; i < g_QLActiveCount; i++) {
        char keyN[16], keyE[16]; sprintf(keyN, "Name%d", i); sprintf(keyE, "Exe%d", i);
        GetPrivateProfileString("QuickLaunch", keyN, "", g_QL[i].name, 16, g_szIniPath); GetPrivateProfileString("QuickLaunch", keyE, "", g_QL[i].exe, MAX_PATH, g_szIniPath);
    }
    LoadIniShortcuts();
}

static void SaveConfig(void) {
    char buf[16]; sprintf(buf, "%d", g_TbPosition); WritePrivateProfileString("Taskbar", "Position", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbHeight); WritePrivateProfileString("Taskbar", "Height", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbWidthVert); WritePrivateProfileString("Taskbar", "WidthVert", buf, g_szIniPath);
}

static void SaveQuickLaunchItem(int i) {
    char keyN[16], keyE[16]; sprintf(keyN, "Name%d", i); sprintf(keyE, "Exe%d", i);
    WritePrivateProfileString("QuickLaunch", keyN, g_QL[i].name, g_szIniPath); WritePrivateProfileString("QuickLaunch", keyE, g_QL[i].exe, g_szIniPath);
}

/* ──────────────────────────────────────────────────────────────────────────
   Start Menu Editor Dialogs
   ────────────────────────────────────────────────────────────────────────── */
LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hName, hTarget, hParams, hIcon, hMinCheck, hFolderCheck, hMapDirCheck, hParentFolder, hHotkey, hTargetLbl;
    switch(msg) {
        case WM_CREATE: {
            char name[MAX_PATH] = "", target[MAX_PATH] = "", params[MAX_PATH] = "", iconF[MAX_PATH] = "", parentId[16] = "0"; 
            int minimized = 0, isFolder = 0, bMapDir = 0, vkCode = 0, i;
            
            if (g_EditShortcutId[0] != '\0') {
                for (i = 0; i < g_IniShortcutCount; i++) {
                    if (lstrcmp(g_IniShortcuts[i].id, g_EditShortcutId) == 0) {
                        lstrcpy(name, g_IniShortcuts[i].name); lstrcpy(target, g_IniShortcuts[i].exe); lstrcpy(params, g_IniShortcuts[i].params); lstrcpy(iconF, g_IniShortcuts[i].icon);
                        lstrcpy(parentId, g_IniShortcuts[i].parentId); 
                        vkCode = atoi(g_IniShortcuts[i].hotkey);
                        minimized = g_IniShortcuts[i].minimized; 
                        isFolder = g_IniShortcuts[i].isFolder; 
                        bMapDir = (isFolder && target[0] != '\0');
                        break; 
                    } 
                }
            } else if (g_ContextId[0] != '\0') {
                lstrcpy(parentId, g_ContextId);
            }

            CreateWindow("STATIC", "Name:", WS_CHILD|WS_VISIBLE, 10, 10, 100, 20, hwnd, NULL, g_hInst, NULL);
            hName = CreateWindowEx(0, "EDIT", name, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 10, 210, 22, hwnd, NULL, g_hInst, NULL);
            
            hTargetLbl = CreateWindow("STATIC", isFolder ? (bMapDir ? "Dir Path:" : "Target:") : "Target (File):", WS_CHILD|WS_VISIBLE, 10, 40, 100, 20, hwnd, NULL, g_hInst, NULL);
            hTarget = CreateWindowEx(0, "EDIT", target, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 40, 150, 22, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 270, 40, 50, 22, hwnd, (HMENU)101, g_hInst, NULL);
            
            CreateWindow("STATIC", "Parameters:", WS_CHILD|WS_VISIBLE, 10, 70, 100, 20, hwnd, NULL, g_hInst, NULL);
            hParams = CreateWindowEx(0, "EDIT", params, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 70, 210, 22, hwnd, NULL, g_hInst, NULL);
            
            CreateWindow("STATIC", "Icon File:", WS_CHILD|WS_VISIBLE, 10, 100, 100, 20, hwnd, NULL, g_hInst, NULL);
            hIcon = CreateWindowEx(0, "EDIT", iconF, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 100, 150, 22, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 270, 100, 50, 22, hwnd, (HMENU)102, g_hInst, NULL);
            
            CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 130, 100, 20, hwnd, NULL, g_hInst, NULL);
            hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL, 110, 130, 210, 150, hwnd, NULL, g_hInst, NULL);
            
            CreateWindow("STATIC", "Hotkey (press key):", WS_CHILD|WS_VISIBLE, 10, 160, 100, 20, hwnd, NULL, g_hInst, NULL);
            hHotkey = CreateWindowEx(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 160, 80, 22, hwnd, NULL, g_hInst, NULL);
            OldHotkeyProc = (FARPROC)SetWindowLong(hHotkey, GWL_WNDPROC, (LONG)g_lpfnHotkeyProc);
            
            if (vkCode) {
                char kname[64];
                UINT scanCode = MapVirtualKey(vkCode, 0);
                SetProp(hHotkey, "VK", (HANDLE)vkCode);
                /* MAKELONG safely places scanCode in the high word without generating shift warnings */
                GetKeyNameText(MAKELONG(0, scanCode), kname, sizeof(kname));
                SetWindowText(hHotkey, kname);
            }

            hMinCheck = CreateWindow("BUTTON", "Start Minimized", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 110, 190, 120, 20, hwnd, NULL, g_hInst, NULL);
            hFolderCheck = CreateWindow("BUTTON", "Is Folder", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 240, 190, 80, 20, hwnd, (HMENU)103, g_hInst, NULL);
            hMapDirCheck = CreateWindow("BUTTON", "Map Dir to Menu", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 110, 215, 140, 20, hwnd, (HMENU)104, g_hInst, NULL);
            
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 80, 250, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 180, 250, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            
            SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)"0 (Root)");
            for (i = 0; i < g_IniShortcutCount; i++) {
                if (g_IniShortcuts[i].isFolder) {
                    char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i].id, g_IniShortcuts[i].name);
                    SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)buf);
                }
            }
            
            for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) {
                char buf[128]; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)buf);
                if (strncmp(buf, parentId, lstrlen(parentId)) == 0 && buf[lstrlen(parentId)] == ' ') {
                    SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break;
                }
            }
            if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0);

            if (minimized) SendMessage(hMinCheck, BM_SETCHECK, 1, 0); 
            SendMessage(hFolderCheck, BM_SETCHECK, isFolder, 0);
            SendMessage(hMapDirCheck, BM_SETCHECK, bMapDir, 0);
            
            if (isFolder) {
                EnableWindow(hParams, FALSE);
                EnableWindow(hMinCheck, FALSE);
                EnableWindow(hHotkey, FALSE);
                EnableWindow(hMapDirCheck, TRUE);
                if (bMapDir) {
                    EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                } else {
                    EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE);
                }
            } else {
                EnableWindow(hMapDirCheck, FALSE);
            }

            SetFont(hwnd, NULL); SetFocus(hName); PositionDialogNearStart(hwnd); return 0;
        }
        case WM_COMMAND:
            if (wp == 101) { 
                char path[MAX_PATH]; 
                if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) {
                    MessageBox(hwnd, "Please select any file inside the directory you wish to map.", "Select Directory", MB_OK|MB_ICONINFORMATION);
                    if (BrowseFile(hwnd, path, "All Files (*.*)\0*.*\0")) {
                        char* pDir = strrchr(path, '\\');
                        if (pDir) {
                            if (pDir == path || *(pDir - 1) == ':') *(pDir + 1) = '\0'; 
                            else *pDir = '\0';
                        }
                        SetWindowText(hTarget, path);
                    }
                } else {
                    if (BrowseFile(hwnd, path, "Programs (*.exe;*.com;*.pif;*.bat)\0*.exe;*.com;*.pif;*.bat\0All Files (*.*)\0*.*\0")) { 
                        SetWindowText(hTarget, path); 
                        if (GetWindowTextLength(hIcon) == 0) SetWindowText(hIcon, path); 
                        if (GetWindowTextLength(hName) == 0) {
                            char base[MAX_PATH], *p, *dot;
                            lstrcpy(base, path);
                            p = strrchr(base, '\\');
                            if (p) lstrcpy(base, p + 1);
                            dot = strrchr(base, '.');
                            if (dot) *dot = '\0';
                            if (base[0]) {
                                AnsiLower((LPSTR)base);
                                if (base[0] >= 'a' && base[0] <= 'z') base[0] -= 32; /* Convert first character to uppercase */
                            }
                            SetWindowText(hName, base);
                        }
                    } 
                }
            } 
            else if (wp == 102) { char path[MAX_PATH]; if (BrowseFile(hwnd, path, "Icons (*.exe;*.ico;*.dll)\0*.exe;*.ico;*.dll\0All Files (*.*)\0*.*\0")) SetWindowText(hIcon, path); } 
            else if (wp == 103) {
                if (SendMessage(hFolderCheck, BM_GETCHECK, 0, 0)) {
                    EnableWindow(hParams, FALSE); SetWindowText(hParams, "");
                    EnableWindow(hMinCheck, FALSE); SendMessage(hMinCheck, BM_SETCHECK, 0, 0);
                    EnableWindow(hHotkey, FALSE); SetWindowText(hHotkey, ""); RemoveProp(hHotkey, "VK");
                    EnableWindow(hMapDirCheck, TRUE);
                    if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) {
                        SetWindowText(hTargetLbl, "Dir Path:");
                        EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                    } else {
                        SetWindowText(hTargetLbl, "Target:");
                        EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE);
                    }
                } else {
                    EnableWindow(hMapDirCheck, FALSE); SendMessage(hMapDirCheck, BM_SETCHECK, 0, 0);
                    SetWindowText(hTargetLbl, "Target (File):");
                    EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                    EnableWindow(hParams, TRUE);
                    EnableWindow(hMinCheck, TRUE);
                    EnableWindow(hHotkey, TRUE);
                }
            }
            else if (wp == 104) {
                if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) {
                    SetWindowText(hTargetLbl, "Dir Path:");
                    EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                } else {
                    SetWindowText(hTargetLbl, "Target:");
                    SetWindowText(hTarget, "");
                    EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE);
                }
            }
            else if (wp == IDOK) {
                char name[MAX_PATH], target[MAX_PATH], params[MAX_PATH], iconF[MAX_PATH], parentSel[128], *space;
                int vkCode = (int)GetProp(hHotkey, "VK");
                GetWindowText(hName, name, MAX_PATH); GetWindowText(hTarget, target, MAX_PATH); GetWindowText(hParams, params, MAX_PATH); GetWindowText(hIcon, iconF, MAX_PATH);
                GetWindowText(hParentFolder, parentSel, sizeof(parentSel));
                space = strchr(parentSel, ' '); if (space) *space = '\0';

                if (name[0] && (target[0] || SendMessage(hFolderCheck, BM_GETCHECK, 0, 0))) {
                    IniShortcut sh;
                    if (g_EditShortcutId[0] != '\0') {
                        int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].id, g_EditShortcutId) == 0) { sh = g_IniShortcuts[i]; break; } }
                    } else {
                        char newId[16]; GetNewIniId(newId); lstrcpy(sh.id, newId);
                    }
                    lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0");
                    lstrcpy(sh.name, name); lstrcpy(sh.exe, target); lstrcpy(sh.params, params); lstrcpy(sh.icon, iconF);
                    if (vkCode) sprintf(sh.hotkey, "%d", vkCode); else sh.hotkey[0] = '\0';
                    
                    sh.minimized = SendMessage(hMinCheck, BM_GETCHECK, 0, 0) ? 1 : 0; 
                    sh.isFolder = SendMessage(hFolderCheck, BM_GETCHECK, 0, 0) ? 1 : 0;
                    
                    if (sh.isFolder) { 
                        sh.params[0] = '\0'; sh.hotkey[0] = '\0';
                        if (!SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) sh.exe[0] = '\0';
                    }
                    
                    SaveIniEntry(&sh); UpdateIconInCache(atoi(sh.id), sh.icon, sh.exe, sh.isFolder); LoadIniShortcuts();
                }
                g_EditShortcutId[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); ShowWindow(hwnd, SW_HIDE); DestroyWindow(hwnd);
            } else if (wp == IDCANCEL) { g_EditShortcutId[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); ShowWindow(hwnd, SW_HIDE); DestroyWindow(hwnd); }
            return 0;
        case WM_CLOSE: g_EditShortcutId[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); ShowWindow(hwnd, SW_HIDE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}/* ──────────────────────────────────────────────────────────────────────────
   Owner-Drawn Win95-Style Start Menu Engine
   ────────────────────────────────────────────────────────────────────────── */
static void ClearODIcons(void) {
    int i; for (i = 0; i < g_ODCount; i++) { if (g_ODItems[i].bDestroyIcon && g_ODItems[i].hIcon) DestroyIcon(g_ODItems[i].hIcon); }
    g_ODCount = 0;
}

static ODMenuItem* AddODItem(const char* text, const char* itemIdStr, const char* fallbackExe, BOOL isRoot, BOOL isSeparator, int polyIcon) {
    ODMenuItem* item;
    if (g_ODCount >= MAX_OD_ITEMS) return NULL;
    item = &g_ODItems[g_ODCount++]; memset(item, 0, sizeof(ODMenuItem));
    if (text) lstrcpyn(item->text, text, 63);
    item->isRoot = isRoot; item->isSeparator = isSeparator; item->cacheIndex = -1; item->hIcon = NULL; item->polyIcon = polyIcon;
    item->cmdId = IDM_FS_BASE + g_ODCount;
    if (!isSeparator && polyIcon < 0) {
        if (itemIdStr && itemIdStr[0]) item->cacheIndex = atoi(itemIdStr);
        else if (fallbackExe && fallbackExe[0]) { item->hIcon = ExtractIcon(g_hInst, fallbackExe, 0); if ((int)item->hIcon <= 1) item->hIcon = LoadIcon(NULL, IDI_APPLICATION); } 
        else item->hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    return item;
}

static void BuildMenuFromIni(HMENU hMenu, const char* parentId, BOOL isRoot) {
    int i;
    for (i = 0; i < g_IniShortcutCount; i++) {
        if (lstrcmp(g_IniShortcuts[i].parentId, parentId) == 0) {
            BOOL isSep = (lstrcmp(g_IniShortcuts[i].name, "-") == 0);
            int poly = -1;
            
            if (g_IniShortcuts[i].icon[0] >= '0' && g_IniShortcuts[i].icon[0] <= '9' && g_IniShortcuts[i].icon[1] == '\0') {
                poly = g_IniShortcuts[i].icon[0] - '0';
            } else if (g_IniShortcuts[i].isFolder) {
                poly = 0; /* Folder icon */
            }
            
            if (isSep) {
                ODMenuItem* pItem = AddODItem("", NULL, NULL, isRoot, TRUE, -1);
                if (pItem) AppendMenu(hMenu, MF_OWNERDRAW, 0, (LPSTR)pItem);
            } else if (g_IniShortcuts[i].isFolder) {
                HMENU hSub = CreatePopupMenu();
                ODMenuItem* pItem = AddODItem(g_IniShortcuts[i].name, g_IniShortcuts[i].id, NULL, isRoot, FALSE, poly);
                g_IniShortcuts[i].hMenu = hSub;
                
                if (g_IniShortcuts[i].exe[0] != '\0') {
                    BuildMenuFromFileSystemFolder(hSub, g_IniShortcuts[i].exe);
                } else {
                    BuildMenuFromIni(hSub, g_IniShortcuts[i].id, FALSE);
                }
                
                if (pItem) AppendMenu(hMenu, MF_OWNERDRAW | MF_POPUP, (UINT)hSub, (LPSTR)pItem);
            } else {
                ODMenuItem* pItem = AddODItem(g_IniShortcuts[i].name, g_IniShortcuts[i].id, NULL, isRoot, FALSE, poly);
                if (pItem) AppendMenu(hMenu, MF_OWNERDRAW | MF_STRING, IDM_START_BASE + i, (LPSTR)pItem);
            }
        }
    }
}

static void ShowStartMenu(HWND hBtn) {
    HMENU hMenu = CreatePopupMenu(); POINT pt; RECT rc; int i, curY = 0;
    ClearODIcons();
    BuildMenuFromIni(hMenu, "0", TRUE);
    for (i = 0; i < g_ODCount; i++) { if (g_ODItems[i].isRoot) { g_ODItems[i].yOffset = curY; curY += g_ODItems[i].isSeparator ? 8 : 36; } }
    for (i = 0; i < g_ODCount; i++) { if (g_ODItems[i].isRoot) g_ODItems[i].totalHeight = curY; }

    GetWindowRect(hBtn, &rc);
    if (g_TbPosition == POS_BOTTOM) { pt.x = rc.left; pt.y = rc.top; } 
    else if (g_TbPosition == POS_TOP) { pt.x = rc.left; pt.y = rc.bottom; } 
    else if (g_TbPosition == POS_LEFT) { pt.x = rc.right; pt.y = rc.top; } 
    else { pt.x = rc.left; pt.y = rc.top; }

    g_ContextId[0] = '\0'; g_ContextIsFolder = FALSE;
    g_lpfnMsgFilter = MakeProcInstance((FARPROC)MsgFilterProc, g_hInst);
    g_hMsgHook = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)g_lpfnMsgFilter, g_hInst, GetCurrentTask());
    
    g_MenuOpen = TRUE;
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL);
    g_MenuOpen = FALSE;
    
    UnhookWindowsHookEx(g_hMsgHook); FreeProcInstance(g_lpfnMsgFilter); DestroyMenu(hMenu);
}

/* ──────────────────────────────────────────────────────────────────────────
   Subclass Procedures
   ────────────────────────────────────────────────────────────────────────── */
static void AddTrayIcon(HICON hIcon, const char* tooltip) {
    if (g_TrayIconCount < MAX_TRAY_ICONS) { g_TrayIcons[g_TrayIconCount].hIcon = hIcon; lstrcpyn(g_TrayIcons[g_TrayIconCount].tooltip, tooltip, 31); g_TrayIconCount++; if (g_hTrayArea) InvalidateRect(g_hTrayArea, NULL, TRUE); }
}

LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_DOWN || wp == VK_UP) { SetFocus(g_hSearchList); SendMessage(g_hSearchList, WM_KEYDOWN, wp, lp); return 0; }
        if (wp == VK_RETURN) { PostMessage(g_hTaskbar, WM_COMMAND, IDM_SEARCH_LIST, MAKELONG(g_hSearchList, LBN_DBLCLK)); return 0; }
        if (wp == VK_ESCAPE) { ShowWindow(g_hSearchList, SW_HIDE); SetWindowText(hwnd, ""); return 0; }
    }
    if (msg == WM_KEYUP && wp != VK_DOWN && wp != VK_UP && wp != VK_RETURN && wp != VK_ESCAPE) {
        KillTimer(hwnd, TIMER_SEARCH); SetTimer(hwnd, TIMER_SEARCH, 250, NULL);
    }
    if (msg == WM_TIMER && wp == TIMER_SEARCH) {
        KillTimer(hwnd, TIMER_SEARCH);
        {
            char buf[64]; GetWindowText(hwnd, buf, sizeof(buf)); SendMessage(g_hSearchList, LB_RESETCONTENT, 0, 0);
            if (buf[0]) {
                int i;
                char bufUpper[64]; lstrcpy(bufUpper, buf); AnsiUpper((LPSTR)bufUpper);
                for (i=0; i<g_IniShortcutCount; i++) {
                    /* Allow searching for everything (including folders like "Programs"), excluding separators */
                    if (lstrcmp(g_IniShortcuts[i].name, "-") != 0) {
                        char nameUpper[64]; lstrcpy(nameUpper, g_IniShortcuts[i].name); AnsiUpper((LPSTR)nameUpper);
                        if (strstr(nameUpper, bufUpper)) {
                            int pos = SendMessage(g_hSearchList, LB_ADDSTRING, 0, (LPARAM)g_IniShortcuts[i].name);
                            SendMessage(g_hSearchList, LB_SETITEMDATA, pos, i);
                        }
                    }
                }
                if (SendMessage(g_hSearchList, LB_GETCOUNT, 0, 0) > 0) {
                    RECT rc; int lbH; GetWindowRect(hwnd, &rc);
                    lbH = SendMessage(g_hSearchList, LB_GETCOUNT, 0, 0) * 16 + 2; if (lbH > 200) lbH = 200;
                    SendMessage(g_hSearchList, LB_SETCURSEL, 0, 0);
                    
                    if (g_TbPosition == POS_BOTTOM) {
                        SetWindowPos(g_hSearchList, HWND_TOPMOST, rc.left, rc.top - lbH, 150, lbH, SWP_SHOWWINDOW);
                    } else if (g_TbPosition == POS_TOP) {
                        SetWindowPos(g_hSearchList, HWND_TOPMOST, rc.left, rc.bottom, 150, lbH, SWP_SHOWWINDOW);
                    } else {
                        SetWindowPos(g_hSearchList, HWND_TOPMOST, rc.right, rc.top, 150, lbH, SWP_SHOWWINDOW);
                    }
                } else ShowWindow(g_hSearchList, SW_HIDE);
            } else ShowWindow(g_hSearchList, SW_HIDE);
        }
        return 0; 
    }
    if (msg == WM_KILLFOCUS && (HWND)wp != g_hSearchList) ShowWindow(g_hSearchList, SW_HIDE);
    return CallWindowProc((FARPROC)OldSearchBoxProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK SearchListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { 
        /* FIXED parameter order */
        PostMessage(g_hTaskbar, WM_COMMAND, IDM_SEARCH_LIST, MAKELONG(hwnd, LBN_DBLCLK)); 
        return 0; 
    }
    if (msg == WM_LBUTTONUP) {
        RECT rc; POINT pt;
        LRESULT ret = CallWindowProc((FARPROC)OldSearchListProc, hwnd, msg, wp, lp);
        GetClientRect(hwnd, &rc);
        pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
        /* Ensure the user clicked inside the listbox items, not on the scrollbar */
        if (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom) {
            /* FIXED parameter order */
            PostMessage(g_hTaskbar, WM_COMMAND, IDM_SEARCH_LIST, MAKELONG(hwnd, LBN_DBLCLK));
        }
        return ret;
    }
    if (msg == WM_LBUTTONDBLCLK) {
        LRESULT ret = CallWindowProc((FARPROC)OldSearchListProc, hwnd, msg, wp, lp);
        PostMessage(g_hTaskbar, WM_COMMAND, IDM_SEARCH_LIST, MAKELONG(hwnd, LBN_DBLCLK));
        return ret;
    }
    if (msg == WM_KILLFOCUS && (HWND)wp != g_hSearchBox) ShowWindow(hwnd, SW_HIDE);
    return CallWindowProc((FARPROC)OldSearchListProc, hwnd, msg, wp, lp);
}
LRESULT CALLBACK MsgFilterProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == MSGF_MENU) {
        MSG FAR* pMsg = (MSG FAR*)lParam;
        if (pMsg->message == WM_RBUTTONUP) { PostMessage(g_hTaskbar, WM_USER_CONTEXTMENU, 0, MAKELONG(pMsg->pt.x, pMsg->pt.y)); SendMessage(pMsg->hwnd, WM_CANCELMODE, 0, 0); return 1; }
    }
    return CallNextHookEx(g_hMsgHook, code, wParam, lParam);
}

LRESULT CALLBACK ClockProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_RBUTTONUP) {
        HMENU hMenu = CreatePopupMenu(); POINT pt;
        pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); ClientToScreen(hwnd, &pt);
        AppendMenu(hMenu, MF_STRING, IDM_CLOCK_ADJUST, "Adjust Date/Time");
        g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL); g_MenuOpen = FALSE;
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
            g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL); g_MenuOpen = FALSE;
            DestroyMenu(hMenu);
        }
        return 0;
    }
    return CallWindowProc((FARPROC)OldTaskBtnProc, hwnd, msg, wParam, lParam);
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
    if (msg == WM_LBUTTONUP) {
        int x = LOWORD(lParam);
        int idx = (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP) ? (x / 24) : (HIWORD(lParam) / 24);
        if (idx == 0) WinExec("sndvol32.exe", SW_SHOWNORMAL);
        return 0;
    }
    return CallWindowProc((FARPROC)OldTrayAreaProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK StartBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; HBRUSH hBlue, hRed, hGreen, hYellow, hOldBrush, hGray; HPEN hBlack, hOldPen, hShadow, hHighlight; BOOL isPushed;
        GetClientRect(hwnd, &rc); isPushed = (SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
        hGray = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &rc, hGray); DeleteObject(hGray);
        
        hShadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)); 
        hHighlight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
        
        if (isPushed) {
            hOldPen = SelectObject(hdc, hShadow);
            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.left, rc.top); LineTo(hdc, rc.right - 1, rc.top);
            SelectObject(hdc, hHighlight);
            LineTo(hdc, rc.right - 1, rc.bottom - 1); LineTo(hdc, rc.left, rc.bottom - 1);
            SelectObject(hdc, hOldPen); 
        } else {
            hOldPen = SelectObject(hdc, hHighlight); 
            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.left, rc.top); LineTo(hdc, rc.right - 1, rc.top);
            SelectObject(hdc, hShadow); 
            LineTo(hdc, rc.right - 1, rc.bottom - 1); LineTo(hdc, rc.left, rc.bottom - 1);
            SelectObject(hdc, hOldPen); 
        }
        DeleteObject(hShadow); DeleteObject(hHighlight);
        
        {
            int lx = 6 + (isPushed ? 1 : 0); int ly = ((rc.bottom - rc.top - 16) / 2) + (isPushed ? 1 : 0); int half = 8;
            hBlue = CreateSolidBrush(RGB(0, 0, 255)); hRed = CreateSolidBrush(RGB(255, 0, 0)); hGreen = CreateSolidBrush(RGB(0, 128, 0)); hYellow = CreateSolidBrush(RGB(255, 255, 0)); hBlack = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            hOldPen = SelectObject(hdc, hBlack);
            hOldBrush = SelectObject(hdc, hBlue); Rectangle(hdc, lx, ly, lx + half + 1, ly + half + 1);
            SelectObject(hdc, hRed); Rectangle(hdc, lx + half, ly, lx + (half*2) + 1, ly + half + 1);
            SelectObject(hdc, hGreen); Rectangle(hdc, lx, ly + half, lx + half + 1, ly + (half*2) + 1);
            SelectObject(hdc, hYellow); Rectangle(hdc, lx + half, ly + half, lx + (half*2) + 1, ly + (half*2) + 1);
            SelectObject(hdc, hOldBrush); SelectObject(hdc, hOldPen);
            DeleteObject(hBlue); DeleteObject(hRed); DeleteObject(hGreen); DeleteObject(hYellow); DeleteObject(hBlack);
            { HFONT hOldFont = SelectObject(hdc, g_hFontMenu); SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT)); TextOut(hdc, lx + 22, ly + 1, "Start", 5); SelectObject(hdc, hOldFont); }
        }
        EndPaint(hwnd, &ps); 
        return 0; /* Returning 0 prevents default button painting during paint cycle */
    }
    
    /* Intercept mouse clicks and state changes so standard Windows drawing is overwritten immediately */
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK || msg == BM_SETSTATE) {
        LRESULT res = CallWindowProc((FARPROC)OldStartBtnProc, hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        return res;
    }
    
    if (msg == WM_RBUTTONUP) {
        HMENU hMenu = CreatePopupMenu(); POINT pt;
        pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); ClientToScreen(hwnd, &pt);
        AppendMenu(hMenu, MF_STRING, IDM_WINX_NEWFOLDER, "New Folder");
        AppendMenu(hMenu, MF_STRING, IDM_WINX_SHORTCUT, "New Shortcut");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, IDM_WINX_CP, "Control Panel");
        AppendMenu(hMenu, MF_STRING, IDM_WINX_DEVMAN, "Device Manager");
        g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL); g_MenuOpen = FALSE;
        DestroyMenu(hMenu); 
        return 0;
    }
    return CallWindowProc((FARPROC)OldStartBtnProc, hwnd, msg, wParam, lParam);
}
/* ──────────────────────────────────────────────────────────────────────────
   Window Layout and Enum
   ────────────────────────────────────────────────────────────────────────── */
BOOL CALLBACK TaskbarEnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        char cls[64]; GetClassName(hwnd, cls, sizeof(cls));
        if (lstrcmp(cls, "CalmiraTaskbarClass") != 0 && lstrcmp(cls, "Progman") != 0 && lstrcmp(cls, "RunDlgClass") != 0 && lstrcmp(cls, "ShortcutDlgClass") != 0 && lstrcmp(cls, "PromptDlgClass") != 0) {
            if (g_TaskCount < MAX_TASKS) {
                char title[128]; GetWindowText(hwnd, title, sizeof(title));
                if (lstrlen(title) > 0) { g_Tasks[g_TaskCount].hWnd = hwnd; lstrcpy(g_Tasks[g_TaskCount].title, title); g_Tasks[g_TaskCount].hBtn = NULL; g_TaskCount++; }
            }
        }
    }
    return TRUE;
}

static void RefreshTasks(void) {
    int i; RECT rc; int isHorz = (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP);
    for (i = 0; i < g_TaskCount; i++) { if (g_Tasks[i].hBtn) DestroyWindow(g_Tasks[i].hBtn); }
    g_TaskCount = 0;
    EnumWindows((WNDENUMPROC)g_lpfnEnumWindowsProc, 0);
    GetClientRect(g_hTaskList, &rc);
    
    if (g_TaskCount > 0) {
        int containerW = rc.right - rc.left; int containerH = rc.bottom - rc.top;

        if (isHorz && g_TbHeight > 60 && containerH >= 48) {
            int rowHeight = 24; int numRows = containerH / rowHeight; if (numRows < 1) numRows = 1;
            int cols = (g_TaskCount + numRows - 1) / numRows; if (cols < 1) cols = 1;
            int btnWidth = containerW / cols; if (btnWidth > 160) btnWidth = 160; if (btnWidth < 40) btnWidth = 40;

            for (i = 0; i < g_TaskCount; i++) {
                int r = i / cols; int c = i % cols; char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 28);
                g_Tasks[i].hBtn = CreateWindow("BUTTON", shortTitle, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, c * btnWidth, r * rowHeight, btnWidth - 2, rowHeight - 2, g_hTaskList, (HMENU)(ID_TASK_BASE + i), g_hInst, NULL);
                SetFont(g_Tasks[i].hBtn, NULL);
                if (!OldTaskBtnProc) OldTaskBtnProc = (FARPROC)SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc); else SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
            }
        } else if (isHorz) {
            int btnWidth = containerW / g_TaskCount; if (btnWidth > 160) btnWidth = 160;
            for (i = 0; i < g_TaskCount; i++) {
                char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 28);
                g_Tasks[i].hBtn = CreateWindow("BUTTON", shortTitle, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, i * btnWidth, 0, btnWidth - 2, containerH - 2, g_hTaskList, (HMENU)(ID_TASK_BASE + i), g_hInst, NULL);
                SetFont(g_Tasks[i].hBtn, NULL);
                if (!OldTaskBtnProc) OldTaskBtnProc = (FARPROC)SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc); else SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
            }
        } else {
            for (i = 0; i < g_TaskCount; i++) {
                char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 10);
                g_Tasks[i].hBtn = CreateWindow("BUTTON", shortTitle, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, i * 26, containerW, 24, g_hTaskList, (HMENU)(ID_TASK_BASE + i), g_hInst, NULL);
                SetFont(g_Tasks[i].hBtn, NULL);
                if (!OldTaskBtnProc) OldTaskBtnProc = (FARPROC)SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc); else SetWindowLong(g_Tasks[i].hBtn, GWL_WNDPROC, (LONG)g_lpfnTaskBtnProc);
            }
        }
    }
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
    if (IsWindowVisible(hwnd) && !IsIconic(hwnd) && IsWindowEnabled(hwnd)) {
        char cls[64]; GetClassName(hwnd, cls, sizeof(cls));
        if (lstrcmp(cls, "CalmiraTaskbarClass") != 0 && lstrcmp(cls, "Progman") != 0 && lstrcmp(cls, "RunDlgClass") != 0 && lstrcmp(cls, "ShortcutDlgClass") != 0 && lstrcmp(cls, "PromptDlgClass") != 0) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
    }
    return TRUE;
}

static void DoShowDesktop(void) { FARPROC lpfn = MakeProcInstance((FARPROC)MinimizeEnumProc, g_hInst); EnumWindows((WNDENUMPROC)lpfn, 0); FreeProcInstance(lpfn); }

static void ApplyLayout(void) {
    int cx = GetSystemMetrics(SM_CXSCREEN); int cy = GetSystemMetrics(SM_CYSCREEN);
    int i; int trayW = (g_TrayIconCount > 0 ? (g_TrayIconCount * 24) + 4 : 0);
    int qlCount = g_QLActiveCount; int qlW = qlCount * 40;

    switch (g_TbPosition) {
        case POS_BOTTOM: MoveWindow(g_hTaskbar, 0, cy - g_TbHeight, cx, g_TbHeight, TRUE); break;
        case POS_TOP:    MoveWindow(g_hTaskbar, 0, 0, cx, g_TbHeight, TRUE); break;
        case POS_LEFT:   MoveWindow(g_hTaskbar, 0, 0, g_TbWidthVert, cy, TRUE); break;
        case POS_RIGHT:  MoveWindow(g_hTaskbar, cx - g_TbWidthVert, 0, g_TbWidthVert, cy, TRUE); break;
    }

    if (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP) {
        int tlX = 6 + START_BTN_WIDTH + 6; int tlW, yOff = (g_TbHeight - 22) / 2;
        MoveWindow(g_hStartBtn, 4, yOff, START_BTN_WIDTH, 22, TRUE);
        
        if (qlCount > 0) {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) {
                if (i < qlCount) { MoveWindow(g_hQuickLaunch[i], tlX + i * 40, yOff, 38, 22, TRUE); ShowWindow(g_hQuickLaunch[i], SW_SHOW); } 
                else ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            }
            if (g_hSearchBox) ShowWindow(g_hSearchBox, SW_HIDE);
            tlX += qlW + 6;
        } else {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            if (g_hSearchBox) { MoveWindow(g_hSearchBox, tlX, yOff, 120, 22, TRUE); ShowWindow(g_hSearchBox, SW_SHOW); tlX += 120 + 6; }
        }
        
        tlW = cx - CLOCK_WIDTH - trayW - tlX - 8; if (tlW < 50) tlW = 50;
        MoveWindow(g_hTaskList, tlX, 3, tlW, g_TbHeight - 6, TRUE);
        if (trayW > 0) MoveWindow(g_hTrayArea, cx - CLOCK_WIDTH - trayW - 6, yOff, trayW, 22, TRUE); else MoveWindow(g_hTrayArea, 0, 0, 0, 0, FALSE);
        MoveWindow(g_hClock, cx - CLOCK_WIDTH - 4, yOff, CLOCK_WIDTH, 22, TRUE);
    } else {
        int y = 4; int tlH = cy - CLOCK_HEIGHT - (trayW > 0 ? (g_TrayIconCount * 24 + 4) : 0) - y - 10;
        int xOff = (g_TbWidthVert - START_BTN_WIDTH) / 2; if (xOff < 4) xOff = 4;
        
        MoveWindow(g_hStartBtn, xOff, y, START_BTN_WIDTH, 22, TRUE); y += 26;
        if (qlCount > 0) {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) {
                if (i < qlCount) { MoveWindow(g_hQuickLaunch[i], (g_TbWidthVert - 38)/2, y, 38, 22, TRUE); ShowWindow(g_hQuickLaunch[i], SW_SHOW); y += 24; } 
                else ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            }
            if (g_hSearchBox) ShowWindow(g_hSearchBox, SW_HIDE);
        } else {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            if (g_hSearchBox) { MoveWindow(g_hSearchBox, 4, y, g_TbWidthVert - 8, 22, TRUE); ShowWindow(g_hSearchBox, SW_SHOW); y += 26; }
        }
        y += 4;
        MoveWindow(g_hTaskList, 4, y, g_TbWidthVert - 8, tlH, TRUE);
        if (trayW > 0) { int trayH = g_TrayIconCount * 24 + 4; MoveWindow(g_hTrayArea, 4, cy - CLOCK_HEIGHT - trayH - 6, g_TbWidthVert - 8, trayH, TRUE); } else MoveWindow(g_hTrayArea, 0, 0, 0, 0, FALSE);
        MoveWindow(g_hClock, 4, cy - CLOCK_HEIGHT - 4, g_TbWidthVert - 8, CLOCK_HEIGHT, TRUE);
    }
    RefreshTasks();
    if (g_hTrayArea) InvalidateRect(g_hTrayArea, NULL, TRUE);
}

LRESULT CALLBACK TaskListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) {
        SendMessage(GetParent(hwnd), msg, wp, lp);
        return 0;
    }
    return CallWindowProc((FARPROC)OldTaskListProc, hwnd, msg, wp, lp);
}


LRESULT CALLBACK __export TaskbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int i; g_hTaskbar = hwnd; InitFonts();
            g_hMemODItems = GlobalAlloc(GPTR, MAX_OD_ITEMS * sizeof(ODMenuItem)); g_ODItems = (ODMenuItem FAR*)GlobalLock(g_hMemODItems);
            g_hMemIniShortcuts = GlobalAlloc(GPTR, MAX_INI_SHORTCUTS * sizeof(IniShortcut)); g_IniShortcuts = (IniShortcut FAR*)GlobalLock(g_hMemIniShortcuts);
            LoadConfig();

            g_hStartBtn = CreateWindow("BUTTON", "Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_START_BUTTON, g_hInst, NULL);
            SetFont(g_hStartBtn, g_hFontMenu); g_lpfnStartBtnProc = MakeProcInstance((FARPROC)StartBtnProc, g_hInst); OldStartBtnProc = (FARPROC)SetWindowLong(g_hStartBtn, GWL_WNDPROC, (LONG)g_lpfnStartBtnProc);
            
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) g_hQuickLaunch[i] = CreateWindow("BUTTON", "", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)(ID_QUICK_BASE + i), g_hInst, NULL);
            
            if (g_QLActiveCount == 0) {
                g_hSearchBox = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)IDM_SEARCH_EDIT, g_hInst, NULL);
                SetFont(g_hSearchBox, NULL); g_lpfnSearchBoxProc = MakeProcInstance((FARPROC)SearchBoxProc, g_hInst); OldSearchBoxProc = (FARPROC)SetWindowLong(g_hSearchBox, GWL_WNDPROC, (LONG)g_lpfnSearchBoxProc);
                
                g_hSearchList = CreateWindowEx(WS_EX_TOPMOST, "LISTBOX", "", WS_POPUP | WS_BORDER | LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL, 0, 0, 150, 150, hwnd, NULL, g_hInst, NULL);
                SetFont(g_hSearchList, NULL); g_lpfnSearchListProc = MakeProcInstance((FARPROC)SearchListProc, g_hInst); OldSearchListProc = (FARPROC)SetWindowLong(g_hSearchList, GWL_WNDPROC, (LONG)g_lpfnSearchListProc);
            }
            
            g_hTaskList = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)0, g_hInst, NULL);
            g_lpfnTaskListProc = MakeProcInstance((FARPROC)TaskListProc, g_hInst);
            OldTaskListProc = (FARPROC)SetWindowLong(g_hTaskList, GWL_WNDPROC, (LONG)g_lpfnTaskListProc);
            
            g_hClock = CreateWindowEx(0, "STATIC", "00:00", WS_CHILD | WS_VISIBLE | SS_CENTER | WS_BORDER, 0, 0, 0, 0, hwnd, (HMENU)ID_CLOCK, g_hInst, NULL);
            SetFont(g_hClock, NULL); g_lpfnClockProc = MakeProcInstance((FARPROC)ClockProc, g_hInst); OldClockProc = (FARPROC)SetWindowLong(g_hClock, GWL_WNDPROC, (LONG)g_lpfnClockProc);

            g_hTrayArea = CreateWindowEx(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)ID_TRAY_AREA, g_hInst, NULL);
            g_lpfnTrayAreaProc = MakeProcInstance((FARPROC)TrayAreaProc, g_hInst); OldTrayAreaProc = (FARPROC)SetWindowLong(g_hTrayArea, GWL_WNDPROC, (LONG)g_lpfnTrayAreaProc);

            {
                HICON hVol = ExtractIcon(g_hInst, "sndvol32.exe", 0);
                if ((int)hVol <= 1) hVol = LoadIcon(NULL, IDI_APPLICATION);
                AddTrayIcon(hVol, "Volume");
            }

            SetTimer(hwnd, TIMER_CLOCK, 1000, NULL);
            SetTimer(hwnd, TIMER_REFRESH, 2000, NULL);
            SetTimer(hwnd, TIMER_HOTKEY, 100, NULL);
            ApplyLayout(); RunStartupItems(); return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
            if (lpdis->CtlType == ODT_BUTTON && lpdis->CtlID >= ID_QUICK_BASE && lpdis->CtlID < ID_QUICK_BASE + QUICK_LAUNCH_COUNT) {
                int idx = lpdis->CtlID - ID_QUICK_BASE; COLORREF colors[4] = { RGB(255, 60, 60), RGB(60, 255, 60), RGB(60, 60, 255), RGB(255, 255, 60) };
                HBRUSH hBr = CreateSolidBrush(colors[idx % 4]); HBRUSH hOldBr = SelectObject(lpdis->hDC, hBr); HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0)); HPEN hOldPen = SelectObject(lpdis->hDC, hPen);
                FillRect(lpdis->hDC, &lpdis->rcItem, (HBRUSH)(COLOR_BTNFACE + 1));
                if (lpdis->itemState & ODS_SELECTED) Ellipse(lpdis->hDC, lpdis->rcItem.left + 5, lpdis->rcItem.top + 5, lpdis->rcItem.right - 3, lpdis->rcItem.bottom - 3);
                else Ellipse(lpdis->hDC, lpdis->rcItem.left + 4, lpdis->rcItem.top + 4, lpdis->rcItem.right - 4, lpdis->rcItem.bottom - 4);
                SelectObject(lpdis->hDC, hOldPen); DeleteObject(hPen); SelectObject(lpdis->hDC, hOldBr); DeleteObject(hBr); return TRUE;
            }
            if (lpdis->CtlType == ODT_MENU) {
                ODMenuItem FAR* item = (ODMenuItem FAR*)lpdis->itemData; HDC hdc = lpdis->hDC; RECT rc = lpdis->rcItem; HBRUSH hBrush; int textX = rc.left + 4;
                if (!item) return TRUE;
                
                if (lpdis->itemState & ODS_SELECTED && !item->isSeparator) { 
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

                if (item->isSeparator) {
                    HPEN hShadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
                    HPEN hHighlight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
                    HPEN hOldPen = SelectObject(hdc, hShadow);
                    int y = rc.top + (rc.bottom - rc.top) / 2;
                    
                    /* Draw Etched Line */
                    MoveTo(hdc, textX, y - 1); LineTo(hdc, rc.right - 2, y - 1);
                    SelectObject(hdc, hHighlight);
                    MoveTo(hdc, textX, y); LineTo(hdc, rc.right - 2, y);
                    
                    SelectObject(hdc, hOldPen); DeleteObject(hShadow); DeleteObject(hHighlight);
                } else {
                    if (item->polyIcon >= 0) {
                        switch(item->polyIcon) {
                            case 0: DrawIconFolder(hdc, textX, rc.top + 2); break;
                            case 1: DrawIconFolder(hdc, textX, rc.top + 2); break; 
                            case 2: DrawIconSearch(hdc, textX, rc.top + 2); break;
                            case 3: DrawIconSettings(hdc, textX, rc.top + 2); break;
                            case 4: DrawIconHelp(hdc, textX, rc.top + 2); break;
                            case 5: DrawIconRun(hdc, textX, rc.top + 2); break;
                            case 6: DrawIconMonitor(hdc, textX, rc.top + 2); break;
                        }
                    } else if (item->cacheIndex >= 0 && g_hCacheBitmap) {
                        HDC hdcMem = CreateCompatibleDC(hdc); HBITMAP hOld = SelectObject(hdcMem, g_hCacheBitmap);
                        BitBlt(hdc, textX, rc.top + 2, 32, 32, hdcMem, item->cacheIndex * 32, 0, SRCCOPY);
                        SelectObject(hdcMem, hOld); DeleteDC(hdcMem);
                    } else if (item->hIcon) DrawIcon(hdc, textX, rc.top + 2, item->hIcon);

                    textX += 36; SetBkMode(hdc, TRANSPARENT);
                    if (lpdis->itemState & ODS_SELECTED) SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT)); else SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
                    { HFONT hOldMenuFont = SelectObject(hdc, g_hFontMenu); TextOut(hdc, textX, rc.top + 10, item->text, lstrlen(item->text)); SelectObject(hdc, hOldMenuFont); }
                }
            }
            return TRUE;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lParam;
            if (lpmis->CtlType == ODT_MENU) {
                ODMenuItem FAR* item = (ODMenuItem FAR*)lpmis->itemData;
                if (item) { if (item->isSeparator) { lpmis->itemWidth = 100; lpmis->itemHeight = 8; } else { lpmis->itemWidth = 120 + (item->isRoot ? 32 : 0); lpmis->itemHeight = 36; } }
            } else if (lpmis->CtlType == ODT_BUTTON) { lpmis->itemWidth = 38; lpmis->itemHeight = 22; }
            return TRUE;
        }

        case WM_USER_CONTEXTMENU: {
            HMENU hMenu = CreatePopupMenu(); UINT state = (g_ContextId[0] == '\0') ? MF_GRAYED : 0;
            AppendMenu(hMenu, MF_STRING | state, IDM_CTX_PROPS, "Properties");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, IDM_CTX_NEWFOLDER, "New Folder...");
            AppendMenu(hMenu, MF_STRING, IDM_CTX_NEWSHORTCUT, "New Shortcut...");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING | state, IDM_CTX_RENAME, "Rename...");
            AppendMenu(hMenu, MF_STRING | state, IDM_CTX_DELETE, "Delete");
            g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, LOWORD(lParam), HIWORD(lParam), 0, hwnd, NULL); g_MenuOpen = FALSE;
            DestroyMenu(hMenu); return 0;
        }
        
        case WM_MENUSELECT: {
            UINT idItem = wParam; UINT flags = LOWORD(lParam); HMENU hMenu = (HMENU)HIWORD(lParam); 
            if ((flags & 0xFFFF) == 0xFFFF && hMenu == NULL) {
            } else if (!(flags & MF_SEPARATOR) && !(flags & MF_SYSMENU)) {
                if (flags & MF_POPUP) {
                    int i;
                    for (i = 0; i < g_IniShortcutCount; i++) {
                        if (g_IniShortcuts[i].isFolder && (g_IniShortcuts[i].hMenu == GetSubMenu(hMenu, idItem) || g_IniShortcuts[i].hMenu == (HMENU)idItem)) {
                            lstrcpy(g_ContextId, g_IniShortcuts[i].id);
                            g_ContextIsFolder = TRUE;
                            break;
                        }
                    }
                } else if (idItem >= IDM_START_BASE && idItem < IDM_START_BASE + MAX_START_ITEMS) {
                    int idx = idItem - IDM_START_BASE;
                    if (idx < g_IniShortcutCount) {
                        lstrcpy(g_ContextId, g_IniShortcuts[idx].id);
                        g_ContextIsFolder = FALSE;
                    }
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt; RECT rcClock; POINT ptTopLeft, ptBotRight;
            pt.x = LOWORD(lParam); pt.y = HIWORD(lParam);
            
            if (IsWindowVisible(g_hSearchList)) ShowWindow(g_hSearchList, SW_HIDE);

            GetWindowRect(g_hClock, &rcClock);
            ptTopLeft.x = rcClock.left; ptTopLeft.y = rcClock.top;
            ptBotRight.x = rcClock.right; ptBotRight.y = rcClock.bottom;
            ScreenToClient(hwnd, &ptTopLeft);
            ScreenToClient(hwnd, &ptBotRight);
            rcClock.left = ptTopLeft.x; rcClock.top = ptTopLeft.y;
            rcClock.right = ptBotRight.x; rcClock.bottom = ptBotRight.y;

            if (PtInRect(&rcClock, pt)) {
                HMENU hMenu = CreatePopupMenu(); POINT screenPt;
                screenPt.x = LOWORD(lParam); screenPt.y = HIWORD(lParam);
                ClientToScreen(hwnd, &screenPt);
                AppendMenu(hMenu, MF_STRING, IDM_CLOCK_ADJUST, "Adjust Date/Time");
                g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, screenPt.x, screenPt.y, 0, hwnd, NULL); g_MenuOpen = FALSE;
                DestroyMenu(hMenu);
                return 0;
            } else {
                HMENU hMenu = CreatePopupMenu(); POINT screenPt;
                screenPt.x = LOWORD(lParam); screenPt.y = HIWORD(lParam); ClientToScreen(hwnd, &screenPt);
                AppendMenu(hMenu, MF_STRING, IDM_TB_TASKMGR, "Task Manager");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, IDM_TB_SHOWDESKTOP, "Show Desktop");
                g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, screenPt.x, screenPt.y, 0, hwnd, NULL); g_MenuOpen = FALSE;
                DestroyMenu(hMenu);
                return 0;
            }
        }

        case WM_LBUTTONDOWN: {
            POINT pt; RECT rc; pt.x = LOWORD(lParam); pt.y = HIWORD(lParam); GetClientRect(hwnd, &rc);
            
            if (IsWindowVisible(g_hSearchList)) ShowWindow(g_hSearchList, SW_HIDE);

            if ((g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP) && pt.x > rc.right - 14) {
                DoShowDesktop();
                return 0;
            }
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
            else if (id >= ID_QUICK_BASE && id < ID_QUICK_BASE + QUICK_LAUNCH_COUNT) {
                IniShortcut sh; memset(&sh, 0, sizeof(sh));
                lstrcpy(sh.exe, g_QL[id - ID_QUICK_BASE].exe);
                LaunchShortcut(hwnd, &sh);
            }
            else if (id >= IDM_FS_BASE && id < IDM_FS_BASE + MAX_OD_ITEMS) {
                int j;
                for (j = 0; j < g_ODCount; j++) {
                    if (g_ODItems[j].cmdId == id) {
                        if (g_ODItems[j].isFsFolder) {
                            ShellExecute(hwnd, "open", "explorer.exe", g_ODItems[j].targetPath, NULL, SW_SHOWNORMAL);
                        } else {
                            IniShortcut tempSh;
                            memset(&tempSh, 0, sizeof(tempSh));
                            lstrcpy(tempSh.exe, g_ODItems[j].targetPath);
                            LaunchShortcut(hwnd, &tempSh);
                        }
                        break;
                    }
                }
            }
            else if (id == IDM_SEARCH_LIST && HIWORD(lParam) == LBN_DBLCLK) {
                int sel = SendMessage(g_hSearchList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    int idx = SendMessage(g_hSearchList, LB_GETITEMDATA, sel, 0);
                    if (idx >= 0 && idx < g_IniShortcutCount) {
                        IniShortcut* sh = &g_IniShortcuts[idx];
                        if (sh->isFolder) {
                            ShowWindow(g_hSearchList, SW_HIDE);
                            SetWindowText(g_hSearchBox, "");
                            ShowFolderMenu(g_hSearchBox, sh->id);
                            return 0;
                        } else {
                            PostMessage(hwnd, WM_COMMAND, IDM_START_BASE + idx, 0);
                        }
                    }
                }
                ShowWindow(g_hSearchList, SW_HIDE); SetWindowText(g_hSearchBox, ""); return 0;
            }
            else if (id >= IDM_START_BASE && id < IDM_START_BASE + MAX_START_ITEMS) {
                int idx = id - IDM_START_BASE; 
                if (idx < g_IniShortcutCount) { 
                    LaunchShortcut(hwnd, &g_IniShortcuts[idx]);
                }
            }
            else if (id >= ID_TASK_BASE && id < ID_TASK_BASE + MAX_TASKS) {
                int ti = id - ID_TASK_BASE;
                if (ti >= 0 && ti < g_TaskCount) {
                    HWND win = g_Tasks[ti].hWnd;
                    if (IsIconic(win)) ShowWindow(win, SW_RESTORE);
                    BringWindowToTop(win);
                }
            }
            else if (id == IDM_TASK_MINIMIZE && g_ContextTargetWnd) SendMessage(g_ContextTargetWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0L);
            else if (id == IDM_TASK_MAXIMIZE && g_ContextTargetWnd) SendMessage(g_ContextTargetWnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0L);
            else if (id == IDM_TASK_CLOSE && g_ContextTargetWnd) SendMessage(g_ContextTargetWnd, WM_SYSCOMMAND, SC_CLOSE, 0L);
            else if (id == IDM_CTX_PROPS) {
                if (g_ContextId[0] != '\0') { lstrcpy(g_EditShortcutId, g_ContextId); CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Edit Properties", 360, 300); }
            }
            else if (id == IDM_CTX_NEWFOLDER || id == IDM_WINX_NEWFOLDER) { 
                if (id == IDM_WINX_NEWFOLDER) lstrcpy(g_ContextId, "0"); 
                g_ContextIsFolder = TRUE; lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0'; g_PromptMode = PROMPT_NEWFOLDER; CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 170); 
            }
            else if (id == IDM_CTX_NEWSHORTCUT || id == IDM_WINX_SHORTCUT) { 
                if (id == IDM_WINX_SHORTCUT) lstrcpy(g_ContextId, "0"); 
                g_ContextIsFolder = TRUE; g_EditShortcutId[0] = '\0'; CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 360, 300); 
            }
            else if (id == IDM_CTX_RENAME) { int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].id, g_ContextId) == 0) { lstrcpy(g_PromptValue, g_IniShortcuts[i].name); break; } } lstrcpy(g_PromptLabel, "New Name:"); g_PromptMode = PROMPT_RENAME; CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "Rename", 290, 130); }
            else if (id == IDM_CTX_DELETE) { if (g_ContextId[0] != '\0' && MessageBox(hwnd, "Delete item?", "Confirm", MB_YESNO) == IDYES) { WritePrivateProfileString("Shortcut", g_ContextId, NULL, g_szIniPath); LoadIniShortcuts(); } }
            else if (id == IDM_TB_TASKMGR) WinExec(g_szTaskMgrExe, SW_SHOWNORMAL);
            else if (id == IDM_TB_SHOWDESKTOP) DoShowDesktop();
            else if (id == IDM_CLOCK_ADJUST) {
                CreateCenteredDialog(g_hInst, hwnd, "DateTimeDlgClass", "Date and Time", 240, 280);
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_CLOCK) UpdateClock();
            else if (wParam == TIMER_REFRESH && !g_MenuOpen) RefreshTasks();
            else if (wParam == TIMER_HOTKEY) { 
                if (GetAsyncKeyState(VK_LWIN) & 1 || GetAsyncKeyState(VK_RWIN) & 1) { 
                    PostMessage(hwnd, WM_COMMAND, ID_START_BUTTON, 0); 
                }
                {
                    int i;
                    for (i = 0; i < g_IniShortcutCount; i++) {
                        if (g_IniShortcuts[i].hotkey[0] != '\0') {
                            int vk = atoi(g_IniShortcuts[i].hotkey);
                            if (vk && (GetAsyncKeyState(vk) & 1)) {
                                LaunchShortcut(hwnd, &g_IniShortcuts[i]);
                            }
                        }
                    }
                }
            }
            return 0;

        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: {
            if (g_hCacheBitmap) DeleteObject(g_hCacheBitmap);
            if (g_hMemODItems) GlobalFree(g_hMemODItems);
            if (g_hMemIniShortcuts) GlobalFree(g_hMemIniShortcuts);
            KillTimer(hwnd, TIMER_CLOCK); KillTimer(hwnd, TIMER_REFRESH); KillTimer(hwnd, TIMER_HOTKEY);
            if (g_hFontMenu) DeleteObject(g_hFontMenu);
            if (g_hFontSidebar) DeleteObject(g_hFontSidebar);
            PostQuitMessage(0); return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK DateTimeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static int s_month, s_year, s_day;
    static HWND hPrev, hNext, hLbl;
    switch(msg) {
        case WM_CREATE: {
            struct dosdate_t d;
            _dos_getdate(&d);
            s_month = d.month; s_year = d.year; s_day = d.day;
            
            hPrev = CreateWindow("BUTTON", "<", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 10, 10, 30, 24, hwnd, (HMENU)101, g_hInst, NULL);
            hNext = CreateWindow("BUTTON", ">", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 200, 10, 30, 24, hwnd, (HMENU)102, g_hInst, NULL);
            hLbl = CreateWindow("STATIC", "", WS_CHILD|WS_VISIBLE|SS_CENTER, 45, 14, 150, 20, hwnd, NULL, g_hInst, NULL);
            SetFont(hPrev, NULL); SetFont(hNext, NULL); SetFont(hLbl, g_hFontMenu);
            
            CreateWindow("BUTTON", "Apply", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 35, 220, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 125, 220, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            
            PostMessage(hwnd, WM_USER+1, 0, 0); 
            PositionDialogNearStart(hwnd);
            return 0;
        }
        case WM_USER+1: {
            char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
            char buf[64];
            sprintf(buf, "%s %d", months[s_month-1], s_year);
            SetWindowText(hLbl, buf);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (wp == 101) {
                if (--s_month < 1) { s_month = 12; s_year--; }
                PostMessage(hwnd, WM_USER+1, 0, 0);
            } else if (wp == 102) {
                if (++s_month > 12) { s_month = 1; s_year++; }
                PostMessage(hwnd, WM_USER+1, 0, 0);
            } else if (wp == IDOK) {
                struct dosdate_t d;
                _dos_getdate(&d); /* retain week day */
                d.year = s_year; d.month = s_month; d.day = s_day;
                _dos_setdate(&d);
                EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd);
            } else if (wp == IDCANCEL) {
                EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            int y = s_year - (s_month < 3);
            int firstDay = (y + y/4 - y/100 + y/400 + t[s_month-1] + 1) % 7;
            int daysInMonth = 31;
            int r, c, d = 1, cx = 15, cy = 45, cellW = 30, cellH = 24;
            char* wd[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
            HFONT hOldFont = SelectObject(hdc, GetStockObject(ANSI_VAR_FONT));
            
            SetBkMode(hdc, TRANSPARENT);
            if (s_month==4||s_month==6||s_month==9||s_month==11) daysInMonth=30;
            else if (s_month==2) daysInMonth = (s_year%4==0 && (s_year%100!=0 || s_year%400==0)) ? 29 : 28;
            
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            for(c=0; c<7; c++) {
                TextOut(hdc, cx + c*cellW + 6, cy, wd[c], 2);
            }
            cy += 20;
            
            for(r=0; r<6; r++) {
                for(c=0; c<7; c++) {
                    if (r==0 && c<firstDay) continue;
                    if (d > daysInMonth) break;
                    
                    if (d == s_day) {
                        HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
                        RECT hRc; hRc.left = cx + c*cellW+2; hRc.top = cy + r*cellH; hRc.right = hRc.left + cellW-4; hRc.bottom = hRc.top + cellH;
                        FillRect(hdc, &hRc, hBr);
                        DeleteObject(hBr);
                        SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                    } else {
                        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                    }
                    
                    {
                        char dstr[4]; sprintf(dstr, "%d", d);
                        TextOut(hdc, cx + c*cellW + (d<10?10:6), cy + r*cellH + 4, dstr, lstrlen(dstr));
                    }
                    d++;
                }
            }
            SelectObject(hdc, hOldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int px = LOWORD(lp), py = HIWORD(lp);
            int cx = 15, cy = 65, cellW = 30, cellH = 24;
            if (px >= cx && px < cx + 7*cellW && py >= cy && py < cy + 6*cellH) {
                int c = (px - cx) / cellW;
                int r = (py - cy) / cellH;
                int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
                int y = s_year - (s_month < 3);
                int firstDay = (y + y/4 - y/100 + y/400 + t[s_month-1] + 1) % 7;
                int clickedDay = r * 7 + c - firstDay + 1;
                int daysInMonth = 31;
                if (s_month==4||s_month==6||s_month==9||s_month==11) daysInMonth=30;
                else if (s_month==2) daysInMonth = (s_year%4==0 && (s_year%100!=0 || s_year%400==0)) ? 29 : 28;
                
                if (clickedDay >= 1 && clickedDay <= daysInMonth) {
                    s_day = clickedDay;
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            return 0;
        }
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void ShowFolderMenu(HWND hAnchor, const char* folderId) {
    HMENU hMenu = CreatePopupMenu(); POINT pt; RECT rc;
    ClearODIcons();
    BuildMenuFromIni(hMenu, folderId, FALSE);

    GetWindowRect(hAnchor, &rc);
    if (g_TbPosition == POS_BOTTOM) { pt.x = rc.left; pt.y = rc.top; } 
    else if (g_TbPosition == POS_TOP) { pt.x = rc.left; pt.y = rc.bottom; } 
    else if (g_TbPosition == POS_LEFT) { pt.x = rc.right; pt.y = rc.top; } 
    else { pt.x = rc.left; pt.y = rc.top; }

    g_ContextId[0] = '\0'; g_ContextIsFolder = FALSE;
    g_lpfnMsgFilter = MakeProcInstance((FARPROC)MsgFilterProc, g_hInst);
    g_hMsgHook = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)g_lpfnMsgFilter, g_hInst, GetCurrentTask());
    
    g_MenuOpen = TRUE;
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL);
    g_MenuOpen = FALSE;
    
    UnhookWindowsHookEx(g_hMsgHook); FreeProcInstance(g_lpfnMsgFilter); DestroyMenu(hMenu);
}

static void LaunchShortcut(HWND hwnd, IniShortcut* sh) {
    if (lstrcmpi(sh->exe, "shutdown") == 0) {
        ExitWindows(0, 0);
    } else if (lstrcmpi(sh->exe, "run") == 0) {
        CreateCenteredDialog(g_hInst, hwnd, "RunDlgClass", "Run", 290, 160);
    } else if (sh->exe[0] != '\0') {
        char dir[MAX_PATH]; char* pDir; HINSTANCE hInstExec;
        lstrcpy(dir, sh->exe);
        pDir = strrchr(dir, '\\');
        if (pDir) *pDir = '\0'; else dir[0] = '\0';
        
        /* Attempt ShellExecute with explicit working directory */
        hInstExec = ShellExecute(hwnd, "open", sh->exe, sh->params[0] ? sh->params : NULL, dir[0] ? dir : NULL, sh->minimized ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
        
        /* Fallback to legacy WinExec if ShellExecute fails (returns <= 32) */
        if ((int)hInstExec <= 32) {
            char cmd[512];
            lstrcpy(cmd, sh->exe);
            if (sh->params[0]) {
                lstrcat(cmd, " ");
                lstrcat(cmd, sh->params);
            }
            WinExec(cmd, sh->minimized ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
        }
    }
}
LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit, hParentFolder;
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", g_PromptLabel, WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            hEdit = CreateWindowEx(0, "EDIT", g_PromptValue, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER, 10, 35, 260, 22, hwnd, NULL, g_hInst, NULL);
            
            if (g_PromptMode == PROMPT_NEWFOLDER) {
                int i; char parentId[16];
                CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 65, 100, 20, hwnd, NULL, g_hInst, NULL);
                hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP, 110, 65, 160, 150, hwnd, NULL, g_hInst, NULL);
                
                SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)"0 (Root)");
                for (i = 0; i < g_IniShortcutCount; i++) {
                    if (g_IniShortcuts[i].isFolder) {
                        char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i].id, g_IniShortcuts[i].name);
                        SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)buf);
                    }
                }
                
                lstrcpy(parentId, g_ContextId[0] ? g_ContextId : "0");
                for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) {
                    char buf[128]; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)buf);
                    if (strncmp(buf, parentId, lstrlen(parentId)) == 0 && buf[lstrlen(parentId)] == ' ') {
                        SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break;
                    }
                }
                if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0);
                
                CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 50, 100, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
                CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 150, 100, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            } else {
                hParentFolder = NULL;
                CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 50, 70, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
                CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 150, 70, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            }
            SetFont(hwnd, NULL); SetFocus(hEdit); PositionDialogNearStart(hwnd); return 0;
        }
        case WM_COMMAND:
            if (wp == IDOK) {
                GetWindowText(hEdit, g_PromptValue, MAX_PATH);
                if (g_PromptValue[0] != '\0') {
                    if (g_PromptMode == PROMPT_NEWFOLDER) {
                        char newId[16], parentSel[128], *space; 
                        IniShortcut sh; 
                        GetNewIniId(newId); lstrcpy(sh.id, newId); lstrcpy(sh.name, g_PromptValue);
                        sh.exe[0] = '\0'; sh.params[0] = '\0'; sh.icon[0] = '\0'; sh.minimized = 0; sh.hotkey[0] = '\0';
                        
                        if (hParentFolder) {
                            GetWindowText(hParentFolder, parentSel, sizeof(parentSel));
                            space = strchr(parentSel, ' '); if (space) *space = '\0';
                            lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0");
                        } else {
                            lstrcpy(sh.parentId, g_ContextId[0] ? g_ContextId : "0");
                        }
                        
                        sh.isFolder = TRUE;
                        SaveIniEntry(&sh); UpdateIconInCache(atoi(sh.id), NULL, NULL, TRUE); LoadIniShortcuts();
                    } else if (g_PromptMode == PROMPT_RENAME) {
                        int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].id, g_ContextId) == 0) { lstrcpy(g_IniShortcuts[i].name, g_PromptValue); SaveIniEntry(&g_IniShortcuts[i]); LoadIniShortcuts(); break; } }
                    }
                }
                EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd);
            } else if (wp == IDCANCEL) { g_PromptValue[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); }
            return 0;
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK RunDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hCombo;
    switch(msg) {
        case WM_CREATE: {
            char hist[512], *token;
            CreateWindow("STATIC", "Type the name of a program to open:", WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            hCombo = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL|WS_BORDER, 10, 35, 260, 120, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 10, 70, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 100, 70, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 190, 70, 80, 24, hwnd, (HMENU)101, g_hInst, NULL);
            GetPrivateProfileString("Run", "History", "", hist, sizeof(hist), g_szIniPath);
            token = strtok(hist, "|"); while(token) { SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)token); token = strtok(NULL, "|"); }
            SendMessage(hCombo, CB_SETCURSEL, 0, 0); SetFont(hwnd, NULL); SetFocus(hCombo); PositionDialogNearStart(hwnd); return 0;
        }
        case WM_COMMAND:
            if (wp == IDOK) {
                char cmd[128], newHist[512] = "", oldHist[512]; int i, count;
                GetWindowText(hCombo, cmd, sizeof(cmd));
                if (cmd[0]) { WinExec(cmd, SW_SHOWNORMAL); lstrcpy(newHist, cmd); count = SendMessage(hCombo, CB_GETCOUNT, 0, 0);
                    for (i=0; i<count && i<9; i++) { SendMessage(hCombo, CB_GETLBTEXT, i, (LPARAM)oldHist); if (lstrcmpi(oldHist, cmd) != 0) { lstrcat(newHist, "|"); lstrcat(newHist, oldHist); } }
                    WritePrivateProfileString("Run", "History", newHist, g_szIniPath); }
                EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd);
            } else if (wp == IDCANCEL) { EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); } 
            else if (wp == 101) { char path[MAX_PATH]; if (BrowseFile(hwnd, path, "Programs (*.exe;*.com)\0*.exe;*.com\0All Files (*.*)\0*.*\0")) SetWindowText(hCombo, path); }
            return 0;
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc; MSG msg; g_hInst = hInst;
    g_lpfnEnumWindowsProc = MakeProcInstance((FARPROC)TaskbarEnumWindowsProc, hInst);
    g_lpfnTaskBtnProc = MakeProcInstance((FARPROC)TaskBtnProc, hInst);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.style = CS_DBLCLKS; wc.lpfnWndProc = TaskbarProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "CalmiraTaskbarClass"; RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = RunDlgProc; wc.hInstance = hInst; wc.lpszClassName = "RunDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = ShortcutDlgProc; wc.hInstance = hInst; wc.lpszClassName = "ShortcutDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = PromptDlgProc; wc.hInstance = hInst; wc.lpszClassName = "PromptDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = DateTimeDlgProc; wc.hInstance = hInst; wc.lpszClassName = "DateTimeDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    g_hTaskbar = CreateWindowEx(WS_EX_ACCEPTFILES, "CalmiraTaskbarClass", "Calmira Taskbar", WS_POPUP | WS_VISIBLE, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    ShowWindow(g_hTaskbar, nCmdShow); UpdateWindow(g_hTaskbar);
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}
