/*
 * ============================================================================
 * Explorer for Win16 - Windows 95 Style Shell
 * ============================================================================
 *
 * COMPILATION INSTRUCTIONS:
 * Using OpenWatcom on Windows:
 *   wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s explorer.c shell.lib commdlg.lib
 *
 * ============================================================================
 */

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include <direct.h>
#include <io.h>
#include <ctype.h>
#include <malloc.h>

#ifndef DEFAULT_GUI_FONT
#define DEFAULT_GUI_FONT ANSI_VAR_FONT
#endif

#define MAX_PATH 260
#define MAX_INI_SHORTCUTS 256
#define MAX_EXPANDED_NODES 128
#define MAX_LIST_ITEMS 2048
#define MAX_TREE_LEVEL 16

#define CELL_W 75
#define CELL_H 75

#define ID_TREE 101
#define ID_LIST 102
#define ID_TOOLBAR 103
#define ID_HDR_NAME 201
#define ID_HDR_SIZE 202
#define ID_HDR_TYPE 203
#define ID_HDR_DATE 204

#define PROMPT_RENAME 1
#define PROMPT_NEWFOLDER 2

typedef struct {
    char id[16];
    char name[64];
    char exe[MAX_PATH];
    char params[MAX_PATH];
    char icon[MAX_PATH];
    char hotkey[16];
    char parentId[16];
    int minimized;
    int isFolder;
    char minimizedStr[8];
} IniShortcut;

typedef struct {
    char pathOrId[MAX_PATH];
    char displayName[64];
    int level;
    BOOL hasChildren;
    BOOL expanded;
    BOOL isVirtual;
    BOOL isLastChild[MAX_TREE_LEVEL];
} TreeItemData;

typedef struct {
    int type; /* 1 = ListItem */
    char name[64];
    char path[MAX_PATH];
    unsigned long size;
    unsigned date;
    unsigned time;
    BOOL isDir;
    BOOL isVirtual;
    char ext[16];
} ListItemData;

typedef struct {
    int type; /* 2 = RowItem */
    int count;
    ListItemData FAR* items[16];
} RowItemData;

typedef struct {
    char name[MAX_PATH];
} TempSubDir;

IniShortcut g_IniShortcuts[MAX_INI_SHORTCUTS];
int g_IniShortcutCount = 0;
char g_szExplorerIni[MAX_PATH];
HINSTANCE g_hInst;
HWND g_hwndMain;
int g_DesktopSelectedIndex = -1;

/* Explorer View State Variables */
int g_SplitX = 200;
int g_SortCol = 0;   
int g_SortOrder = 1; 
int g_ViewMode = 3; /* 0=Large, 1=Small, 2=List, 3=Details */
BOOL g_bDraggingSplitter = FALSE;
char g_CurrentPathOrId[MAX_PATH] = "0"; 
BOOL g_CurrentIsVirtual = TRUE;
BOOL g_bShowToolbar = TRUE;
BOOL g_bShowStatusBar = TRUE;

char g_ExpandedNodes[MAX_EXPANDED_NODES][MAX_PATH];
int g_ExpandedCount = 0;

/* Context / Editing Variables */
char g_ContextId[MAX_PATH] = "";
BOOL g_ContextIsFolder = FALSE;
BOOL g_ContextIsVirtual = TRUE;
char g_EditShortcutId[16] = "";
int g_PromptMode = 0;
char g_PromptValue[MAX_PATH] = "";
char g_PromptLabel[64] = "";

/* Grid Selection Tracking Variables */
char g_SelectedListItemPath[MAX_PATH] = "";
char g_SelectedListItemName[64] = "";
BOOL g_SelectedListItemIsVirtual = FALSE;
BOOL g_SelectedListItemIsDir = FALSE;

/* --- Clipboard & Copy/Move --- */
char g_ClipPath[MAX_PATH] = "";
char g_ClipName[64] = "";
char g_ClipId[16] = "";
BOOL g_ClipIsVirtual = FALSE;
BOOL g_ClipIsDir = FALSE;
int g_ClipOp = 0; /* 1=Copy, 2=Cut */

typedef struct {
    char src[MAX_PATH];
    char dst[MAX_PATH];
    BOOL isMove;
    BOOL isDir;
} CopyJob;
CopyJob g_CurrentJob;

char g_DelPath[MAX_PATH] = "";
BOOL g_DelIsDir = FALSE;
BOOL g_bCancelDel = FALSE;
HWND g_hDelDlg = NULL;

int g_ReplaceMode = 0; /* 0=Prompt, 1=All, 2=Skip, 3=Cancel */
char g_ReplaceTarget[MAX_PATH] = "";
int g_ProgressTick = 0;
int g_ReplaceResult = 0;

static FARPROC g_lpfnOldTreeProc = NULL;
static FARPROC g_lpfnTreeProc = NULL;
static FARPROC g_lpfnOldListProc = NULL;
static FARPROC g_lpfnListProc = NULL;
static FARPROC g_lpfnToolbarProc = NULL;
static FARPROC g_lpfnOldToolbarProc = NULL;

/* --- Inline Rename Support --- */
static FARPROC g_lpfnOldInlineEditProc = NULL;
static char g_InlineRenameOldPath[MAX_PATH];
static char g_InlineRenameId[16];
static BOOL g_InlineRenameIsVirtual;

/* --- Prototypes --- */
static void RebuildTree(HWND hTree);
static void RebuildList(HWND hwnd);
static void LoadConfig(void);
static void SaveConfig(void);
static void LoadIniShortcuts(void);
void ChangeViewMode(HWND hwnd, int mode);
static void SaveIniEntry(IniShortcut* item);

/* --- Helpers --- */

void ProcessMessages(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessage(&msg);
    }
}

void UpdateProgressGauge(HWND hDlg) {
    g_ProgressTick = (g_ProgressTick + 5) % 100;
    HWND hGauge = GetDlgItem(hDlg, 102);
    if (hGauge) {
        HDC hdc = GetDC(hGauge); RECT rc; GetClientRect(hGauge, &rc);
        HBRUSH hBlue = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        HBRUSH hWhite = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        int w = (rc.right * g_ProgressTick) / 100;
        RECT rFill = rc; rFill.right = w; FillRect(hdc, &rFill, hBlue);
        RECT rClear = rc; rClear.left = w; FillRect(hdc, &rClear, hWhite);
        DeleteObject(hBlue); DeleteObject(hWhite); ReleaseDC(hGauge, hdc);
    }
}

int ShowModalReplaceDialog(HWND hParent) {
    HWND hDlg; MSG msg; int res; g_ReplaceResult = 0;
    hDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "ReplaceDlgClass", "Confirm Replace", WS_POPUP|WS_CAPTION|WS_VISIBLE|WS_SYSMENU, 
        (GetSystemMetrics(SM_CXSCREEN)-320)/2, (GetSystemMetrics(SM_CYSCREEN)-140)/2, 320, 140, hParent, NULL, g_hInst, NULL);
    EnableWindow(hParent, FALSE);
    while (g_ReplaceResult == 0 && GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    res = g_ReplaceResult; EnableWindow(hParent, TRUE); DestroyWindow(hDlg);
    return res;
}

BOOL DoCopyFile(HWND hDlg, const char* src, const char* dst) {
    FILE *fs, *fd; char *buf; size_t n;
    if (access(dst, 0) == 0) {
        if (g_ReplaceMode == 1) { /* Yes to All */ }
        else if (g_ReplaceMode == 2) { return TRUE; /* Skip */ }
        else {
            lstrcpy(g_ReplaceTarget, dst);
            int res = ShowModalReplaceDialog(hDlg);
            if (res == 4) { g_ReplaceMode = 3; return FALSE; } 
            if (res == 3) { return TRUE; } 
            if (res == 2) { g_ReplaceMode = 1; } 
        }
    }
    if (g_ReplaceMode == 3) return FALSE;
    fs = fopen(src, "rb"); fd = fopen(dst, "wb");
    if (!fs || !fd) { if (fs) fclose(fs); if (fd) fclose(fd); return FALSE; }
    buf = (char*)malloc(4096);
    if (buf) {
        while ((n = fread(buf, 1, 4096, fs)) > 0) { 
            fwrite(buf, 1, n, fd); UpdateProgressGauge(hDlg); ProcessMessages(); 
        }
        free(buf);
    }
    fclose(fs); fclose(fd);
    return TRUE;
}

void DoCopyFolder(HWND hDlg, const char* src, const char* dst) {
    struct find_t file; char *search; char *sPath; char *dPath;
    
    if (strstr(dst, src) == dst) {
        MessageBox(hDlg, "Cannot copy a folder into itself.", "Error", MB_OK|MB_ICONHAND);
        g_ReplaceMode = 3; return;
    }
    
    search = (char*)malloc(MAX_PATH); if (!search) return;
    mkdir(dst); lstrcpy(search, src);
    if (search[0] != '\0' && search[lstrlen(search)-1] != '\\') lstrcat(search, "\\");
    lstrcat(search, "*.*");
    
    if (_dos_findfirst(search, _A_NORMAL|_A_SUBDIR|_A_RDONLY|_A_ARCH|_A_HIDDEN|_A_SYSTEM, &file) == 0) {
        sPath = (char*)malloc(MAX_PATH); dPath = (char*)malloc(MAX_PATH);
        if (sPath && dPath) {
            do {
                if (g_ReplaceMode == 3) break;
                UpdateProgressGauge(hDlg); ProcessMessages();
                if (lstrcmp(file.name, ".") != 0 && lstrcmp(file.name, "..") != 0) {
                    lstrcpy(sPath, src); if (sPath[lstrlen(sPath)-1] != '\\') lstrcat(sPath, "\\"); lstrcat(sPath, file.name);
                    lstrcpy(dPath, dst); if (dPath[lstrlen(dPath)-1] != '\\') lstrcat(dPath, "\\"); lstrcat(dPath, file.name);
                    if (file.attrib & _A_SUBDIR) DoCopyFolder(hDlg, sPath, dPath); else DoCopyFile(hDlg, sPath, dPath);
                }
            } while (_dos_findnext(&file) == 0);
        }
        if (sPath) free(sPath); if (dPath) free(dPath);
    }
    free(search);
}

BOOL DoDeleteFolder(const char* path) {
    struct find_t file; char *search; char *child;
    search = (char*)malloc(MAX_PATH); if (!search) return FALSE;
    
    lstrcpy(search, path);
    if (search[0] != '\0' && search[lstrlen(search)-1] != '\\') lstrcat(search, "\\");
    lstrcat(search, "*.*");
    
    if (_dos_findfirst(search, _A_NORMAL|_A_SUBDIR|_A_RDONLY|_A_ARCH|_A_HIDDEN|_A_SYSTEM, &file) == 0) {
        child = (char*)malloc(MAX_PATH);
        if (child) {
            do {
                if (g_bCancelDel) break;
                if (g_hDelDlg) UpdateProgressGauge(g_hDelDlg); ProcessMessages();
                if (lstrcmp(file.name, ".") != 0 && lstrcmp(file.name, "..") != 0) {
                    lstrcpy(child, path); if (child[lstrlen(child)-1] != '\\') lstrcat(child, "\\"); lstrcat(child, file.name);
                    if (g_hDelDlg) { HWND hLbl = GetDlgItem(g_hDelDlg, 101); if (hLbl) { SetWindowText(hLbl, child); UpdateWindow(hLbl); } }
                    if (file.attrib & _A_SUBDIR) { DoDeleteFolder(child); if (!g_bCancelDel) rmdir(child); } else remove(child);
                }
            } while (_dos_findnext(&file) == 0);
            free(child);
        }
    }
    free(search);
    return !g_bCancelDel;
}

BOOL NameExists(const char* parentId, const char* name, BOOL isVirtual) {
    if (isVirtual) {
        int i; for (i=0; i<g_IniShortcutCount; i++) {
            if (lstrcmp(g_IniShortcuts[i].parentId, parentId) == 0 && lstrcmpi(g_IniShortcuts[i].name, name) == 0) return TRUE;
        }
    } else {
        char path[MAX_PATH]; lstrcpy(path, parentId);
        if (path[0] && path[lstrlen(path)-1] != '\\') lstrcat(path, "\\");
        lstrcat(path, name);
        if (access(path, 0) == 0) return TRUE;
    }
    return FALSE;
}

void FormatDateStr(unsigned date, unsigned time, char* out) {
    int y = (date >> 9) + 1980; int m = ((date >> 5) & 0x0F); int d = (date & 0x1F); int h = (time >> 11); int min = ((time >> 5) & 0x3F);
    sprintf(out, "%02d/%02d/%04d %02d:%02d", m, d, y, h, min);
}

void FormatSizeStr(unsigned long bytes, char* out) {
    if (bytes < 1024) sprintf(out, "%lu bytes", bytes);
    else if (bytes < 1048576) sprintf(out, "%lu KB", bytes / 1024);
    else sprintf(out, "%lu.%02lu MB", bytes / 1048576, (bytes % 1048576) * 100 / 1048576);
}

void GetExtension(const char* filename, char* extOut) {
    char* dot = strrchr(filename, '.');
    if (dot) { int i = 0; dot++; while (*dot && i < 15) { extOut[i++] = toupper(*dot++); } extOut[i] = '\0'; } 
    else lstrcpy(extOut, "");
}

BOOL BrowseFile(HWND hwnd, char* outPath, const char* filter) {
    OPENFILENAME ofn; memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFilter = filter; ofn.lpstrFile = outPath; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY; outPath[0] = '\0';
    return GetOpenFileName(&ofn);
}

void CreateCenteredDialog(HINSTANCE hInst, HWND hwndParent, const char* className, const char* title, int width, int height) {
    int cx = GetSystemMetrics(SM_CXSCREEN); int cy = GetSystemMetrics(SM_CYSCREEN);
    CreateWindowEx(WS_EX_DLGMODALFRAME, className, title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, (cx - width) / 2, (cy - height) / 2, width, height, hwndParent, NULL, hInst, NULL);
    EnableWindow(hwndParent, FALSE);
}

/* --- Tree Expansion Sync --- */

BOOL IsExpanded(const char* pathOrId) {
    int i; for (i = 0; i < g_ExpandedCount; i++) { if (lstrcmpi(g_ExpandedNodes[i], pathOrId) == 0) return TRUE; } return FALSE;
}

void ToggleExpand(const char* pathOrId) {
    int i;
    for (i = 0; i < g_ExpandedCount; i++) {
        if (lstrcmpi(g_ExpandedNodes[i], pathOrId) == 0) {
            g_ExpandedNodes[i][0] = '\0'; g_ExpandedCount--;
            if (i < g_ExpandedCount) lstrcpy(g_ExpandedNodes[i], g_ExpandedNodes[g_ExpandedCount]);
            return;
        }
    }
    if (g_ExpandedCount < MAX_EXPANDED_NODES) lstrcpy(g_ExpandedNodes[g_ExpandedCount++], pathOrId);
}

void ExpandAllParentsVirtual(const char* id) {
    int i;
    for (i = 0; i < g_IniShortcutCount; i++) {
        if (lstrcmp(g_IniShortcuts[i].id, id) == 0) {
            if (lstrcmp(g_IniShortcuts[i].parentId, "0") != 0) {
                ExpandAllParentsVirtual(g_IniShortcuts[i].parentId);
                if (!IsExpanded(g_IniShortcuts[i].parentId)) ToggleExpand(g_IniShortcuts[i].parentId);
            } break;
        }
    }
}

void ExpandAllParentsFS(const char* path) {
    char temp[MAX_PATH]; char* p; lstrcpy(temp, path); p = temp;
    if (!IsExpanded("0")) ToggleExpand("0");
    while (*p) {
        if (*p == '\\' && p > temp) { char saved = *(p+1); *(p+1) = '\0'; if (!IsExpanded(temp)) ToggleExpand(temp); *(p+1) = saved; }
        p++;
    }
}

/* --- INI Parsing --- */

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
        int flags = atoi(out->minimizedStr); out->minimized = (flags & 1) != 0; out->isFolder = (flags & 2) != 0;
        if (out->exe[0] == '\0' && flags == 0 && lstrcmp(out->minimizedStr, "0") == 0) out->isFolder = TRUE;
    }
}

static void LoadIniShortcuts(void) {
    char *keys = (char*)malloc(4096); char *val = (char*)malloc(512); char *pKey, *p;
    g_IniShortcutCount = 0;
    
    if (!keys || !val) { if (keys) free(keys); if (val) free(val); return; }
    
    GetModuleFileName(g_hInst, g_szExplorerIni, MAX_PATH);
    p = strrchr(g_szExplorerIni, '\\');
    if (p) { *(p + 1) = '\0'; lstrcat(g_szExplorerIni, "explorer.ini"); } else { lstrcpy(g_szExplorerIni, "explorer.ini"); }
    
    GetPrivateProfileString("Shortcut", NULL, "", keys, 4096, g_szExplorerIni);
    if (keys[0] == '\0') {
        WritePrivateProfileString("Shortcut", "00000001", "Programs||||2|0|", g_szExplorerIni);
        WritePrivateProfileString("Shortcut", "00000005", "Search|winfile.exe||2|0|0|", g_szExplorerIni);
        WritePrivateProfileString("Shortcut", "00000009", "C: Drive|C:\\||0|2|0|", g_szExplorerIni);
        GetPrivateProfileString("Shortcut", NULL, "", keys, 4096, g_szExplorerIni);
    }
    
    pKey = keys;
    while (*pKey) {
        if (g_IniShortcutCount < MAX_INI_SHORTCUTS) {
            GetPrivateProfileString("Shortcut", pKey, "", val, 512, g_szExplorerIni);
            ParseIniEntry(pKey, val, &g_IniShortcuts[g_IniShortcutCount]); g_IniShortcutCount++;
        } pKey += lstrlen(pKey) + 1;
    }
    free(keys); free(val);
}

void GetNewIniId(char* outId) {
    int maxId = 0, i;
    for (i = 0; i < g_IniShortcutCount; i++) { int id = atoi(g_IniShortcuts[i].id); if (id > maxId) maxId = id; }
    sprintf(outId, "%08d", maxId + 1);
}

static void SaveIniEntry(IniShortcut* item) {
    char *val = (char*)malloc(1024); int flags;
    if (!val) return;
    flags = (item->minimized ? 1 : 0) | (item->isFolder ? 2 : 0);
    sprintf(val, "%s|%s|%s|%s|%d|%s|%s", item->name, item->exe, item->params, item->icon, flags, item->parentId, item->hotkey);
    WritePrivateProfileString("Shortcut", item->id, val, g_szExplorerIni);
    free(val);
}

static void LoadConfig(void) {
    LoadIniShortcuts();
    g_SplitX = GetPrivateProfileInt("Settings", "SplitX", 200, g_szExplorerIni); g_SortCol = GetPrivateProfileInt("Settings", "SortCol", 0, g_szExplorerIni); g_SortOrder = GetPrivateProfileInt("Settings", "SortOrder", 1, g_szExplorerIni); g_bShowToolbar = GetPrivateProfileInt("Settings", "Toolbar", 1, g_szExplorerIni); g_bShowStatusBar = GetPrivateProfileInt("Settings", "StatusBar", 1, g_szExplorerIni); g_ViewMode = GetPrivateProfileInt("Settings", "ViewMode", 3, g_szExplorerIni);
    if (g_SplitX < 50) g_SplitX = 50; if (g_ViewMode < 0 || g_ViewMode > 3) g_ViewMode = 3;
}

static void SaveConfig(void) {
    char buf[16];
    sprintf(buf, "%d", g_SplitX); WritePrivateProfileString("Settings", "SplitX", buf, g_szExplorerIni); sprintf(buf, "%d", g_SortCol); WritePrivateProfileString("Settings", "SortCol", buf, g_szExplorerIni); sprintf(buf, "%d", g_SortOrder); WritePrivateProfileString("Settings", "SortOrder", buf, g_szExplorerIni); sprintf(buf, "%d", g_bShowToolbar); WritePrivateProfileString("Settings", "Toolbar", buf, g_szExplorerIni); sprintf(buf, "%d", g_bShowStatusBar); WritePrivateProfileString("Settings", "StatusBar", buf, g_szExplorerIni); sprintf(buf, "%d", g_ViewMode); WritePrivateProfileString("Settings", "ViewMode", buf, g_szExplorerIni);
}

/* --- List Sorting --- */

int CompareListItems(ListItemData FAR* FAR* a, ListItemData FAR* FAR* b) {
    ListItemData FAR* ia = *a; ListItemData FAR* ib = *b; int cmp = 0;
    if (ia->isDir != ib->isDir) return ib->isDir - ia->isDir;
    switch (g_SortCol) {
        case 0: cmp = lstrcmpi(ia->name, ib->name); break;
        case 1: cmp = (ia->size > ib->size) ? 1 : (ia->size < ib->size ? -1 : 0); break;
        case 2: cmp = lstrcmpi(ia->ext, ib->ext); if(cmp==0) cmp = lstrcmpi(ia->name, ib->name); break;
        case 3: if (ia->date != ib->date) cmp = (ia->date > ib->date) ? 1 : -1; else cmp = (ia->time > ib->time) ? 1 : (ia->time < ib->time ? -1 : 0); break;
    } return cmp * g_SortOrder;
}

void SortListItems(ListItemData FAR* FAR* arr, int count) {
    int gap = count; BOOL swapped = TRUE; int i;
    while (gap > 1 || swapped) {
        gap = (gap * 10) / 13; if (gap == 9 || gap == 10) gap = 11; if (gap < 1) gap = 1;
        swapped = FALSE;
        for (i = 0; i < count - gap; i++) {
            if (CompareListItems(&arr[i], &arr[i + gap]) > 0) {
                ListItemData FAR* temp = arr[i]; arr[i] = arr[i + gap]; arr[i + gap] = temp; swapped = TRUE;
            }
        }
    }
}

int CompareSubDirs(TempSubDir FAR* FAR* a, TempSubDir FAR* FAR* b) {
    TempSubDir FAR* ia = *a; TempSubDir FAR* ib = *b; return lstrcmpi(ia->name, ib->name);
}

void SortSubDirs(TempSubDir FAR* FAR* arr, int count) {
    int gap = count; BOOL swapped = TRUE; int i;
    while (gap > 1 || swapped) {
        gap = (gap * 10) / 13; if (gap == 9 || gap == 10) gap = 11; if (gap < 1) gap = 1;
        swapped = FALSE;
        for (i = 0; i < count - gap; i++) {
            if (CompareSubDirs(&arr[i], &arr[i + gap]) > 0) {
                TempSubDir FAR* temp = arr[i]; arr[i] = arr[i + gap]; arr[i + gap] = temp; swapped = TRUE;
            }
        }
    }
}

/* --- Data Population --- */

void AddTreeItem(HWND hTree, const char* name, const char* pathOrId, int level, BOOL hasChildren, BOOL isVirtual, BOOL* parentLastChildArr, BOOL isLast) {
    TreeItemData FAR* item = (TreeItemData FAR*)malloc(sizeof(TreeItemData)); int pos, i;
    if (!item) return; lstrcpy(item->pathOrId, pathOrId); lstrcpyn(item->displayName, name, 63); item->level = level; item->hasChildren = hasChildren; item->expanded = IsExpanded(pathOrId); item->isVirtual = isVirtual;
    for (i = 0; i < level; i++) item->isLastChild[i] = parentLastChildArr[i]; item->isLastChild[level] = isLast;
    pos = SendMessage(hTree, LB_ADDSTRING, 0, (LPARAM)item->displayName); SendMessage(hTree, LB_SETITEMDATA, pos, (LPARAM)item);
}

void RecursiveAddTreeFS(HWND hTree, const char* path, int level, BOOL* parentLastChildArr) {
    struct find_t file; char *searchPath; TempSubDir FAR* FAR* dirs; int dirCount = 0, i;
    
    dirs = (TempSubDir FAR* FAR*)malloc(512 * sizeof(TempSubDir FAR*)); 
    searchPath = (char*)malloc(MAX_PATH);
    if (!dirs || !searchPath) { if(dirs) free(dirs); if(searchPath) free(searchPath); return; }
    
    lstrcpy(searchPath, path); if (searchPath[0] != '\0' && searchPath[lstrlen(searchPath)-1] != '\\') lstrcat(searchPath, "\\"); lstrcat(searchPath, "*.*");
    
    if (_dos_findfirst(searchPath, _A_NORMAL | _A_SUBDIR | _A_RDONLY | _A_ARCH, &file) == 0) {
        do {
            if ((file.attrib & _A_SUBDIR) && lstrcmp(file.name, ".") != 0 && lstrcmp(file.name, "..") != 0 && dirCount < 512) {
                dirs[dirCount] = (TempSubDir FAR*)malloc(sizeof(TempSubDir));
                if (dirs[dirCount]) { lstrcpy(dirs[dirCount]->name, file.name); dirCount++; }
            }
        } while (_dos_findnext(&file) == 0);
    }
    free(searchPath);
    SortSubDirs(dirs, dirCount);
    
    for (i = 0; i < dirCount; i++) {
        char *fullPath = (char*)malloc(MAX_PATH);
        BOOL isExp, isLast = (i == dirCount - 1);
        if (fullPath) {
            lstrcpy(fullPath, path); if (fullPath[0] != '\0' && fullPath[lstrlen(fullPath)-1] != '\\') lstrcat(fullPath, "\\"); lstrcat(fullPath, dirs[i]->name);
            isExp = IsExpanded(fullPath); AddTreeItem(hTree, dirs[i]->name, fullPath, level, TRUE, FALSE, parentLastChildArr, isLast); 
            if (isExp && level < MAX_TREE_LEVEL - 1) {
                BOOL newArr[MAX_TREE_LEVEL]; int j; for (j = 0; j < level; j++) newArr[j] = parentLastChildArr[j]; newArr[level] = isLast; RecursiveAddTreeFS(hTree, fullPath, level + 1, newArr);
            }
            free(fullPath);
        } free(dirs[i]);
    } free(dirs);
}

void RecursiveAddTreeVirtual(HWND hTree, const char* parentId, int level, BOOL* parentLastChildArr) {
    int i, count = 0, current = 0;
    for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i].isFolder && lstrcmp(g_IniShortcuts[i].parentId, parentId) == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) count++; }
    for (i = 0; i < g_IniShortcutCount; i++) {
        if (g_IniShortcuts[i].isFolder && lstrcmp(g_IniShortcuts[i].parentId, parentId) == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) {
            BOOL isExp, isLast = (current == count - 1);
            if (g_IniShortcuts[i].exe[0] != '\0') {
                isExp = IsExpanded(g_IniShortcuts[i].exe); AddTreeItem(hTree, g_IniShortcuts[i].name, g_IniShortcuts[i].exe, level, TRUE, FALSE, parentLastChildArr, isLast);
                if (isExp && level < MAX_TREE_LEVEL - 1) { BOOL newArr[MAX_TREE_LEVEL]; int j; for (j = 0; j < level; j++) newArr[j] = parentLastChildArr[j]; newArr[level] = isLast; RecursiveAddTreeFS(hTree, g_IniShortcuts[i].exe, level + 1, newArr); }
            } else {
                isExp = IsExpanded(g_IniShortcuts[i].id); AddTreeItem(hTree, g_IniShortcuts[i].name, g_IniShortcuts[i].id, level, TRUE, TRUE, parentLastChildArr, isLast);
                if (isExp && level < MAX_TREE_LEVEL - 1) { BOOL newArr[MAX_TREE_LEVEL]; int j; for (j = 0; j < level; j++) newArr[j] = parentLastChildArr[j]; newArr[level] = isLast; RecursiveAddTreeVirtual(hTree, g_IniShortcuts[i].id, level + 1, newArr); }
            } current++;
        }
    }
}

static void RebuildTree(HWND hTree) {
    int i, count; char targetSel[MAX_PATH]; BOOL rootArr[MAX_TREE_LEVEL];
    memset(rootArr, 0, sizeof(rootArr)); SendMessage(hTree, WM_SETREDRAW, FALSE, 0); lstrcpy(targetSel, g_CurrentPathOrId); SendMessage(hTree, LB_RESETCONTENT, 0, 0);
    AddTreeItem(hTree, "Desktop", "0", 0, TRUE, TRUE, rootArr, TRUE);
    if (IsExpanded("0")) {
        int d, driveCount = 0, currentDrive = 0, virtCount = 0;
        for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i].isFolder && lstrcmp(g_IniShortcuts[i].parentId, "0") == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) virtCount++; }
        for (d = 0; d < 26; d++) { int type = GetDriveType(d); if (type == DRIVE_REMOVABLE || type == DRIVE_FIXED || type == DRIVE_REMOTE) driveCount++; }
        for (d = 0; d < 26; d++) {
            int type = GetDriveType(d);
            if (type == DRIVE_REMOVABLE || type == DRIVE_FIXED || type == DRIVE_REMOTE) {
                char dRoot[8]; char dName[16]; BOOL isLast = (currentDrive == driveCount - 1 && virtCount == 0);
                sprintf(dRoot, "%c:\\", 'A' + d); sprintf(dName, "Local Disk (%c:)", 'A' + d); rootArr[0] = TRUE;
                AddTreeItem(hTree, dName, dRoot, 1, TRUE, FALSE, rootArr, isLast);
                if (IsExpanded(dRoot)) { BOOL newArr[MAX_TREE_LEVEL]; newArr[0] = TRUE; newArr[1] = isLast; RecursiveAddTreeFS(hTree, dRoot, 2, newArr); } currentDrive++;
            }
        }
        if (virtCount > 0) { BOOL newArr[MAX_TREE_LEVEL]; newArr[0] = TRUE; RecursiveAddTreeVirtual(hTree, "0", 1, newArr); }
    }
    count = SendMessage(hTree, LB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) { TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hTree, LB_GETITEMDATA, i, 0); if (item && lstrcmpi(item->pathOrId, targetSel) == 0) { SendMessage(hTree, LB_SETCURSEL, i, 0); break; } }
    SendMessage(hTree, WM_SETREDRAW, TRUE, 0); InvalidateRect(hTree, NULL, TRUE);
}

void ChangeViewMode(HWND hwnd, int mode) {
    HWND hOldList = GetDlgItem(hwnd, ID_LIST);
    DWORD style = WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_BORDER | LBS_EXTENDEDSEL; HMENU hMenu = GetMenu(hwnd); RECT rc;
    if (mode == 1 || mode == 2) style |= LBS_MULTICOLUMN | WS_HSCROLL; else style |= WS_VSCROLL;
    g_ViewMode = mode; SaveConfig();
    if (hMenu) { CheckMenuItem(hMenu, 4022, MF_BYCOMMAND | (mode == 0 ? MF_CHECKED : MF_UNCHECKED)); CheckMenuItem(hMenu, 4023, MF_BYCOMMAND | (mode == 1 ? MF_CHECKED : MF_UNCHECKED)); CheckMenuItem(hMenu, 4024, MF_BYCOMMAND | (mode == 2 ? MF_CHECKED : MF_UNCHECKED)); CheckMenuItem(hMenu, 4025, MF_BYCOMMAND | (mode == 3 ? MF_CHECKED : MF_UNCHECKED)); }
    if (hOldList) DestroyWindow(hOldList);
    {
        HWND hNewList = CreateWindowEx(0, "LISTBOX", "", style, 0, 0, 0, 0, hwnd, (HMENU)ID_LIST, g_hInst, NULL);
        SendMessage(hNewList, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
        if (mode == 1 || mode == 2) SendMessage(hNewList, LB_SETCOLUMNWIDTH, 150, 0);
        g_lpfnOldListProc = (FARPROC)SetWindowLong(hNewList, GWL_WNDPROC, (LONG)g_lpfnListProc);
    }
    RebuildList(hwnd); GetClientRect(hwnd, &rc); SendMessage(hwnd, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom));
}

static void RebuildList(HWND hwnd) {
    HWND hList = GetDlgItem(hwnd, ID_LIST); int i, count = 0; unsigned long totalBytes = 0; char statBuf[128]; char sizeStr[64];
    ListItemData FAR* FAR* arr = (ListItemData FAR* FAR*)malloc(MAX_LIST_ITEMS * sizeof(ListItemData FAR*)); if (!arr) return;
    SendMessage(hList, WM_SETREDRAW, FALSE, 0); SendMessage(hList, LB_RESETCONTENT, 0, 0); g_SelectedListItemPath[0] = '\0'; g_SelectedListItemName[0] = '\0';

    if (g_CurrentIsVirtual) {
        if (lstrcmp(g_CurrentPathOrId, "0") == 0) {
            int d; for (d = 0; d < 26 && count < MAX_LIST_ITEMS; d++) {
                int type = GetDriveType(d);
                if (type == DRIVE_REMOVABLE || type == DRIVE_FIXED || type == DRIVE_REMOTE) {
                    arr[count] = (ListItemData FAR*)malloc(sizeof(ListItemData));
                    if (arr[count]) { sprintf(arr[count]->name, "Local Disk (%c:)", 'A' + d); sprintf(arr[count]->path, "%c:\\", 'A' + d); arr[count]->isDir = TRUE; arr[count]->isVirtual = FALSE; lstrcpy(arr[count]->ext, ""); arr[count]->size = 0; arr[count]->date = 0; arr[count]->time = 0; count++; }
                }
            }
        }
        for (i = 0; i < g_IniShortcutCount && count < MAX_LIST_ITEMS; i++) {
            if (lstrcmp(g_IniShortcuts[i].parentId, g_CurrentPathOrId) == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) {
                arr[count] = (ListItemData FAR*)malloc(sizeof(ListItemData));
                if (arr[count]) { lstrcpyn(arr[count]->name, g_IniShortcuts[i].name, 63); lstrcpy(arr[count]->path, g_IniShortcuts[i].id); arr[count]->isDir = g_IniShortcuts[i].isFolder; arr[count]->isVirtual = TRUE; GetExtension(arr[count]->name, arr[count]->ext); if (arr[count]->isDir) lstrcpy(arr[count]->ext, ""); arr[count]->size = 0; arr[count]->date = 0; arr[count]->time = 0; count++; }
            }
        }
    } else {
        struct find_t file; char searchPath[MAX_PATH]; lstrcpy(searchPath, g_CurrentPathOrId);
        
        for (i = 0; i < g_IniShortcutCount && count < MAX_LIST_ITEMS; i++) {
            if (lstrcmp(g_IniShortcuts[i].parentId, g_CurrentPathOrId) == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) {
                arr[count] = (ListItemData FAR*)malloc(sizeof(ListItemData));
                if (arr[count]) { lstrcpyn(arr[count]->name, g_IniShortcuts[i].name, 63); lstrcpy(arr[count]->path, g_IniShortcuts[i].id); arr[count]->isDir = g_IniShortcuts[i].isFolder; arr[count]->isVirtual = TRUE; GetExtension(arr[count]->name, arr[count]->ext); if (arr[count]->isDir) lstrcpy(arr[count]->ext, ""); arr[count]->size = 0; arr[count]->date = 0; arr[count]->time = 0; count++; }
            }
        }
        if (searchPath[0] != '\0' && searchPath[lstrlen(searchPath)-1] != '\\') lstrcat(searchPath, "\\"); lstrcat(searchPath, "*.*");
        
        if (_dos_findfirst(searchPath, _A_NORMAL | _A_SUBDIR | _A_RDONLY | _A_ARCH, &file) == 0) {
            do {
                if (lstrcmp(file.name, ".") != 0 && lstrcmp(file.name, "..") != 0 && count < MAX_LIST_ITEMS) {
                    arr[count] = (ListItemData FAR*)malloc(sizeof(ListItemData));
                    if (arr[count]) { lstrcpy(arr[count]->name, file.name); lstrcpy(arr[count]->path, g_CurrentPathOrId); if (arr[count]->path[0] != '\0' && arr[count]->path[lstrlen(arr[count]->path)-1] != '\\') lstrcat(arr[count]->path, "\\"); lstrcat(arr[count]->path, file.name); arr[count]->isDir = (file.attrib & _A_SUBDIR) ? TRUE : FALSE; arr[count]->isVirtual = FALSE; arr[count]->size = file.size; arr[count]->date = file.wr_date; arr[count]->time = file.wr_time; GetExtension(file.name, arr[count]->ext); if (arr[count]->isDir) { lstrcpy(arr[count]->ext, ""); } else { totalBytes += file.size; } count++; }
                }
            } while (_dos_findnext(&file) == 0);
        }
    }
    SortListItems(arr, count);

    if (g_ViewMode == 0 || g_ViewMode == 1) {
        RECT rcList; GetClientRect(hList, &rcList); int listW = rcList.right - rcList.left; int colW = (g_ViewMode == 0) ? CELL_W : 150; int cols, r;
        if (listW < colW) listW = colW; cols = listW / colW; if (cols > 16) cols = 16; if (cols < 1) cols = 1;
        for (r = 0; r < count; r += cols) {
            RowItemData FAR* row = (RowItemData FAR*)malloc(sizeof(RowItemData));
            if (row) { int c, pos; row->type = 2; row->count = 0; for (c = 0; c < cols && r + c < count; c++) { row->items[c] = arr[r + c]; row->items[c]->type = 1; row->count++; } pos = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)""); SendMessage(hList, LB_SETITEMDATA, pos, (LPARAM)row); }
        }
    } else {
        for (i = 0; i < count; i++) { int pos; arr[i]->type = 1; pos = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)arr[i]->name); SendMessage(hList, LB_SETITEMDATA, pos, (LPARAM)arr[i]); }
    }
    
    free(arr); SendMessage(hList, WM_SETREDRAW, TRUE, 0); InvalidateRect(hList, NULL, TRUE);
    if (g_bShowStatusBar) { FormatSizeStr(totalBytes, sizeStr); sprintf(statBuf, " %d object(s)     %s", count, sizeStr); SetWindowText(GetDlgItem(hwnd, 300), statBuf); }
}

/* --- GDI Drawing Utilities --- */

static void Draw3DButton(HDC hdc, int left, int top, int right, int bottom, BOOL bPushed) {
    HPEN hHi = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT)); HPEN hSh = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)); HPEN hOld = SelectObject(hdc, bPushed ? hSh : hHi);
    MoveTo(hdc, left, bottom - 1); LineTo(hdc, left, top); LineTo(hdc, right - 1, top); SelectObject(hdc, bPushed ? hHi : hSh); MoveTo(hdc, right - 1, top); LineTo(hdc, right - 1, bottom - 1); LineTo(hdc, left, bottom - 1);
    SelectObject(hdc, hOld); DeleteObject(hHi); DeleteObject(hSh);
}

static void DrawGDIFolder(HDC hdc, int x, int y, BOOL bOpen, BOOL bSelected, BOOL bLarge) {
    HBRUSH hBr = CreateSolidBrush(bSelected ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 128)); HBRUSH hOld = SelectObject(hdc, hBr); HPEN hPen = GetStockObject(BLACK_PEN); HPEN hOldP = SelectObject(hdc, hPen);
    if (bLarge) {
        if (bOpen) { Rectangle(hdc, x+2, y+4, x+14, y+12); Rectangle(hdc, x, y+10, x+30, y+28); MoveTo(hdc, x, y+28); LineTo(hdc, x+8, y+16); LineTo(hdc, x+36, y+16); LineTo(hdc, x+30, y+28); } 
        else { Rectangle(hdc, x+2, y+4, x+14, y+12); Rectangle(hdc, x, y+10, x+32, y+28); }
    } else {
        if (bOpen) { Rectangle(hdc, x+1, y+2, x+7, y+6); Rectangle(hdc, x, y+5, x+15, y+14); MoveTo(hdc, x, y+14); LineTo(hdc, x+4, y+8); LineTo(hdc, x+18, y+8); LineTo(hdc, x+15, y+14); } 
        else { Rectangle(hdc, x+1, y+2, x+7, y+6); Rectangle(hdc, x, y+5, x+16, y+14); }
    } SelectObject(hdc, hOldP); SelectObject(hdc, hOld); DeleteObject(hBr);
}

static void DrawGDIFile(HDC hdc, int x, int y, BOOL bSelected, BOOL bLarge) {
    HBRUSH hBr = CreateSolidBrush(bSelected ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 255)); HBRUSH hOld = SelectObject(hdc, hBr); HPEN hPen = GetStockObject(BLACK_PEN); HPEN hOldP = SelectObject(hdc, hPen);
    if (bLarge) { Rectangle(hdc, x+6, y+2, x+26, y+30); MoveTo(hdc, x+18, y+2); LineTo(hdc, x+26, y+10); LineTo(hdc, x+18, y+10); LineTo(hdc, x+18, y+2); } 
    else { Rectangle(hdc, x+3, y+1, x+13, y+15); MoveTo(hdc, x+9, y+1); LineTo(hdc, x+13, y+5); LineTo(hdc, x+9, y+5); LineTo(hdc, x+9, y+1); }
    SelectObject(hdc, hOldP); SelectObject(hdc, hOld); DeleteObject(hBr);
}

static void DrawPlusMinus(HDC hdc, int x, int y, BOOL bExpanded) {
    HPEN hPen = GetStockObject(BLACK_PEN); HPEN hOldP = SelectObject(hdc, hPen); HBRUSH hBr = GetStockObject(WHITE_BRUSH); HBRUSH hOld = SelectObject(hdc, hBr);
    Rectangle(hdc, x, y, x+9, y+9); MoveTo(hdc, x+2, y+4); LineTo(hdc, x+7, y+4); if (!bExpanded) { MoveTo(hdc, x+4, y+2); LineTo(hdc, x+4, y+7); }
    SelectObject(hdc, hOld); SelectObject(hdc, hOldP);
}

/* --- Context Menus --- */
static void ShowContextMenu(HWND hwnd, int x, int y, BOOL isDir, BOOL isBackground) {
    HMENU hCtx = CreatePopupMenu();
    if (isBackground) {
        AppendMenu(hCtx, MF_STRING | (g_ViewMode == 0 ? MF_CHECKED : 0), 4022, "Lar&ge Icons"); AppendMenu(hCtx, MF_STRING | (g_ViewMode == 1 ? MF_CHECKED : 0), 4023, "S&mall Icons"); AppendMenu(hCtx, MF_STRING | (g_ViewMode == 2 ? MF_CHECKED : 0), 4024, "&List"); AppendMenu(hCtx, MF_STRING | (g_ViewMode == 3 ? MF_CHECKED : 0), 4025, "&Details"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4026, "Arrange &Icons"); AppendMenu(hCtx, MF_STRING, 4027, "Lin&e up Icons"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4013, "&Paste"); AppendMenu(hCtx, MF_STRING, 4014, "Paste &Shortcut"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4001, "New &Folder"); AppendMenu(hCtx, MF_STRING, 4002, "New &Shortcut"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4005, "P&roperties");
    } else {
        if (isDir) { AppendMenu(hCtx, MF_STRING, 5001, "&Explore"); AppendMenu(hCtx, MF_STRING, 5002, "&Open"); AppendMenu(hCtx, MF_STRING, 5003, "&Find..."); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); } else { AppendMenu(hCtx, MF_STRING, 5002, "&Open"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); }
        AppendMenu(hCtx, MF_STRING, 4011, "Cu&t"); AppendMenu(hCtx, MF_STRING, 4012, "&Copy"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4002, "Create &Shortcut"); AppendMenu(hCtx, MF_STRING, 4003, "&Delete"); AppendMenu(hCtx, MF_STRING, 4004, "Re&name"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4005, "P&roperties");
    }
    TrackPopupMenu(hCtx, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL); DestroyMenu(hCtx);
}

/* --- Inline Rename Support --- */

LRESULT CALLBACK InlineEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            char newName[MAX_PATH]; GetWindowText(hwnd, newName, MAX_PATH);
            if (newName[0] != '\0') {
                if (g_InlineRenameIsVirtual) {
                    int i; for (i=0; i<g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].id, g_InlineRenameId) == 0) { if (!NameExists(g_IniShortcuts[i].parentId, newName, TRUE)) { lstrcpy(g_IniShortcuts[i].name, newName); SaveIniEntry(&g_IniShortcuts[i]); } else MessageBox(hwnd, "Name exists.", "Error", MB_OK|MB_ICONHAND); break; } }
                } else {
                    char newPath[MAX_PATH]; char *p = strrchr(g_InlineRenameOldPath, '\\');
                    if (p) { int len = p - g_InlineRenameOldPath + 1; lstrcpyn(newPath, g_InlineRenameOldPath, len + 1); lstrcat(newPath, newName); if (!NameExists(g_CurrentPathOrId, newName, FALSE)) rename(g_InlineRenameOldPath, newPath); else MessageBox(hwnd, "Name exists.", "Error", MB_OK|MB_ICONHAND); }
                }
            } HWND hList = GetParent(hwnd); HWND hFolder = GetParent(hList); DestroyWindow(hwnd); PostMessage(hFolder, WM_COMMAND, 4028, 0); return 0;
        } else if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
    } else if (msg == WM_KILLFOCUS) { DestroyWindow(hwnd); return 0; }
    return CallWindowProc((FARPROC)g_lpfnOldInlineEditProc, hwnd, msg, wp, lp);
}

/* --- Dialog Procs --- */

LRESULT CALLBACK ReplaceDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        CreateWindow("STATIC", "File already exists:", WS_CHILD|WS_VISIBLE, 10, 10, 300, 20, hwnd, NULL, g_hInst, NULL);
        CreateWindow("STATIC", g_ReplaceTarget, WS_CHILD|WS_VISIBLE|SS_NOPREFIX, 10, 30, 300, 40, hwnd, NULL, g_hInst, NULL);
        CreateWindow("BUTTON", "Overwrite", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 10, 80, 70, 24, hwnd, (HMENU)101, g_hInst, NULL);
        CreateWindow("BUTTON", "All", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 85, 80, 50, 24, hwnd, (HMENU)102, g_hInst, NULL);
        CreateWindow("BUTTON", "Skip", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 140, 80, 50, 24, hwnd, (HMENU)103, g_hInst, NULL);
        CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 195, 80, 60, 24, hwnd, (HMENU)104, g_hInst, NULL);
        return 0;
    }
    if (msg == WM_COMMAND) { g_ReplaceResult = wp - 100; return 0; }
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK CopyProgressDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE:
            CreateWindow("STATIC", g_CurrentJob.isMove ? "Moving..." : "Copying...", WS_CHILD|WS_VISIBLE|SS_CENTER, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            CreateWindow("STATIC", g_CurrentJob.src, WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX, 10, 40, 260, 20, hwnd, (HMENU)101, g_hInst, NULL);
            CreateWindow("STATIC", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 10, 70, 260, 20, hwnd, (HMENU)102, g_hInst, NULL);
            SetTimer(hwnd, 1, 100, NULL); return 0;
        case WM_TIMER:
            KillTimer(hwnd, 1);
            if (g_CurrentJob.isMove) { if (rename(g_CurrentJob.src, g_CurrentJob.dst) != 0) { if (g_CurrentJob.isDir) DoCopyFolder(hwnd, g_CurrentJob.src, g_CurrentJob.dst); else DoCopyFile(hwnd, g_CurrentJob.src, g_CurrentJob.dst); if (g_ReplaceMode != 3) { if (g_CurrentJob.isDir) { g_bCancelDel = FALSE; DoDeleteFolder(g_CurrentJob.src); rmdir(g_CurrentJob.src); } else remove(g_CurrentJob.src); } } } 
            else { if (g_CurrentJob.isDir) DoCopyFolder(hwnd, g_CurrentJob.src, g_CurrentJob.dst); else DoCopyFile(hwnd, g_CurrentJob.src, g_CurrentJob.dst); }
            { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); } return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK DeleteProgressDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE:
            g_hDelDlg = hwnd; CreateWindow("STATIC", "Deleting...", WS_CHILD|WS_VISIBLE|SS_CENTER, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            CreateWindow("STATIC", g_DelPath, WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX, 10, 40, 260, 20, hwnd, (HMENU)101, g_hInst, NULL);
            CreateWindow("STATIC", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 10, 70, 260, 20, hwnd, (HMENU)102, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 100, 100, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            SetTimer(hwnd, 1, 50, NULL); return 0;
        case WM_TIMER:
            KillTimer(hwnd, 1);
            if (g_DelIsDir) { DoDeleteFolder(g_DelPath); if (!g_bCancelDel) rmdir(g_DelPath); } else remove(g_DelPath);
            { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); } return 0;
        case WM_COMMAND: if (wp == IDCANCEL) g_bCancelDel = TRUE; return 0;
        case WM_DESTROY: g_hDelDlg = NULL; return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit, hParentFolder; static char parentId[MAX_PATH]; static char parentSel[128]; static IniShortcut sh;
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", g_PromptLabel, WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            hEdit = CreateWindowEx(0, "EDIT", g_PromptValue, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER, 10, 35, 260, 22, hwnd, NULL, g_hInst, NULL);
            if (g_PromptMode == PROMPT_NEWFOLDER) {
                int i; CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 65, 100, 20, hwnd, NULL, g_hInst, NULL); hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP, 110, 65, 160, 150, hwnd, NULL, g_hInst, NULL);
                SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"0 (Root)"); for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i].isFolder) { char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i].id, g_IniShortcuts[i].name); SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)buf); } }
                lstrcpy(parentId, g_ContextId[0] ? g_ContextId : "0"); for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) { char buf[128]; buf[0] = '\0'; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)(LPSTR)buf); int pLen = lstrlen(parentId); if (pLen < lstrlen(buf)) { BOOL match = TRUE; int j; for (j=0; j<pLen; j++) { if (buf[j] != parentId[j]) { match = FALSE; break; } } if (match && buf[pLen] == ' ') { SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break; } } }
                if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0);
                CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 50, 100, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL); CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 150, 100, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            } SetFocus(hEdit); return 0;
        }
        case WM_COMMAND:
            if (wp == IDOK) {
                GetWindowText(hEdit, g_PromptValue, MAX_PATH);
                if (g_PromptValue[0] != '\0' && g_PromptMode == PROMPT_NEWFOLDER) {
                    char newId[16]; char* space; GetNewIniId(newId); lstrcpy(sh.id, newId); lstrcpy(sh.name, g_PromptValue); sh.exe[0] = '\0'; sh.params[0] = '\0'; sh.icon[0] = '\0'; sh.minimized = 0; sh.hotkey[0] = '\0';
                    if (hParentFolder) { GetWindowText(hParentFolder, parentSel, sizeof(parentSel)); space = strchr(parentSel, ' '); if (space) *space = '\0'; lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0"); } else lstrcpy(sh.parentId, g_ContextId[0] ? g_ContextId : "0");
                    if (NameExists(sh.parentId, g_PromptValue, TRUE)) { MessageBox(hwnd, "Name already exists.", "Error", MB_OK|MB_ICONHAND); return 0; }
                    sh.isFolder = TRUE; SaveIniEntry(&sh);
                } HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); 
            } else if (wp == IDCANCEL) { g_PromptValue[0] = '\0'; HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
        case WM_CLOSE: { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hName, hTarget, hParams, hIcon, hMinCheck, hFolderCheck, hMapDirCheck, hParentFolder, hTargetLbl;
    static char name[MAX_PATH], target[MAX_PATH], params[MAX_PATH], iconF[MAX_PATH], parentId[MAX_PATH]; static char parentSel[128]; static IniShortcut sh;
    switch(msg) {
        case WM_CREATE: {
            int minimized = 0, isFolder = 0, bMapDir = 0, i; name[0] = '\0'; target[0] = '\0'; params[0] = '\0'; iconF[0] = '\0'; lstrcpy(parentId, "0");
            if (g_EditShortcutId[0] != '\0') { for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].id, g_EditShortcutId) == 0) { lstrcpy(name, g_IniShortcuts[i].name); lstrcpy(target, g_IniShortcuts[i].exe); lstrcpy(params, g_IniShortcuts[i].params); lstrcpy(iconF, g_IniShortcuts[i].icon); lstrcpyn(parentId, g_IniShortcuts[i].parentId, MAX_PATH - 1); minimized = g_IniShortcuts[i].minimized; isFolder = g_IniShortcuts[i].isFolder; bMapDir = (isFolder && target[0] != '\0'); break; } } } else if (g_ContextId[0] != '\0') lstrcpyn(parentId, g_ContextId, MAX_PATH - 1);
            CreateWindow("STATIC", "Name:", WS_CHILD|WS_VISIBLE, 10, 10, 100, 20, hwnd, NULL, g_hInst, NULL); hName = CreateWindowEx(0, "EDIT", name, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 10, 210, 22, hwnd, NULL, g_hInst, NULL); hTargetLbl = CreateWindow("STATIC", isFolder ? (bMapDir ? "Dir Path:" : "Target:") : "Target (File):", WS_CHILD|WS_VISIBLE, 10, 40, 100, 20, hwnd, NULL, g_hInst, NULL); hTarget = CreateWindowEx(0, "EDIT", target, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 40, 150, 22, hwnd, NULL, g_hInst, NULL); CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 270, 40, 50, 22, hwnd, (HMENU)101, g_hInst, NULL); CreateWindow("STATIC", "Parameters:", WS_CHILD|WS_VISIBLE, 10, 70, 100, 20, hwnd, NULL, g_hInst, NULL); hParams = CreateWindowEx(0, "EDIT", params, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 70, 210, 22, hwnd, NULL, g_hInst, NULL); CreateWindow("STATIC", "Icon File:", WS_CHILD|WS_VISIBLE, 10, 100, 100, 20, hwnd, NULL, g_hInst, NULL); hIcon = CreateWindowEx(0, "EDIT", iconF, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 100, 150, 22, hwnd, NULL, g_hInst, NULL); CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 270, 100, 50, 22, hwnd, (HMENU)102, g_hInst, NULL); CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 130, 100, 20, hwnd, NULL, g_hInst, NULL); hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL, 110, 130, 210, 150, hwnd, NULL, g_hInst, NULL); hMinCheck = CreateWindow("BUTTON", "Start Minimized", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 110, 160, 120, 20, hwnd, NULL, g_hInst, NULL); hFolderCheck = CreateWindow("BUTTON", "Is Folder", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 240, 160, 80, 20, hwnd, (HMENU)103, g_hInst, NULL); hMapDirCheck = CreateWindow("BUTTON", "Map Dir to Menu", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 110, 185, 140, 20, hwnd, (HMENU)104, g_hInst, NULL); CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 80, 220, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL); CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 180, 220, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"0 (Root)"); for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i].isFolder) { char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i].id, g_IniShortcuts[i].name); SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)buf); } }
            for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) { char buf[128]; buf[0] = '\0'; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)(LPSTR)buf); int pLen = lstrlen(parentId); if (pLen < lstrlen(buf)) { BOOL match = TRUE; int j; for (j=0; j<pLen; j++) { if (buf[j] != parentId[j]) { match = FALSE; break; } } if (match && buf[pLen] == ' ') { SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break; } } } if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0);
            if (minimized) SendMessage(hMinCheck, BM_SETCHECK, 1, 0); SendMessage(hFolderCheck, BM_SETCHECK, isFolder, 0); SendMessage(hMapDirCheck, BM_SETCHECK, bMapDir, 0);
            if (isFolder) { EnableWindow(hParams, FALSE); EnableWindow(hMinCheck, FALSE); EnableWindow(hMapDirCheck, TRUE); if (bMapDir) { EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); } else { EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE); } } else { EnableWindow(hMapDirCheck, FALSE); }
            SetFocus(hName); return 0;
        }
        case WM_COMMAND:
            if (wp == 101) { 
                char path[MAX_PATH]; 
                if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) { MessageBox(hwnd, "Select any file inside the mapped directory.", "Select", MB_OK); if (BrowseFile(hwnd, path, "All Files (*.*)\0*.*\0")) { char* pDir = strrchr(path, '\\'); if (pDir) { if (pDir == path || *(pDir - 1) == ':') *(pDir + 1) = '\0'; else *pDir = '\0'; } SetWindowText(hTarget, path); } } 
                else { if (BrowseFile(hwnd, path, "Programs (*.exe;*.com;*.bat)\0*.exe;*.com;*.bat\0All Files (*.*)\0*.*\0")) { SetWindowText(hTarget, path); if (GetWindowTextLength(hIcon) == 0) SetWindowText(hIcon, path); if (GetWindowTextLength(hName) == 0) { char base[MAX_PATH], *p, *dot; lstrcpy(base, path); p = strrchr(base, '\\'); if (p) lstrcpy(base, p + 1); dot = strrchr(base, '.'); if (dot) *dot = '\0'; if (base[0]) { AnsiLower((LPSTR)base); if (base[0] >= 'a' && base[0] <= 'z') base[0] -= 32; } SetWindowText(hName, base); } } }
            } else if (wp == 102) { char path[MAX_PATH]; if (BrowseFile(hwnd, path, "Icons (*.exe;*.ico)\0*.exe;*.ico\0All Files (*.*)\0*.*\0")) SetWindowText(hIcon, path); } else if (wp == 103) { if (SendMessage(hFolderCheck, BM_GETCHECK, 0, 0)) { EnableWindow(hParams, FALSE); SetWindowText(hParams, ""); EnableWindow(hMinCheck, FALSE); EnableWindow(hMapDirCheck, TRUE); if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) { SetWindowText(hTargetLbl, "Dir Path:"); EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); } else { SetWindowText(hTargetLbl, "Target:"); EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE); } } else { EnableWindow(hMapDirCheck, FALSE); SendMessage(hMapDirCheck, BM_SETCHECK, 0, 0); SetWindowText(hTargetLbl, "Target (File):"); EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); EnableWindow(hParams, TRUE); EnableWindow(hMinCheck, TRUE); } } else if (wp == 104) { if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) { SetWindowText(hTargetLbl, "Dir Path:"); EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); } else { SetWindowText(hTargetLbl, "Target:"); SetWindowText(hTarget, ""); EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE); } } else if (wp == IDOK) {
                char *space; GetWindowText(hName, name, MAX_PATH); GetWindowText(hTarget, target, MAX_PATH); GetWindowText(hParams, params, MAX_PATH); GetWindowText(hIcon, iconF, MAX_PATH); GetWindowText(hParentFolder, parentSel, sizeof(parentSel)); space = strchr(parentSel, ' '); if (space) *space = '\0';
                if (name[0] && (target[0] || SendMessage(hFolderCheck, BM_GETCHECK, 0, 0))) {
                    memset(&sh, 0, sizeof(sh)); if (g_EditShortcutId[0] != '\0') { int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].id, g_EditShortcutId) == 0) { sh = g_IniShortcuts[i]; break; } } } else { char newId[16]; GetNewIniId(newId); lstrcpy(sh.id, newId); }
                    lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0"); lstrcpy(sh.name, name); lstrcpy(sh.exe, target); lstrcpy(sh.params, params); lstrcpy(sh.icon, iconF); sh.hotkey[0] = '\0'; sh.minimized = SendMessage(hMinCheck, BM_GETCHECK, 0, 0) ? 1 : 0; sh.isFolder = SendMessage(hFolderCheck, BM_GETCHECK, 0, 0) ? 1 : 0; if (sh.isFolder) { sh.params[0] = '\0'; if (!SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) sh.exe[0] = '\0'; }
                    if (g_EditShortcutId[0] == '\0' || lstrcmpi(sh.name, name) != 0) { if (NameExists(sh.parentId, name, TRUE)) { MessageBox(hwnd, "Name exists.", "Error", MB_OK|MB_ICONHAND); return 0; } } SaveIniEntry(&sh);
                } HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); 
            } else if (wp == IDCANCEL) { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
        case WM_CLOSE: { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

/* --- Subclass Procedures --- */

LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc); FillRect(hdc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        Draw3DButton(hdc, 2, 2, 26, 26, FALSE); DrawGDIFolder(hdc, 5, 8, TRUE, FALSE, FALSE); MoveTo(hdc, 14, 15); LineTo(hdc, 14, 8); MoveTo(hdc, 11, 11); LineTo(hdc, 14, 8); MoveTo(hdc, 17, 11); LineTo(hdc, 14, 8);
        Draw3DButton(hdc, 30, 2, 54, 26, FALSE); Ellipse(hdc, 34, 14, 40, 20); Ellipse(hdc, 44, 14, 50, 20); MoveTo(hdc, 38, 15); LineTo(hdc, 48, 6); MoveTo(hdc, 46, 15); LineTo(hdc, 36, 6);
        Draw3DButton(hdc, 58, 2, 82, 26, FALSE); Rectangle(hdc, 65, 9, 75, 21); Rectangle(hdc, 61, 5, 71, 17);
        Draw3DButton(hdc, 86, 2, 110, 26, FALSE); Rectangle(hdc, 90, 6, 102, 22); Rectangle(hdc, 93, 10, 105, 20); Rectangle(hdc, 94, 4, 98, 7);
        Draw3DButton(hdc, 114, 2, 138, 26, FALSE); MoveTo(hdc, 120, 8); LineTo(hdc, 132, 20); MoveTo(hdc, 120, 20); LineTo(hdc, 132, 8);
        Draw3DButton(hdc, 142, 2, 166, 26, FALSE); Rectangle(hdc, 148, 6, 160, 22); MoveTo(hdc, 150, 10); LineTo(hdc, 158, 10); MoveTo(hdc, 150, 14); LineTo(hdc, 158, 14);
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        int x = LOWORD(lp);
        if (x >= 2 && x <= 26) PostMessage(GetParent(hwnd), WM_COMMAND, 4050, 0); 
        else if (x >= 30 && x <= 54) PostMessage(GetParent(hwnd), WM_COMMAND, 4011, 0); 
        else if (x >= 58 && x <= 82) PostMessage(GetParent(hwnd), WM_COMMAND, 4012, 0); 
        else if (x >= 86 && x <= 110) PostMessage(GetParent(hwnd), WM_COMMAND, 4013, 0); 
        else if (x >= 114 && x <= 138) PostMessage(GetParent(hwnd), WM_COMMAND, 4003, 0); 
        else if (x >= 142 && x <= 166) PostMessage(GetParent(hwnd), WM_COMMAND, 4005, 0); 
        return 0;
    }
    return CallWindowProc((FARPROC)g_lpfnOldToolbarProc, hwnd, msg, wp, lp); 
}

LRESULT CALLBACK TreeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONUP) {
        int x = LOWORD(lp); int y = HIWORD(lp); int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0); int topIdx = SendMessage(hwnd, LB_GETTOPINDEX, 0, 0); int hitIdx = -1; int i;
        for (i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hwnd, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (y >= rc.top && y <= rc.bottom) { hitIdx = i; break; } } }
        if (hitIdx >= 0) {
            if (msg == WM_LBUTTONDOWN) {
                TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, hitIdx, 0);
                if (item && item->hasChildren) {
                    int pmX = item->level * 16 + 2;
                    if (x >= pmX && x <= pmX + 12) { ToggleExpand(item->pathOrId); RebuildTree(hwnd); return 0; }
                }
            } else if (msg == WM_RBUTTONUP) {
                TreeItemData FAR* item; POINT pt; pt.x = x; pt.y = y; SendMessage(hwnd, LB_SETCURSEL, hitIdx, 0); item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, hitIdx, 0);
                if (item) { g_ContextIsVirtual = item->isVirtual; lstrcpy(g_ContextId, item->pathOrId); g_ContextIsFolder = TRUE; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, TRUE, FALSE); } return 0;
            }
        }
    }
    return CallWindowProc((FARPROC)g_lpfnOldTreeProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && (g_ViewMode == 0 || g_ViewMode == 1)) { g_SelectedListItemPath[0] = '\0'; InvalidateRect(hwnd, NULL, FALSE); }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONUP) {
        int x = LOWORD(lp); int y = HIWORD(lp); int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0); int topIdx = SendMessage(hwnd, LB_GETTOPINDEX, 0, 0); int hitIdx = -1; int i;
        for (i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hwnd, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (y >= rc.top && y <= rc.bottom) { hitIdx = i; break; } } }
        if (hitIdx >= 0) {
            int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, hitIdx, 0);
            if (pType && *pType == 2 && (g_ViewMode == 0 || g_ViewMode == 1)) {
                RowItemData FAR* row = (RowItemData FAR*)pType; int colW = (g_ViewMode == 0) ? CELL_W : 150; int col = x / colW;
                if (col >= 0 && col < row->count) {
                    ListItemData FAR* item = row->items[col]; lstrcpy(g_SelectedListItemPath, item->path); lstrcpy(g_SelectedListItemName, item->name); g_SelectedListItemIsVirtual = item->isVirtual; g_SelectedListItemIsDir = item->isDir; InvalidateRect(hwnd, NULL, FALSE);
                    if (msg == WM_LBUTTONDBLCLK) {
                        if (item->isDir) { lstrcpy(g_CurrentPathOrId, item->path); g_CurrentIsVirtual = item->isVirtual; SetWindowText(GetParent(hwnd), item->name); if (g_CurrentIsVirtual) ExpandAllParentsVirtual(g_CurrentPathOrId); else ExpandAllParentsFS(g_CurrentPathOrId); if (!IsExpanded(item->path)) ToggleExpand(item->path); RebuildTree(GetDlgItem(GetParent(hwnd), ID_TREE)); RebuildList(GetParent(hwnd)); } else { if (item->isVirtual) { int k; for (k = 0; k < g_IniShortcutCount; k++) { if (lstrcmpi(g_IniShortcuts[k].id, item->path) == 0) { if (g_IniShortcuts[k].exe[0]) ShellExecute(hwnd, "open", g_IniShortcuts[k].exe, g_IniShortcuts[k].params, NULL, SW_SHOWNORMAL); break; } } } else ShellExecute(hwnd, "open", item->path, NULL, NULL, SW_SHOWNORMAL); }
                    } else if (msg == WM_RBUTTONUP) { g_ContextIsVirtual = item->isVirtual; lstrcpy(g_ContextId, item->path); g_ContextIsFolder = item->isDir; POINT pt; pt.x = x; pt.y = y; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, item->isDir, FALSE); }
                } else if (msg == WM_LBUTTONDOWN) { g_SelectedListItemPath[0] = '\0'; InvalidateRect(hwnd, NULL, FALSE); } 
                else if (msg == WM_RBUTTONUP) { POINT pt; pt.x = x; pt.y = y; g_ContextIsVirtual = g_CurrentIsVirtual; lstrcpy(g_ContextId, g_CurrentPathOrId); g_ContextIsFolder = TRUE; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, TRUE, TRUE); } return 0;
            } else if (msg == WM_RBUTTONUP && pType && *pType == 1) {
                ListItemData FAR* item; POINT pt; pt.x = x; pt.y = y; SendMessage(hwnd, LB_SETSEL, FALSE, -1); SendMessage(hwnd, LB_SETSEL, TRUE, hitIdx); SendMessage(hwnd, LB_SETCARETINDEX, hitIdx, 0); item = (ListItemData FAR*)pType;
                g_ContextIsVirtual = item->isVirtual; lstrcpy(g_ContextId, item->path); g_ContextIsFolder = item->isDir; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, item->isDir, FALSE); return 0;
            }
        }
        if (msg == WM_RBUTTONUP) { POINT pt; pt.x = x; pt.y = y; g_ContextIsVirtual = g_CurrentIsVirtual; lstrcpy(g_ContextId, g_CurrentPathOrId); g_ContextIsFolder = TRUE; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, TRUE, TRUE); return 0; }
    }
    return CallWindowProc((FARPROC)g_lpfnOldListProc, hwnd, msg, wp, lp);
}

/* --- Window Procedures --- */

LRESULT CALLBACK DesktopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: LoadConfig(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rcClient; int rows, idx = 0, i; HFONT hFont = GetStockObject(DEFAULT_GUI_FONT); HFONT hOldFont = SelectObject(hdc, hFont); GetClientRect(hwnd, &rcClient); rows = rcClient.bottom / CELL_H; if (rows < 1) rows = 1; SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(255, 255, 255));
            for (i = 0; i < g_IniShortcutCount; i++) {
                if (lstrcmp(g_IniShortcuts[i].parentId, "0") == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) {
                    int r = idx % rows; int c = idx / rows; int x = c * CELL_W; int y = r * CELL_H; RECT textRect; BOOL bSel = (i == g_DesktopSelectedIndex);
                    if (g_IniShortcuts[i].isFolder) DrawGDIFolder(hdc, x + (CELL_W - 32)/2, y + 5, FALSE, bSel, TRUE); else DrawGDIFile(hdc, x + (CELL_W - 32)/2, y + 5, bSel, TRUE);
                    textRect.left = x + 2; textRect.right = x + CELL_W - 2; textRect.top = y + 42; textRect.bottom = y + CELL_H;
                    if (bSel) { FillRect(hdc, &textRect, CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT))); SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT)); } 
                    else { SetTextColor(hdc, RGB(255, 255, 255)); }
                    DrawText(hdc, g_IniShortcuts[i].name, -1, &textRect, DT_CENTER | DT_WORDBREAK); idx++;
                }
            } SelectObject(hdc, hOldFont); EndPaint(hwnd, &ps); return 0;
        }
        case WM_LBUTTONDOWN: {
            RECT rc; int rows, c, r, idx = 0, hitIndex = -1, i; int x = LOWORD(lp); int y = HIWORD(lp); GetClientRect(hwnd, &rc); rows = rc.bottom / CELL_H; if (rows < 1) rows = 1; c = x / CELL_W; r = y / CELL_H; idx = c * rows + r;
            for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].parentId, "0") == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) { if (idx == 0) { hitIndex = i; break; } idx--; } }
            if (hitIndex != g_DesktopSelectedIndex) { g_DesktopSelectedIndex = hitIndex; InvalidateRect(hwnd, NULL, TRUE); } return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (g_DesktopSelectedIndex >= 0) {
                IniShortcut* sh = &g_IniShortcuts[g_DesktopSelectedIndex];
                if (sh->isFolder) { char path[MAX_PATH]; if (sh->exe[0]) lstrcpy(path, sh->exe); else lstrcpy(path, sh->id); CreateWindowEx(0, "Win95FolderClass", sh->name, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, g_hInst, (LPVOID)path); } 
                else if (sh->exe[0]) ShellExecute(hwnd, "open", sh->exe, sh->params[0] ? sh->params : NULL, NULL, SW_SHOWNORMAL);
            } return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt; pt.x = LOWORD(lp); pt.y = HIWORD(lp); ClientToScreen(hwnd, &pt);
            if (g_DesktopSelectedIndex >= 0) { g_ContextIsVirtual = TRUE; lstrcpy(g_ContextId, g_IniShortcuts[g_DesktopSelectedIndex].id); ShowContextMenu(hwnd, pt.x, pt.y, g_IniShortcuts[g_DesktopSelectedIndex].isFolder, FALSE); } 
            else { g_ContextIsVirtual = TRUE; lstrcpy(g_ContextId, "0"); ShowContextMenu(hwnd, pt.x, pt.y, TRUE, TRUE); } return 0;
        }
        case WM_COMMAND: {
            int id = wp;
            if (id == 4028) { LoadIniShortcuts(); InvalidateRect(hwnd, NULL, TRUE); return 0; }
            if (id >= 4011 && id <= 4015) {
                if (id == 4011 || id == 4012) {
                    if (g_DesktopSelectedIndex >= 0) {
                        lstrcpy(g_ClipPath, g_IniShortcuts[g_DesktopSelectedIndex].id); lstrcpy(g_ClipName, g_IniShortcuts[g_DesktopSelectedIndex].name);
                        g_ClipIsVirtual = TRUE; g_ClipIsDir = g_IniShortcuts[g_DesktopSelectedIndex].isFolder;
                        g_ClipOp = (id == 4011) ? 2 : 1; lstrcpy(g_ClipId, g_IniShortcuts[g_DesktopSelectedIndex].id);
                    }
                }
                else if (id == 4013) {
                    if (g_ClipOp == 0) return 0; char newId[16]; IniShortcut sh; memset(&sh, 0, sizeof(sh)); GetNewIniId(newId); lstrcpy(sh.id, newId);
                    lstrcpy(sh.name, g_ClipName); lstrcpy(sh.parentId, "0"); sh.isFolder = g_ClipIsDir;
                    if (g_ClipIsVirtual) { int i; for(i=0; i<g_IniShortcutCount; i++) if (lstrcmp(g_IniShortcuts[i].id, g_ClipId) == 0) { lstrcpy(sh.exe, g_IniShortcuts[i].exe); lstrcpy(sh.params, g_IniShortcuts[i].params); lstrcpy(sh.icon, g_IniShortcuts[i].icon); sh.minimized = g_IniShortcuts[i].minimized; break; }
                        if (g_ClipOp == 2) { WritePrivateProfileString("Shortcut", g_ClipId, NULL, g_szExplorerIni); g_ClipOp = 0; }
                    } else lstrcpy(sh.exe, g_ClipPath);
                    if (!NameExists("0", sh.name, TRUE)) SaveIniEntry(&sh); LoadIniShortcuts(); InvalidateRect(hwnd, NULL, TRUE);
                }
                else if (id == 4014) {
                    if (g_ClipOp == 0) return 0; char newId[16]; IniShortcut sh; memset(&sh, 0, sizeof(sh)); GetNewIniId(newId); lstrcpy(sh.id, newId);
                    sprintf(sh.name, "Shortcut to %s", g_ClipName); lstrcpy(sh.parentId, "0"); sh.isFolder = FALSE;
                    if (g_ClipIsVirtual) { int i; for(i=0; i<g_IniShortcutCount; i++) if (lstrcmp(g_IniShortcuts[i].id, g_ClipId) == 0) { lstrcpy(sh.exe, g_IniShortcuts[i].exe); lstrcpy(sh.params, g_IniShortcuts[i].params); lstrcpy(sh.icon, g_IniShortcuts[i].icon); sh.minimized = g_IniShortcuts[i].minimized; break; } } 
                    else lstrcpy(sh.exe, g_ClipPath); if (!NameExists("0", sh.name, TRUE)) SaveIniEntry(&sh); LoadIniShortcuts(); InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            if (id >= 4001 && id <= 4005) {
                if (id == 4001 || id == 4002) { lstrcpy(g_ContextId, "0"); g_ContextIsVirtual = TRUE; } 
                else if (g_DesktopSelectedIndex >= 0) lstrcpy(g_ContextId, g_IniShortcuts[g_DesktopSelectedIndex].id);
                else return 0;
                
                if (id == 4001) { lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0'; g_PromptMode = PROMPT_NEWFOLDER; CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 170); }
                else if (id == 4002) { g_EditShortcutId[0] = '\0'; CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 360, 300); }
                else if (id == 4003 && g_ContextId[0] != '\0') {
                    char msgBuf[256]; sprintf(msgBuf, "Are you sure you want to delete '%s'?", g_ContextId);
                    if (MessageBox(hwnd, msgBuf, "Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        WritePrivateProfileString("Shortcut", g_ContextId, NULL, g_szExplorerIni); LoadIniShortcuts(); InvalidateRect(hwnd, NULL, TRUE); 
                    }
                }
                else if (id == 4004 && g_DesktopSelectedIndex >= 0) {
                    RECT rcClient; GetClientRect(hwnd, &rcClient); int rows = rcClient.bottom / CELL_H; if (rows < 1) rows = 1; int idx = 0, i;
                    for (i = 0; i < g_IniShortcutCount; i++) {
                        if (lstrcmp(g_IniShortcuts[i].parentId, "0") == 0 && lstrcmp(g_IniShortcuts[i].name, "-") != 0) {
                            if (idx == g_DesktopSelectedIndex) {
                                int r = idx % rows; int c = idx / rows; int x = c * CELL_W; int y = r * CELL_H; RECT eRc; eRc.left = x + 2; eRc.right = x + CELL_W - 2; eRc.top = y + 42; eRc.bottom = y + CELL_H; g_InlineRenameIsVirtual = TRUE; lstrcpy(g_InlineRenameId, g_IniShortcuts[i].id); HWND hEdit = CreateWindowEx(0, "EDIT", g_IniShortcuts[i].name, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_CENTER, eRc.left, eRc.top, eRc.right - eRc.left, eRc.bottom - eRc.top, hwnd, (HMENU)9999, g_hInst, NULL); g_lpfnOldInlineEditProc = (FARPROC)SetWindowLong(hEdit, GWL_WNDPROC, (LONG)MakeProcInstance((FARPROC)InlineEditProc, g_hInst)); SetFocus(hEdit); SendMessage(hEdit, EM_SETSEL, 0, MAKELONG(0, -1)); break;
                            } idx++;
                        }
                    }
                }
                else if (id == 4005) { lstrcpy(g_EditShortcutId, g_ContextId); CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Properties", 360, 300); }
            } return 0;
        }
        case WM_SIZE: InvalidateRect(hwnd, NULL, TRUE); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK FolderWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lp; char* targetPath = (char*)cs->lpCreateParams; HWND hTree, hToolbar; HMENU hMenu, hFile, hEdit, hView, hTools, hHelp; LoadConfig();
            if (targetPath && targetPath[0] != '\0') { lstrcpy(g_CurrentPathOrId, targetPath); g_CurrentIsVirtual = FALSE; ExpandAllParentsFS(targetPath); ToggleExpand(targetPath); } 
            else { lstrcpy(g_CurrentPathOrId, "0"); g_CurrentIsVirtual = TRUE; ToggleExpand("0"); }

            hMenu = CreateMenu(); hFile = CreatePopupMenu(); AppendMenu(hFile, MF_STRING, 4001, "&New\t"); AppendMenu(hFile, MF_SEPARATOR, 0, NULL); AppendMenu(hFile, MF_STRING, 4002, "Create &Shortcut"); AppendMenu(hFile, MF_STRING, 4003, "&Delete"); AppendMenu(hFile, MF_STRING, 4004, "Re&name"); AppendMenu(hFile, MF_STRING, 4005, "P&roperties"); AppendMenu(hFile, MF_SEPARATOR, 0, NULL); AppendMenu(hFile, MF_STRING, 4006, "&Close"); AppendMenu(hMenu, MF_POPUP, (UINT)hFile, "&File");
            hEdit = CreatePopupMenu(); AppendMenu(hEdit, MF_STRING, 4010, "&Undo"); AppendMenu(hEdit, MF_SEPARATOR, 0, NULL); AppendMenu(hEdit, MF_STRING, 4011, "Cu&t"); AppendMenu(hEdit, MF_STRING, 4012, "&Copy"); AppendMenu(hEdit, MF_STRING, 4013, "&Paste"); AppendMenu(hEdit, MF_STRING, 4014, "Paste &Shortcut"); AppendMenu(hEdit, MF_SEPARATOR, 0, NULL); AppendMenu(hEdit, MF_STRING, 4015, "Select &All"); AppendMenu(hEdit, MF_STRING, 4016, "&Invert Selection"); AppendMenu(hMenu, MF_POPUP, (UINT)hEdit, "&Edit");
            hView = CreatePopupMenu(); AppendMenu(hView, MF_STRING | (g_bShowToolbar ? MF_CHECKED : 0), 4020, "&Toolbar"); AppendMenu(hView, MF_STRING | (g_bShowStatusBar ? MF_CHECKED : 0), 4021, "&Status Bar"); AppendMenu(hView, MF_SEPARATOR, 0, NULL); AppendMenu(hView, MF_STRING | (g_ViewMode == 0 ? MF_CHECKED : 0), 4022, "Lar&ge Icons"); AppendMenu(hView, MF_STRING | (g_ViewMode == 1 ? MF_CHECKED : 0), 4023, "S&mall Icons"); AppendMenu(hView, MF_STRING | (g_ViewMode == 2 ? MF_CHECKED : 0), 4024, "&List"); AppendMenu(hView, MF_STRING | (g_ViewMode == 3 ? MF_CHECKED : 0), 4025, "&Details"); AppendMenu(hView, MF_SEPARATOR, 0, NULL); AppendMenu(hView, MF_STRING, 4026, "Arrange &Icons"); AppendMenu(hView, MF_STRING, 4027, "Lin&e up Icons"); AppendMenu(hView, MF_SEPARATOR, 0, NULL); AppendMenu(hView, MF_STRING, 4028, "&Refresh"); AppendMenu(hView, MF_STRING, 4029, "&Options..."); AppendMenu(hMenu, MF_POPUP, (UINT)hView, "&View");
            hTools = CreatePopupMenu(); AppendMenu(hTools, MF_STRING, 4030, "&Find"); AppendMenu(hTools, MF_SEPARATOR, 0, NULL); 
            AppendMenu(hTools, MF_STRING, 4033, "&Go to..."); AppendMenu(hMenu, MF_POPUP, (UINT)hTools, "&Tools");
            hHelp = CreatePopupMenu(); AppendMenu(hHelp, MF_STRING, 4040, "&Help Topics"); AppendMenu(hHelp, MF_SEPARATOR, 0, NULL); AppendMenu(hHelp, MF_STRING, 4041, "&About Calmira"); AppendMenu(hMenu, MF_POPUP, (UINT)hHelp, "&Help"); SetMenu(hwnd, hMenu);

            hToolbar = CreateWindowEx(0, "STATIC", "", WS_CHILD | (g_bShowToolbar ? WS_VISIBLE : 0), 0, 0, 0, 0, hwnd, (HMENU)ID_TOOLBAR, g_hInst, NULL); g_lpfnToolbarProc = MakeProcInstance((FARPROC)ToolbarProc, g_hInst); g_lpfnOldToolbarProc = (FARPROC)SetWindowLong(hToolbar, GWL_WNDPROC, (LONG)g_lpfnToolbarProc);
            CreateWindow("BUTTON", "Name", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_NAME, g_hInst, NULL); CreateWindow("BUTTON", "Size", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_SIZE, g_hInst, NULL); CreateWindow("BUTTON", "Type", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_TYPE, g_hInst, NULL); CreateWindow("BUTTON", "Modified", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_DATE, g_hInst, NULL);
            hTree = CreateWindowEx(0, "LISTBOX", "", WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_BORDER, 0, 0, 0, 0, hwnd, (HMENU)ID_TREE, g_hInst, NULL); SendMessage(hTree, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE); g_lpfnTreeProc = MakeProcInstance((FARPROC)TreeProc, g_hInst); g_lpfnOldTreeProc = (FARPROC)SetWindowLong(hTree, GWL_WNDPROC, (LONG)g_lpfnTreeProc);
            g_lpfnListProc = MakeProcInstance((FARPROC)ListProc, g_hInst); ChangeViewMode(hwnd, g_ViewMode);
            CreateWindowEx(0, "STATIC", " Ready", WS_CHILD | (g_bShowStatusBar ? WS_VISIBLE : 0) | WS_BORDER | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)300, g_hInst, NULL); SendMessage(GetDlgItem(hwnd, 300), WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE); RebuildTree(hTree); return 0;
        }
        case WM_SIZE: {
            int cx = LOWORD(lp); int cy = HIWORD(lp); int tbH = g_bShowToolbar ? 28 : 0; int statH = g_bShowStatusBar ? 22 : 0; int listX = g_SplitX + 4; int listW = cx - listX; int hdrH = (g_ViewMode == 3) ? 22 : 0;
            if (g_bShowToolbar) MoveWindow(GetDlgItem(hwnd, ID_TOOLBAR), 0, 0, cx, tbH, TRUE); MoveWindow(GetDlgItem(hwnd, ID_TREE), 0, tbH, g_SplitX, cy - statH - tbH, TRUE);
            if (g_ViewMode == 3) { MoveWindow(GetDlgItem(hwnd, ID_HDR_NAME), listX, tbH, 150, hdrH, TRUE); MoveWindow(GetDlgItem(hwnd, ID_HDR_SIZE), listX + 150, tbH, 70, hdrH, TRUE); MoveWindow(GetDlgItem(hwnd, ID_HDR_TYPE), listX + 220, tbH, 60, hdrH, TRUE); MoveWindow(GetDlgItem(hwnd, ID_HDR_DATE), listX + 280, tbH, listW - 280 > 0 ? listW - 280 : 80, hdrH, TRUE); ShowWindow(GetDlgItem(hwnd, ID_HDR_NAME), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_SIZE), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_TYPE), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_DATE), SW_SHOW); } else { ShowWindow(GetDlgItem(hwnd, ID_HDR_NAME), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_SIZE), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_TYPE), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_DATE), SW_HIDE); }
            MoveWindow(GetDlgItem(hwnd, ID_LIST), listX, tbH + hdrH, listW, cy - hdrH - statH - tbH, TRUE); if (g_bShowStatusBar) MoveWindow(GetDlgItem(hwnd, 300), 0, cy - statH, cx, statH, TRUE);
            if (g_ViewMode == 0 || g_ViewMode == 1) RebuildList(hwnd); return 0;
        }
        case WM_LBUTTONDOWN: { int x = LOWORD(lp); int y = HIWORD(lp); int tbH = g_bShowToolbar ? 28 : 0; if (y > tbH && x >= g_SplitX - 4 && x <= g_SplitX + 4) { SetCapture(hwnd); g_bDraggingSplitter = TRUE; } return 0; }
        case WM_MOUSEMOVE: { int x = LOWORD(lp); int y = HIWORD(lp); int tbH = g_bShowToolbar ? 28 : 0; if (y > tbH && x >= g_SplitX - 4 && x <= g_SplitX + 4) SetCursor(LoadCursor(NULL, IDC_SIZEWE)); if (g_bDraggingSplitter) { RECT rc; GetClientRect(hwnd, &rc); if (x > 50 && x < rc.right - 100) { g_SplitX = x; SendMessage(hwnd, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom)); } } return 0; }
        case WM_LBUTTONUP: if (g_bDraggingSplitter) { ReleaseCapture(); g_bDraggingSplitter = FALSE; SaveConfig(); } return 0;
        case WM_MEASUREITEM: { LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lp; if (lpmis->CtlID == ID_LIST) { if (g_ViewMode == 0) lpmis->itemHeight = CELL_H; else if (g_ViewMode == 1) lpmis->itemHeight = 20; else lpmis->itemHeight = 18; } else { lpmis->itemHeight = 18; } return TRUE; }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lp; HDC hdc = lpdis->hDC; RECT rc = lpdis->rcItem; BOOL bSel = (lpdis->itemState & ODS_SELECTED); HBRUSH hBgBr = CreateSolidBrush(bSel ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW)); HBRUSH hWinBg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            if (lpdis->itemID == (UINT)-1) { DeleteObject(hBgBr); DeleteObject(hWinBg); return TRUE; }
            if (lpdis->CtlID == ID_TREE) {
                TreeItemData FAR* item = (TreeItemData FAR*)lpdis->itemData; int startX = item->level * 16 + 5; int lyHalf = rc.top + (rc.bottom - rc.top) / 2; int l; HPEN hDot = CreatePen(PS_DOT, 1, RGB(128, 128, 128)); HPEN hOldP = SelectObject(hdc, hDot); SetBkMode(hdc, TRANSPARENT); FillRect(hdc, &rc, hWinBg);
                for (l = 1; l < item->level; l++) { if (!item->isLastChild[l]) { int lx = l * 16 - 3; MoveTo(hdc, lx, rc.top); LineTo(hdc, lx, rc.bottom); } }
                if (item->level > 0) { int lx = item->level * 16 - 3; MoveTo(hdc, lx, rc.top); if (item->isLastChild[item->level]) LineTo(hdc, lx, lyHalf); else LineTo(hdc, lx, rc.bottom); MoveTo(hdc, lx, lyHalf); LineTo(hdc, lx + 8, lyHalf); }
                SelectObject(hdc, hOldP); DeleteObject(hDot); if (item->hasChildren) DrawPlusMinus(hdc, startX - 12, rc.top + 4, item->expanded); DrawGDIFolder(hdc, startX, rc.top + 2, item->expanded, FALSE, FALSE); rc.left = startX + 20; FillRect(hdc, &rc, hBgBr); SetTextColor(hdc, bSel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT)); TextOut(hdc, rc.left + 2, rc.top + 2, item->displayName, lstrlen(item->displayName));
            } else if (lpdis->CtlID == ID_LIST) {
                int FAR* pType = (int FAR*)lpdis->itemData;
                if (pType && *pType == 2) {
                    RowItemData FAR* row = (RowItemData FAR*)lpdis->itemData; int c; FillRect(hdc, &rc, hWinBg); int colW = (g_ViewMode == 0) ? CELL_W : 150;
                    for (c = 0; c < row->count; c++) {
                        ListItemData FAR* item = row->items[c]; int itemX = rc.left + c * colW; BOOL bItemSel = (lstrcmp(item->path, g_SelectedListItemPath) == 0 && lstrcmp(item->name, g_SelectedListItemName) == 0);
                        if (g_ViewMode == 0) {
                            int centerX = itemX + (CELL_W - 32) / 2; if (item->isDir) DrawGDIFolder(hdc, centerX, rc.top + 5, FALSE, bItemSel, TRUE); else DrawGDIFile(hdc, centerX, rc.top + 5, bItemSel, TRUE);
                            RECT tRc; tRc.left = itemX + 2; tRc.right = itemX + CELL_W - 2; tRc.top = rc.top + 42; tRc.bottom = rc.bottom;
                            if (bItemSel) { FillRect(hdc, &tRc, hBgBr); SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT)); } else { FillRect(hdc, &tRc, hWinBg); SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT)); } SetBkMode(hdc, TRANSPARENT); DrawText(hdc, item->name, -1, &tRc, DT_CENTER | DT_WORDBREAK);
                        } else {
                            if (bItemSel) { RECT selRc; selRc.left = itemX; selRc.right = itemX + colW; selRc.top = rc.top; selRc.bottom = rc.bottom; FillRect(hdc, &selRc, hBgBr); }
                            if (item->isDir) DrawGDIFolder(hdc, itemX + 2, rc.top + 2, FALSE, FALSE, FALSE); else DrawGDIFile(hdc, itemX + 2, rc.top + 2, FALSE, FALSE);
                            RECT tRc; tRc.left = itemX + 22; tRc.right = itemX + colW - 4; tRc.top = rc.top + 2; tRc.bottom = rc.bottom; SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, bItemSel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT)); ExtTextOut(hdc, tRc.left, tRc.top, ETO_CLIPPED, &tRc, item->name, lstrlen(item->name), NULL);
                        }
                    }
                } else if (pType && *pType == 1) {
                    ListItemData FAR* item = (ListItemData FAR*)lpdis->itemData; char buf[64]; FillRect(hdc, &rc, hWinBg); SetBkMode(hdc, TRANSPARENT);
                    RECT rName = rc, rSize = rc, rType = rc, rDate = rc; rName.left += 22;
                    if (g_ViewMode == 3) { rName.right = rc.left + 150 - 4; rSize.left = rc.left + 150 + 4; rSize.right = rc.left + 220 - 4; rType.left = rc.left + 220 + 4; rType.right = rc.left + 280 - 4; rDate.left = rc.left + 280 + 4; }
                    if (bSel) FillRect(hdc, &rc, hBgBr);
                    if (item->isDir) DrawGDIFolder(hdc, rc.left + 2, rc.top + 2, FALSE, FALSE, FALSE); else DrawGDIFile(hdc, rc.left + 2, rc.top + 2, FALSE, FALSE);
                    SetTextColor(hdc, bSel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT));
                    if (g_ViewMode == 3) {
                        ExtTextOut(hdc, rName.left, rc.top + 2, ETO_CLIPPED, &rName, item->name, lstrlen(item->name), NULL);
                        if (!item->isDir && !item->isVirtual) { FormatSizeStr(item->size, buf); ExtTextOut(hdc, rSize.left, rc.top + 2, ETO_CLIPPED, &rSize, buf, lstrlen(buf), NULL); }
                        if (item->isDir) lstrcpy(buf, "File Folder"); else if (item->ext[0]) sprintf(buf, "%s File", item->ext); else lstrcpy(buf, "File"); ExtTextOut(hdc, rType.left, rc.top + 2, ETO_CLIPPED, &rType, buf, lstrlen(buf), NULL);
                        if (!item->isVirtual && item->date > 0) { FormatDateStr(item->date, item->time, buf); ExtTextOut(hdc, rDate.left, rc.top + 2, ETO_CLIPPED, &rDate, buf, lstrlen(buf), NULL); }
                    } else { TextOut(hdc, rName.left, rc.top + 2, item->name, lstrlen(item->name)); }
                }
            } DeleteObject(hBgBr); DeleteObject(hWinBg); return TRUE;
        }
        case WM_DELETEITEM: {
            LPDELETEITEMSTRUCT lpdis = (LPDELETEITEMSTRUCT)lp;
            if (lpdis->itemData && lpdis->itemData != (DWORD)LB_ERR) { int FAR* pType = (int FAR*)lpdis->itemData; if (*pType == 1) free((void*)lpdis->itemData); else if (*pType == 2) { RowItemData FAR* row = (RowItemData FAR*)lpdis->itemData; int i; for (i = 0; i < row->count; i++) free(row->items[i]); free(row); } }
            return TRUE;
        }
        case WM_COMMAND: {
            int id = wp; HWND hList = GetDlgItem(hwnd, ID_LIST); HWND hTree = GetDlgItem(hwnd, ID_TREE); HWND hFocus = GetFocus();
            if (id == 4041) MessageBox(hwnd, "Explorer for Win16\nWindows 95 Shell Clone", "About Calmira", MB_OK | MB_ICONINFORMATION);
            else if (id == 4022) ChangeViewMode(hwnd, 0); else if (id == 4023) ChangeViewMode(hwnd, 1); else if (id == 4024) ChangeViewMode(hwnd, 2); else if (id == 4025) ChangeViewMode(hwnd, 3);
            else if (id == 4020) { g_bShowToolbar = !g_bShowToolbar; ShowWindow(GetDlgItem(hwnd, ID_TOOLBAR), g_bShowToolbar ? SW_SHOW : SW_HIDE); CheckMenuItem(GetMenu(hwnd), 4020, MF_BYCOMMAND | (g_bShowToolbar ? MF_CHECKED : MF_UNCHECKED)); SendMessage(hwnd, WM_SIZE, 0, MAKELONG(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))); }
            else if (id == 4021) { g_bShowStatusBar = !g_bShowStatusBar; ShowWindow(GetDlgItem(hwnd, 300), g_bShowStatusBar ? SW_SHOW : SW_HIDE); CheckMenuItem(GetMenu(hwnd), 4021, MF_BYCOMMAND | (g_bShowStatusBar ? MF_CHECKED : MF_UNCHECKED)); SendMessage(hwnd, WM_SIZE, 0, MAKELONG(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))); }
            else if (id == 4028) { LoadIniShortcuts(); RebuildTree(hTree); RebuildList(hwnd); }
            else if (id == 4050) { 
                if (g_CurrentIsVirtual) { if (lstrcmp(g_CurrentPathOrId, "0") != 0) { int i; for(i=0; i<g_IniShortcutCount; i++) { if(lstrcmp(g_IniShortcuts[i].id, g_CurrentPathOrId)==0) { lstrcpy(g_CurrentPathOrId, g_IniShortcuts[i].parentId); break; } } } } else { char temp[MAX_PATH]; lstrcpy(temp, g_CurrentPathOrId); char* p = strrchr(temp, '\\'); if (p) { if (p == temp || *(p-1) == ':') *(p+1) = '\0'; else *p = '\0'; lstrcpy(g_CurrentPathOrId, temp); } else { lstrcpy(g_CurrentPathOrId, "0"); g_CurrentIsVirtual = TRUE; } }
                if (g_CurrentIsVirtual) ExpandAllParentsVirtual(g_CurrentPathOrId); else ExpandAllParentsFS(g_CurrentPathOrId); if (!IsExpanded(g_CurrentPathOrId)) ToggleExpand(g_CurrentPathOrId); RebuildTree(hTree); RebuildList(hwnd);
            }
            else if (id == ID_TREE && (HIWORD(lp) == LBN_SELCHANGE || HIWORD(lp) == LBN_DBLCLK)) {
                int sel = SendMessage(hTree, LB_GETCURSEL, 0, 0); if (sel != LB_ERR) { TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hTree, LB_GETITEMDATA, sel, 0); if (item) { lstrcpy(g_CurrentPathOrId, item->pathOrId); g_CurrentIsVirtual = item->isVirtual; SetWindowText(hwnd, item->displayName); RebuildList(hwnd); } }
            }
            else if (id >= 4001 && id <= 4016) {
                if (id == 4001 || id == 4002) { lstrcpy(g_ContextId, g_CurrentPathOrId); g_ContextIsVirtual = g_CurrentIsVirtual; } 
                else if (id <= 4005 || id == 4011 || id == 4012) {
                    if (hFocus == hTree) { lstrcpy(g_ContextId, g_CurrentPathOrId); g_ContextIsVirtual = g_CurrentIsVirtual; g_ContextIsFolder = TRUE; }
                    else {
                        if (g_ViewMode == 0 || g_ViewMode == 1) { if (g_SelectedListItemPath[0] != '\0') { lstrcpy(g_ContextId, g_SelectedListItemPath); g_ContextIsVirtual = g_SelectedListItemIsVirtual; g_ContextIsFolder = g_SelectedListItemIsDir; } else { MessageBox(hwnd, "Please select an item first.", "Information", MB_OK); return 0; } } 
                        else { int sel = SendMessage(hList, LB_GETCARETINDEX, 0, 0); if (sel != LB_ERR && SendMessage(hList, LB_GETSEL, sel, 0) > 0) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; lstrcpy(g_ContextId, item->path); g_ContextIsVirtual = item->isVirtual; g_ContextIsFolder = item->isDir; } } else { MessageBox(hwnd, "Please select an item first.", "Information", MB_OK); return 0; } }
                    }
                }
                
                if (!g_ContextIsVirtual && (id == 4002 || id == 4005)) { MessageBox(hwnd, "Only INI virtual shortcuts can be edited.", "Information", MB_OK | MB_ICONINFORMATION); return 0; }
                
                if (id == 4001) { lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0'; g_PromptMode = PROMPT_NEWFOLDER; CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 170); }
                else if (id == 4002) { g_EditShortcutId[0] = '\0'; CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 360, 300); }
                else if (id == 4003 && g_ContextId[0] != '\0') {
                    char msgBuf[256]; sprintf(msgBuf, "Are you sure you want to delete '%s'?", g_ContextId);
                    if (MessageBox(hwnd, msgBuf, "Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        if (g_ContextIsVirtual) { WritePrivateProfileString("Shortcut", g_ContextId, NULL, g_szExplorerIni); LoadIniShortcuts(); RebuildTree(hTree); RebuildList(hwnd); }
                        else { lstrcpy(g_DelPath, g_ContextId); g_DelIsDir = g_ContextIsFolder; g_bCancelDel = FALSE; CreateCenteredDialog(g_hInst, hwnd, "DeleteProgressDlgClass", "Deleting", 280, 130); }
                    }
                }
                else if (id == 4004) {
                    if (hFocus == hTree) { MessageBox(hwnd, "Rename unsupported in tree view directly.", "Info", MB_OK); }
                    else {
                        int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
                        if (g_ViewMode == 0 || g_ViewMode == 1) {
                            if (g_SelectedListItemPath[0] != '\0') { g_InlineRenameIsVirtual = g_SelectedListItemIsVirtual; if (g_SelectedListItemIsVirtual) lstrcpy(g_InlineRenameId, g_SelectedListItemPath); else lstrcpy(g_InlineRenameOldPath, g_SelectedListItemPath); if (sel != LB_ERR) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 2) { RowItemData FAR* row = (RowItemData FAR*)pType; int c; int colW = (g_ViewMode == 0) ? CELL_W : 150; for (c=0; c<row->count; c++) { if (lstrcmp(row->items[c]->path, g_SelectedListItemPath) == 0 && lstrcmp(row->items[c]->name, g_SelectedListItemName) == 0) { RECT rc; SendMessage(hList, LB_GETITEMRECT, sel, (LPARAM)(LPRECT)&rc); int itemX = rc.left + c * colW; RECT eRc; if (g_ViewMode == 0) { eRc.left = itemX + 2; eRc.right = itemX + CELL_W - 2; eRc.top = rc.top + 42; eRc.bottom = rc.bottom; } else { eRc.left = itemX + 22; eRc.right = itemX + 150 - 4; eRc.top = rc.top + 2; eRc.bottom = rc.bottom; } HWND hEdit = CreateWindowEx(0, "EDIT", row->items[c]->name, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | (g_ViewMode == 0 ? ES_CENTER : 0), eRc.left, eRc.top, eRc.right - eRc.left, eRc.bottom - eRc.top, hList, (HMENU)9999, g_hInst, NULL); g_lpfnOldInlineEditProc = (FARPROC)SetWindowLong(hEdit, GWL_WNDPROC, (LONG)MakeProcInstance((FARPROC)InlineEditProc, g_hInst)); SetFocus(hEdit); SendMessage(hEdit, EM_SETSEL, 0, MAKELONG(0, -1)); break; } } } } }
                        } else {
                            if (sel != LB_ERR) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; g_InlineRenameIsVirtual = item->isVirtual; if (item->isVirtual) lstrcpy(g_InlineRenameId, item->path); else lstrcpy(g_InlineRenameOldPath, item->path); RECT rc; SendMessage(hList, LB_GETITEMRECT, sel, (LPARAM)(LPRECT)&rc); rc.left += 22; if (g_ViewMode == 3) rc.right = rc.left + 150 - 4; HWND hEdit = CreateWindowEx(0, "EDIT", item->name, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hList, (HMENU)9999, g_hInst, NULL); g_lpfnOldInlineEditProc = (FARPROC)SetWindowLong(hEdit, GWL_WNDPROC, (LONG)MakeProcInstance((FARPROC)InlineEditProc, g_hInst)); SetFocus(hEdit); SendMessage(hEdit, EM_SETSEL, 0, MAKELONG(0, -1)); } }
                        }
                    }
                }
                else if (id == 4005 && g_ContextId[0] != '\0') { lstrcpy(g_EditShortcutId, g_ContextId); CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Properties", 360, 300); }
                else if (id == 4011 || id == 4012) {
                    lstrcpy(g_ClipPath, g_ContextId); lstrcpy(g_ClipName, (g_ViewMode == 0 || g_ViewMode == 1) && hFocus != hTree ? g_SelectedListItemName : "");
                    if (g_ClipName[0] == '\0') { int k; for(k=0; k<g_IniShortcutCount; k++) if(lstrcmp(g_IniShortcuts[k].id, g_ContextId) == 0) { lstrcpy(g_ClipName, g_IniShortcuts[k].name); break; } }
                    g_ClipIsVirtual = g_ContextIsVirtual; g_ClipIsDir = g_ContextIsFolder; g_ClipOp = (id == 4011) ? 2 : 1; if (g_ClipIsVirtual) lstrcpy(g_ClipId, g_ContextId);
                }
                else if (id == 4013) {
                    if (g_ClipOp == 0) return 0;
                    if (g_ClipIsVirtual || g_CurrentIsVirtual) {
                        char newId[16]; IniShortcut sh; memset(&sh, 0, sizeof(sh)); GetNewIniId(newId); lstrcpy(sh.id, newId);
                        lstrcpy(sh.name, g_ClipName); lstrcpy(sh.parentId, g_CurrentPathOrId); sh.isFolder = g_ClipIsDir;
                        if (g_ClipIsVirtual) { int i; for(i=0; i<g_IniShortcutCount; i++) if (lstrcmp(g_IniShortcuts[i].id, g_ClipId) == 0) { lstrcpy(sh.exe, g_IniShortcuts[i].exe); lstrcpy(sh.params, g_IniShortcuts[i].params); lstrcpy(sh.icon, g_IniShortcuts[i].icon); sh.minimized = g_IniShortcuts[i].minimized; break; } if (g_ClipOp == 2) { WritePrivateProfileString("Shortcut", g_ClipId, NULL, g_szExplorerIni); g_ClipOp = 0; } } else lstrcpy(sh.exe, g_ClipPath);
                        if (!NameExists(g_CurrentPathOrId, sh.name, TRUE)) SaveIniEntry(&sh); LoadIniShortcuts(); RebuildTree(hTree); RebuildList(hwnd);
                    } else {
                        lstrcpy(g_CurrentJob.src, g_ClipPath); lstrcpy(g_CurrentJob.dst, g_CurrentPathOrId); if (g_CurrentJob.dst[0] != '\0' && g_CurrentJob.dst[lstrlen(g_CurrentJob.dst)-1] != '\\') lstrcat(g_CurrentJob.dst, "\\"); lstrcat(g_CurrentJob.dst, g_ClipName); g_CurrentJob.isMove = (g_ClipOp == 2); g_CurrentJob.isDir = g_ClipIsDir; g_ReplaceMode = 0; if (g_ClipOp == 2) g_ClipOp = 0; CreateCenteredDialog(g_hInst, hwnd, "CopyProgressDlgClass", g_CurrentJob.isMove ? "Moving" : "Copying", 280, 130);
                    }
                }
                else if (id == 4014) {
                    if (g_ClipOp == 0) return 0; char newId[16]; IniShortcut sh; memset(&sh, 0, sizeof(sh)); GetNewIniId(newId); lstrcpy(sh.id, newId);
                    sprintf(sh.name, "Shortcut to %s", g_ClipName); lstrcpy(sh.parentId, g_CurrentPathOrId); sh.isFolder = FALSE;
                    if (g_ClipIsVirtual) { int i; for(i=0; i<g_IniShortcutCount; i++) if (lstrcmp(g_IniShortcuts[i].id, g_ClipId) == 0) { lstrcpy(sh.exe, g_IniShortcuts[i].exe); lstrcpy(sh.params, g_IniShortcuts[i].params); lstrcpy(sh.icon, g_IniShortcuts[i].icon); sh.minimized = g_IniShortcuts[i].minimized; break; } } else lstrcpy(sh.exe, g_ClipPath); if (!NameExists(g_CurrentPathOrId, sh.name, TRUE)) SaveIniEntry(&sh); LoadIniShortcuts(); RebuildTree(hTree); RebuildList(hwnd);
                }
                else if (id == 4015) { if (g_ViewMode != 0 && g_ViewMode != 1) SendMessage(hList, LB_SETSEL, TRUE, -1); }
                else if (id == 4016) { if (g_ViewMode != 0 && g_ViewMode != 1) { int i, count = SendMessage(hList, LB_GETCOUNT, 0, 0); for (i=0; i<count; i++) { BOOL sel = SendMessage(hList, LB_GETSEL, i, 0); SendMessage(hList, LB_SETSEL, !sel, i); } } }
            }
            else if ((id == ID_LIST && HIWORD(lp) == LBN_DBLCLK) || id == 5002 || id == 5001) {
                if (g_ViewMode != 0 && g_ViewMode != 1) {
                    int sel = SendMessage(hList, LB_GETCARETINDEX, 0, 0);
                    if (sel != LB_ERR) {
                        int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0);
                        if (pType && *pType == 1) {
                            ListItemData FAR* item = (ListItemData FAR*)pType;
                            if (item->isDir) { lstrcpy(g_CurrentPathOrId, item->path); g_CurrentIsVirtual = item->isVirtual; SetWindowText(hwnd, item->name); if (g_CurrentIsVirtual) ExpandAllParentsVirtual(g_CurrentPathOrId); else ExpandAllParentsFS(g_CurrentPathOrId); if (!IsExpanded(item->path)) ToggleExpand(item->path); RebuildTree(hTree); RebuildList(hwnd); } else { if (item->isVirtual) { int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmpi(g_IniShortcuts[i].id, item->path) == 0) { if (g_IniShortcuts[i].exe[0]) ShellExecute(hwnd, "open", g_IniShortcuts[i].exe, g_IniShortcuts[i].params, NULL, SW_SHOWNORMAL); break; } } } else ShellExecute(hwnd, "open", item->path, NULL, NULL, SW_SHOWNORMAL); }
                        }
                    }
                }
            } return 0;
        }
        case WM_DESTROY: if (!FindWindow("Win95DesktopClass", "Desktop")) PostQuitMessage(0); return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

/* --- Entry Point --- */
int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc; MSG msg; BOOL isWindowed = FALSE; char startPath[MAX_PATH] = ""; char* p = lpCmdLine; g_hInst = hInst;
    while (*p) { while (*p == ' ') p++; if (!*p) break; if (p[0] == '-' && (p[1] == 'w' || p[1] == 'W')) { isWindowed = TRUE; p += 2; } else { int i = 0; if (*p == '"') { p++; while (*p && *p != '"' && i < MAX_PATH - 1) startPath[i++] = *p++; if (*p == '"') p++; } else { while (*p && *p != ' ' && i < MAX_PATH - 1) startPath[i++] = *p++; } startPath[i] = '\0'; } }

    memset(&wc, 0, sizeof(WNDCLASS)); wc.style = CS_DBLCLKS; wc.lpfnWndProc = DesktopProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(RGB(0, 128, 128)); wc.lpszClassName = "Win95DesktopClass"; RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.style = CS_DBLCLKS; wc.lpfnWndProc = FolderWndProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "Win95FolderClass"; RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = PromptDlgProc; wc.hInstance = hInst; wc.lpszClassName = "PromptDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = ShortcutDlgProc; wc.hInstance = hInst; wc.lpszClassName = "ShortcutDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = CopyProgressDlgProc; wc.hInstance = hInst; wc.lpszClassName = "CopyProgressDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = DeleteProgressDlgProc; wc.hInstance = hInst; wc.lpszClassName = "DeleteProgressDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = ReplaceDlgProc; wc.hInstance = hInst; wc.lpszClassName = "ReplaceDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);

    if (startPath[0] == '\0' || isWindowed) { if (isWindowed) CreateWindowEx(0, "Win95DesktopClass", "Desktop", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, hInst, NULL); else CreateWindowEx(0, "Win95DesktopClass", "Desktop", WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, hInst, NULL); }
    if (startPath[0] != '\0') { g_hwndMain = CreateWindowEx(0, "Win95FolderClass", startPath, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, hInst, (LPVOID)startPath); } else if (isWindowed) { g_hwndMain = FindWindow("Win95DesktopClass", "Desktop"); }
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    if (g_lpfnTreeProc) FreeProcInstance(g_lpfnTreeProc); if (g_lpfnListProc) FreeProcInstance(g_lpfnListProc); if (g_lpfnToolbarProc) FreeProcInstance(g_lpfnToolbarProc);
    return (int)msg.wParam;
}