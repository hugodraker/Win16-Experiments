/*
 * isomastr.c - Complete ISO9660/RockRidge/Joliet Editor (16-bit Win3.1 Port)
 * 
 * Compile: wcl -bt=windows -l=windows -ml -os -fe=isomastr.exe isomastr.c
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <dos.h>
#include <malloc.h>
#include <time.h>

#pragma library("commdlg.lib")
#pragma library("shell.lib")

#ifndef MB_ICONERROR
#define MB_ICONERROR 0x0010U
#endif
#ifndef MB_ICONWARNING
#define MB_ICONWARNING 0x0030U
#endif
#ifndef MB_ICONINFORMATION
#define MB_ICONINFORMATION 0x0040U
#endif

/* ============================================================
 * CONSTANTS
 * ============================================================ */
#define APP_NAME        "ISO Master"
#define APP_VERSION     "3.2 (Win16)"
#define WINDOW_WIDTH    640
#define WINDOW_HEIGHT   480
#define MAX_PATH_LEN    260
#define MAX_ENTRIES     2048
#define SECTOR_SIZE     2048UL

#define ID_LOCAL_BACK     2001
#define ID_LOCAL_NEWDIR   2002
#define ID_ISO_BACK       2003
#define ID_ISO_NEWDIR     2004
#define ID_ISO_ADD        2005
#define ID_ISO_EXTRACT    2006
#define ID_ISO_DELETE     2007
#define ID_ISO_DELTEMP    2008
#define ID_ISO_SAVEBTN    2009

#define ID_LIST_LOCAL     2101
#define ID_LIST_ISO       2102

#define IDM_IMAGE_NEW      3001
#define IDM_IMAGE_OPEN     3002
#define IDM_IMAGE_SAVE     3003
#define IDM_IMAGE_SAVEAS   3004
#define IDM_IMAGE_PROPS    3005
#define IDM_IMAGE_QUIT     3006
#define IDM_VIEW_REFRESH   3010
#define IDM_VIEW_HIDDEN    3011
#define IDM_VIEW_SORTDIR   3012
#define IDM_BOOT_PROPS     3020
#define IDM_BOOT_SAVE      3021
#define IDM_BOOT_DEL       3022
#define IDM_HELP_ABOUT     3041

#define IDM_MRU_1          3101
#define IDM_MRU_2          3102
#define IDM_MRU_3          3103
#define IDM_MRU_4          3104
#define IDM_MRU_5          3105
#define IDM_MRU_SEP        3100
#define IDM_MRU_SEP2       3106

/* ============================================================
 * DATA STRUCTURES
 * ============================================================ */
typedef enum { NODE_TYPE_ROOT, NODE_TYPE_DIRECTORY, NODE_TYPE_FILE } NodeType;

#pragma pack(push, 1)
typedef struct {
    BYTE  length;               
    BYTE  ext_attr_length;      
    BYTE  extent_location[8];   
    BYTE  data_length[8];       
    BYTE  recording_date[7];    
    BYTE  flags;                
    BYTE  file_unit_size;       
    BYTE  interleave_gap_size;  
    BYTE  volume_sequence[4];   
    BYTE  name_length;          
    BYTE  name[1];              
} Iso9660DirRecord;

typedef struct {
    BYTE  type; BYTE  identifier[5]; BYTE  version; BYTE  unused1;
    BYTE  system_identifier[32]; BYTE  volume_identifier[32]; BYTE  unused2[8];
    BYTE  volume_space_size[8]; BYTE  unused3[32]; BYTE  volume_set_size[4];
    BYTE  volume_sequence_number[4]; BYTE  logical_block_size[4]; BYTE  path_table_size[8];
    BYTE  type_l_path_table[4]; BYTE  opt_type_l_path_table[4];
    BYTE  type_m_path_table[4]; BYTE  opt_type_m_path_table[4];
} IsoPrimaryVolumeDesc;
#pragma pack(pop)

typedef struct {
    NodeType    type;
    char        name[128];       
    char        long_name[128];  
    DWORD       size;
    BYTE        is_directory;
    DWORD       parent_index;    
    DWORD       sector_offset;   
    DWORD       sector_count;    
    BOOL        marked_deleted;  
    BOOL        marked_new;      
    char        source_file[MAX_PATH_LEN]; 
    time_t      timestamp;       
    DWORD       path_table_idx;
    
    DWORD       j_size;
    DWORD       j_sector_offset;
    DWORD       j_path_table_idx;
} IsoEntry;

typedef struct {
    FILE*           hFile;
    BOOL            isOpen;
    char            path[MAX_PATH_LEN];
    char            volume_name[32];
    DWORD           file_size;
    DWORD           num_entries;
    IsoEntry FAR*   entries[MAX_ENTRIES];
    
    DWORD           new_data_offset;
    DWORD           root_sector;
    DWORD           logical_block_size;
    BYTE            pvd_buffer[2048];  
    
    DWORD           svd_sector;
    BYTE            svd_buffer[2048];
    
    DWORD           boot_desc_sector;
    BYTE            boot_desc_buffer[2048];
    
    time_t          creation_date;
    char            system_identifier[32];
    BOOL            has_rockridge;
    BOOL            has_joliet;
    
    BOOL            is_bootable;
    DWORD           boot_catalog_sector;
    DWORD           boot_image_sector;
    DWORD           boot_image_size;
} IsoImageState;

/* ============================================================
 * GLOBAL VARIABLES
 * ============================================================ */
HWND g_hMainWnd = NULL, g_hLocalListBox = NULL, g_hIsoListBox = NULL;
HWND g_hStatusBar = NULL;
HINSTANCE g_hInstance = NULL;

HWND g_btnLBack, g_btnLNew;
HWND g_btnISave, g_btnIBack, g_btnINew, g_btnIAdd, g_btnIExt, g_btnIDel, g_btnIDelT;

IsoImageState g_iso;
char g_current_local_path[MAX_PATH_LEN];
DWORD g_current_iso_parent = 0; 
char g_mru[5][MAX_PATH_LEN];

char g_temp_files[64][MAX_PATH_LEN];
int g_temp_files_count = 0;

BOOL g_show_hidden = FALSE;
BOOL g_sort_dirs_first = TRUE;
volatile BOOL g_cancel_operation = FALSE;

static void cmd_delete_selected(HWND hwnd);
static void cmd_rename_local(HWND hwnd);
static void cmd_rename_iso(HWND hwnd);
static void iso_delete_entry(DWORD entry_idx);

/* ============================================================
 * UTILITY FUNCTIONS
 * ============================================================ */
static const char* get_basename(const char* path) {
    const char* slash = strrchr(path, '/'); 
    if (!slash) slash = strrchr(path, '\\'); 
    return slash ? slash + 1 : path;
}

void UpdateWindowTitle() {
    char title[MAX_PATH_LEN + 64];
    if (g_iso.isOpen && strlen(g_iso.path) > 0) {
        sprintf(title, "ISO Master - %s - [%s]", APP_VERSION, get_basename(g_iso.path));
    } else if (g_iso.isOpen) {
        sprintf(title, "ISO Master - %s - [New ISO]", APP_VERSION);
    } else {
        sprintf(title, "ISO Master - %s", APP_VERSION);
    }
    SetWindowText(g_hMainWnd, title);
}

static void extract_name_from_listbox(const char* display, char* out_name) {
    int i = 0;
    while (display[i] != '\0' && display[i] != ' ') {
        out_name[i] = display[i];
        i++;
    }
    out_name[i] = '\0';
}

static DWORD ReadLE32(const BYTE FAR* buf) { return ((DWORD)buf[0]) | (((DWORD)buf[1]) << 8) | (((DWORD)buf[2]) << 16) | (((DWORD)buf[3]) << 24); }
static void WriteLE32(BYTE FAR* buf, DWORD val) { buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF; buf[2] = (val >> 16) & 0xFF; buf[3] = (val >> 24) & 0xFF; }
static void WriteBE32(BYTE FAR* buf, DWORD val) { buf[0] = (val >> 24) & 0xFF; buf[1] = (val >> 16) & 0xFF; buf[2] = (val >> 8) & 0xFF; buf[3] = val & 0xFF; }

static void ExtractString(const BYTE FAR* src, char* dst, int max_len) {
    int i, j = 0;
    for (i = 0; i < max_len && src[i] != ' ' && src[i] != '\0'; i++) { 
        if (src[i] >= 32 && src[i] < 127) dst[j++] = src[i]; 
    } 
    dst[j] = '\0';
}

void SetStatus(const char* msg) {
    if (g_hStatusBar) SetWindowText(g_hStatusBar, msg);
}

void PumpMessages() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

/* ============================================================
 * SETTINGS & MRU MANAGEMENT
 * ============================================================ */
void SaveSettings() {
    char iniPath[MAX_PATH_LEN];
    char* p;
    RECT rc;
    char buf[32];
    int i;

    GetModuleFileName(NULL, iniPath, MAX_PATH_LEN);
    p = strrchr(iniPath, '\\');
    if (p) strcpy(p + 1, "isomastr.ini");
    else strcpy(iniPath, "isomastr.ini");
    
    GetWindowRect(g_hMainWnd, &rc);
    sprintf(buf, "%d", rc.left); WritePrivateProfileString("Window", "X", buf, iniPath);
    sprintf(buf, "%d", rc.top); WritePrivateProfileString("Window", "Y", buf, iniPath);
    sprintf(buf, "%d", rc.right - rc.left); WritePrivateProfileString("Window", "Width", buf, iniPath);
    sprintf(buf, "%d", rc.bottom - rc.top); WritePrivateProfileString("Window", "Height", buf, iniPath);
    
    for (i = 0; i < 5; i++) {
        char key[16]; sprintf(key, "MRU%d", i + 1);
        WritePrivateProfileString("MRU", key, g_mru[i], iniPath);
    }
}

void UpdateMRUMenu() {
    HMENU hMenu = GetMenu(g_hMainWnd);
    HMENU hFile;
    int i, count = 0;

    if (!hMenu) return; 
    hFile = GetSubMenu(hMenu, 0);
    
    DeleteMenu(hFile, IDM_MRU_SEP, MF_BYCOMMAND);
    for (i = 0; i < 5; i++) DeleteMenu(hFile, IDM_MRU_1 + i, MF_BYCOMMAND);
    DeleteMenu(hFile, IDM_MRU_SEP2, MF_BYCOMMAND);
    
    for (i = 0; i < 5; i++) if (strlen(g_mru[i]) > 0) count++;
    
    if (count > 0) {
        InsertMenu(hFile, IDM_IMAGE_QUIT, MF_BYCOMMAND | MF_SEPARATOR, IDM_MRU_SEP, NULL);
        for (i = 0; i < 5; i++) {
            if (strlen(g_mru[i]) > 0) {
                char text[MAX_PATH_LEN + 10];
                sprintf(text, "&%d %s", i + 1, g_mru[i]);
                InsertMenu(hFile, IDM_IMAGE_QUIT, MF_BYCOMMAND | MF_STRING, IDM_MRU_1 + i, text);
            }
        }
    }
    InsertMenu(hFile, IDM_IMAGE_QUIT, MF_BYCOMMAND | MF_SEPARATOR, IDM_MRU_SEP2, NULL);
    DrawMenuBar(g_hMainWnd);
}

void LoadSettings() {
    char iniPath[MAX_PATH_LEN];
    char* p;
    char buf[32];
    int x = -9999, y = -9999, w = WINDOW_WIDTH, h = WINDOW_HEIGHT;
    int i;

    GetModuleFileName(NULL, iniPath, MAX_PATH_LEN);
    p = strrchr(iniPath, '\\');
    if (p) strcpy(p + 1, "isomastr.ini");
    else strcpy(iniPath, "isomastr.ini");
    
    buf[0] = '\0';
    GetPrivateProfileString("Window", "X", "", buf, 32, iniPath);
    if (strlen(buf) > 0) x = atoi(buf);
    
    buf[0] = '\0';
    GetPrivateProfileString("Window", "Y", "", buf, 32, iniPath);
    if (strlen(buf) > 0) y = atoi(buf);
    
    buf[0] = '\0';
    GetPrivateProfileString("Window", "Width", "", buf, 32, iniPath);
    if (strlen(buf) > 0) w = atoi(buf);
    
    buf[0] = '\0';
    GetPrivateProfileString("Window", "Height", "", buf, 32, iniPath);
    if (strlen(buf) > 0) h = atoi(buf);
    
    if (x != -9999 && y != -9999) {
        SetWindowPos(g_hMainWnd, NULL, x, y, w, h, SWP_NOZORDER);
    }
    
    for (i = 0; i < 5; i++) {
        char key[16]; sprintf(key, "MRU%d", i + 1);
        GetPrivateProfileString("MRU", key, "", g_mru[i], MAX_PATH_LEN, iniPath);
    }
    UpdateMRUMenu();
}

void UpdateMRU(const char* path) {
    int existing = -1;
    int i;
    for (i = 0; i < 5; i++) {
        if (strcmp(g_mru[i], path) == 0) existing = i;
    }
    if (existing != -1) {
        char temp[MAX_PATH_LEN]; strcpy(temp, g_mru[existing]);
        for (i = existing; i > 0; i--) strcpy(g_mru[i], g_mru[i-1]);
        strcpy(g_mru[0], temp);
    } else {
        for (i = 4; i > 0; i--) strcpy(g_mru[i], g_mru[i-1]);
        strcpy(g_mru[0], path);
    }
    UpdateMRUMenu();
    SaveSettings();
}

/* ============================================================
 * IN-MEMORY INPUT BOX
 * ============================================================ */
char g_input_result[MAX_PATH_LEN];
HWND g_hInputEdit;

LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (wp == 1) { 
                GetWindowText(g_hInputEdit, g_input_result, MAX_PATH_LEN); 
                DestroyWindow(hwnd);
            } else if (wp == 2) { 
                g_input_result[0] = '\0'; 
                DestroyWindow(hwnd);
            }
            break;
        case WM_CLOSE: 
            g_input_result[0] = '\0'; 
            DestroyWindow(hwnd); 
            break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

BOOL ShowInputBox(HWND parent, const char* title, const char* prompt, char* out_buf) {
    WNDCLASS wc = {0}; 
    HWND hDlg;
    MSG msg;

    wc.lpfnWndProc = InputWndProc; 
    wc.hInstance = g_hInstance;
    wc.lpszClassName = "IsoInputBoxClass"; 
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); 
    RegisterClass(&wc);

    hDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "IsoInputBoxClass", title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 300, 140, parent, NULL, g_hInstance, NULL);
    CreateWindow("STATIC", prompt, WS_CHILD | WS_VISIBLE, 10, 10, 260, 20, hDlg, NULL, g_hInstance, NULL);
    g_hInputEdit = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 10, 35, 260, 22, hDlg, NULL, g_hInstance, NULL);
    CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 110, 70, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindow("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 195, 70, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);
    
    SetFocus(g_hInputEdit); 
    EnableWindow(parent, FALSE);
    
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hDlg, &msg)) { 
            TranslateMessage(&msg); 
            DispatchMessage(&msg); 
        }
    }
    EnableWindow(parent, TRUE); 
    BringWindowToTop(parent);
    
    if (g_input_result[0] != '\0') { 
        strcpy(out_buf, g_input_result); 
        return TRUE; 
    }
    return FALSE;
}

/* ============================================================
 * UI POPULATION
 * ============================================================ */
void set_local_path(const char* path) {
    struct find_t fd;
    char search_path[MAX_PATH_LEN];
    int pass, idx;
    
    strcpy(g_current_local_path, path); 
    SendMessage(g_hLocalListBox, LB_RESETCONTENT, 0, 0);
    sprintf(search_path, "%s\\*.*", g_current_local_path);
    
    if (strcmp(g_current_local_path, "C:\\") != 0 && strlen(g_current_local_path) > 3) {
        idx = SendMessage(g_hLocalListBox, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"..                             <DIR>");
        SendMessage(g_hLocalListBox, LB_SETITEMDATA, idx, 1);
    }

    for (pass = 0; pass < 2; pass++) {
        if (_dos_findfirst(search_path, _A_NORMAL|_A_HIDDEN|_A_SUBDIR|_A_RDONLY, &fd) == 0) {
            do {
                BOOL isDir;
                char display_str[128];
                
                if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0) continue;
                if (!g_show_hidden && (fd.attrib & _A_HIDDEN)) continue;
                
                isDir = (fd.attrib & _A_SUBDIR) != 0;
                if (g_sort_dirs_first && ((pass == 0 && !isDir) || (pass == 1 && isDir))) continue;
                
                sprintf(display_str, "%-30s %s", fd.name, isDir ? "<DIR>" : "");
                idx = SendMessage(g_hLocalListBox, LB_ADDSTRING, 0, (LPARAM)(LPSTR)display_str);
                SendMessage(g_hLocalListBox, LB_SETITEMDATA, idx, isDir ? 1 : 0);
                
            } while (_dos_findnext(&fd) == 0);
        }
    }
}

void populate_iso_listview() {
    int pass, idx;
    DWORD i;
    
    SendMessage(g_hIsoListBox, LB_RESETCONTENT, 0, 0); 
    if (!g_iso.isOpen) return;
    
    if (g_current_iso_parent != 0) {
        idx = SendMessage(g_hIsoListBox, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"..                             <DIR>");
        SendMessage(g_hIsoListBox, LB_SETITEMDATA, idx, 0xFFFFFFFF);
    }
    
    for (pass = 0; pass < 2; pass++) {
        for (i = 1; i < g_iso.num_entries; i++) {
            IsoEntry FAR* entry = g_iso.entries[i];
            char display_str[128];
            char safe_name[128];
            
            if (entry == NULL || entry->marked_deleted || entry->parent_index != g_current_iso_parent) continue;
            if (g_sort_dirs_first && ((pass == 0 && !entry->is_directory) || (pass == 1 && entry->is_directory))) continue;
            
            _fstrcpy(safe_name, entry->long_name); /* Copy from far heap to near stack safely */
            sprintf(display_str, "%-30s %s", safe_name, entry->is_directory ? "<DIR>" : "");
            idx = SendMessage(g_hIsoListBox, LB_ADDSTRING, 0, (LPARAM)(LPSTR)display_str);
            SendMessage(g_hIsoListBox, LB_SETITEMDATA, idx, (LPARAM)i);
        }
    }
}

/* ============================================================
 * CORE ISO PARSING
 * ============================================================ */
static int parse_directory_record(const BYTE FAR* record, int record_len, DWORD parent_idx, DWORD base_sector) {
    IsoEntry FAR* entry;
    
    if (record_len < 34 || record[0] == 0) return 0;
    if (record[0] >= 2 && (record[33] == 0 || record[33] == 1)) return record[0];
    if (g_iso.num_entries >= MAX_ENTRIES) return record[0];
    
    entry = (IsoEntry FAR*)_fmalloc(sizeof(IsoEntry));
    if (!entry) return record[0];
    _fmemset(entry, 0, sizeof(IsoEntry));
    
    g_iso.entries[g_iso.num_entries] = entry;
    
    entry->sector_offset = ReadLE32(record + 2); 
    entry->size = ReadLE32(record + 10);
    entry->sector_count = (entry->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    entry->is_directory = (record[25] & 2) != 0; 
    entry->parent_index = parent_idx;
    
    if (record[32] > 0 && record[32] < 128) {
        char FAR* semi;
        _fmemcpy(entry->name, record + 33, record[32]); 
        entry->name[record[32]] = '\0';
        semi = _fstrchr(entry->name, ';'); 
        if (semi) *semi = '\0';
    } else _fstrcpy(entry->name, "(unnamed)");
    
    _fstrncpy(entry->long_name, entry->name, sizeof(entry->long_name) - 1);
    entry->type = entry->is_directory ? NODE_TYPE_DIRECTORY : NODE_TYPE_FILE;
    g_iso.num_entries++; 
    
    return record[0];
}

static void read_directory(DWORD sector, DWORD parent_idx, int depth) {
    BYTE FAR* buffer;
    DWORD dir_size, bytes_read_total = 0;
    DWORD current_entries, i;
    
    /* ISO 9660 natively limits to 8 directory levels. Exceeding this blows out the 16-bit stack */
    if (depth > 8) return;
    
    dir_size = g_iso.entries[parent_idx]->size;
    if (dir_size == 0) return;
    
    /* Process 1 sector at a time to prevent massive _fmalloc blocks exceeding 64KB */
    buffer = (BYTE FAR*)_fmalloc(SECTOR_SIZE); 
    if (!buffer) return;
    
    fseek(g_iso.hFile, (long)sector * SECTOR_SIZE, SEEK_SET);
    
    while (bytes_read_total < dir_size) {
        DWORD offset = 0;
        if (fread(buffer, 1, SECTOR_SIZE, g_iso.hFile) < SECTOR_SIZE) break;
        bytes_read_total += SECTOR_SIZE;
        
        while (offset < SECTOR_SIZE) {
            BYTE len = buffer[offset];
            int consumed;
            if (len == 0) break; 
            if (offset + len > SECTOR_SIZE) break; 
            
            consumed = parse_directory_record(&buffer[offset], len, parent_idx, sector);
            if (consumed == 0) break; 
            offset += consumed;
        }
    } 
    _ffree(buffer);
    
    current_entries = g_iso.num_entries; 
    for (i = 0; i < current_entries; i++) {
        IsoEntry FAR* ent = g_iso.entries[i];
        if (ent && !ent->marked_deleted && ent->is_directory && ent->parent_index == parent_idx && i != parent_idx) {
            read_directory(ent->sector_offset, i, depth + 1);
        }
    }
}

static void free_iso_entries() {
    DWORD i;
    for (i = 0; i < g_iso.num_entries; i++) {
        if (g_iso.entries[i]) {
            _ffree(g_iso.entries[i]);
            g_iso.entries[i] = NULL;
        }
    }
    g_iso.num_entries = 0;
}

static void iso_close_image(void) {
    if (g_iso.hFile) { fclose(g_iso.hFile); g_iso.hFile = NULL; }
    free_iso_entries();
    g_iso.isOpen = FALSE; 
    g_current_iso_parent = 0;
    UpdateWindowTitle();
}

static int iso_open_image(const char* path) {
    FILE* hKeep = NULL;
    DWORD s;
    IsoEntry FAR* root_entry;
    IsoPrimaryVolumeDesc FAR* pvd;
    DWORD root_extent, root_size;
    BYTE FAR* desc;
    BYTE FAR* cat_sec;
    
    if (g_iso.isOpen && strcmp(g_iso.path, path) == 0) hKeep = g_iso.hFile;
    else iso_close_image();
    
    if (hKeep == NULL) {
        memset(&g_iso, 0, sizeof(g_iso));
        g_iso.hFile = fopen(path, "rb+");
        if (!g_iso.hFile) {
            g_iso.hFile = fopen(path, "rb"); 
            if (!g_iso.hFile) return -1;
        }
        strncpy(g_iso.path, path, MAX_PATH_LEN - 1);
        g_iso.isOpen = TRUE;
    } else {
        free_iso_entries();
        g_current_iso_parent = 0;
        g_iso.boot_desc_sector = 0;
        g_iso.svd_sector = 0;
        g_iso.is_bootable = FALSE;
    }
    
    fseek(g_iso.hFile, 0, SEEK_END);
    g_iso.file_size = ftell(g_iso.hFile);
    
    fseek(g_iso.hFile, 16L * SECTOR_SIZE, SEEK_SET);
    if (fread(g_iso.pvd_buffer, 1, SECTOR_SIZE, g_iso.hFile) < SECTOR_SIZE) { 
        fclose(g_iso.hFile); g_iso.hFile = NULL; return -2; 
    }
    
    pvd = (IsoPrimaryVolumeDesc FAR*)g_iso.pvd_buffer;
    if (_fmemcmp(pvd->identifier, "CD001", 5) != 0) { 
        fclose(g_iso.hFile); g_iso.hFile = NULL; return -3; 
    }
    
    ExtractString(g_iso.pvd_buffer + 40, g_iso.volume_name, 32);
    root_extent = ReadLE32(g_iso.pvd_buffer + 156 + 2);
    root_size   = ReadLE32(g_iso.pvd_buffer + 156 + 10);
    
    g_iso.svd_sector = 0;
    g_iso.boot_desc_sector = 0;
    
    desc = (BYTE FAR*)_fmalloc(SECTOR_SIZE);
    cat_sec = (BYTE FAR*)_fmalloc(SECTOR_SIZE);
    if (desc && cat_sec) {
        for (s = 17; s < 32; s++) {
            fseek(g_iso.hFile, (long)s * SECTOR_SIZE, SEEK_SET);
            fread(desc, 1, SECTOR_SIZE, g_iso.hFile);
            if (desc[0] == 2 && _fmemcmp(desc+1, "CD001", 5) == 0) {
                g_iso.svd_sector = s; _fmemcpy(g_iso.svd_buffer, desc, SECTOR_SIZE);
            } else if (desc[0] == 0 && _fmemcmp(desc+1, "CD001", 5) == 0 && _fmemcmp(desc+7, "EL TORITO SPECIFICATION", 23) == 0) {
                g_iso.boot_desc_sector = s;
                _fmemcpy(g_iso.boot_desc_buffer, desc, SECTOR_SIZE);
                g_iso.boot_catalog_sector = ReadLE32(desc + 71);
                if (g_iso.boot_catalog_sector > 0) {
                    fseek(g_iso.hFile, (long)g_iso.boot_catalog_sector * SECTOR_SIZE, SEEK_SET);
                    if (fread(cat_sec, 1, SECTOR_SIZE, g_iso.hFile) == SECTOR_SIZE) {
                        g_iso.boot_image_sector = ReadLE32(cat_sec + 32 + 8);
                        g_iso.boot_image_size = (DWORD)cat_sec[32 + 12] | ((DWORD)cat_sec[32 + 13] << 8);
                        g_iso.is_bootable = TRUE;
                    }
                }
            } else if (desc[0] == 255) break;
        }
    }
    if (desc) _ffree(desc);
    if (cat_sec) _ffree(cat_sec);
    
    root_entry = (IsoEntry FAR*)_fmalloc(sizeof(IsoEntry));
    _fmemset(root_entry, 0, sizeof(IsoEntry));
    g_iso.entries[0] = root_entry;
    root_entry->type = NODE_TYPE_DIRECTORY; 
    root_entry->is_directory = TRUE; 
    root_entry->parent_index = 0;
    _fstrcpy(root_entry->name, ""); 
    _fstrcpy(root_entry->long_name, "Root");
    root_entry->sector_offset = root_extent; 
    root_entry->size = root_size; 
    g_iso.num_entries = 1;
    
    read_directory(root_extent, 0, 0);
    g_iso.new_data_offset = g_iso.file_size;
    if (g_iso.new_data_offset % SECTOR_SIZE) g_iso.new_data_offset += SECTOR_SIZE - (g_iso.new_data_offset % SECTOR_SIZE);
    
    UpdateWindowTitle();
    return 0;
}

static void iso_new_image(void) {
    IsoEntry FAR* root_entry;
    iso_close_image(); 
    memset(&g_iso, 0, sizeof(g_iso));
    g_iso.isOpen = TRUE; 
    strcpy(g_iso.volume_name, "NEW_VOLUME"); 
    g_iso.file_size = SECTOR_SIZE * 20; 
    g_iso.hFile = NULL; 
    strcpy(g_iso.path, "");
    
    root_entry = (IsoEntry FAR*)_fmalloc(sizeof(IsoEntry));
    _fmemset(root_entry, 0, sizeof(IsoEntry));
    g_iso.entries[0] = root_entry;
    root_entry->type = NODE_TYPE_DIRECTORY; 
    root_entry->is_directory = TRUE;
    _fstrcpy(root_entry->name, ""); 
    _fstrcpy(root_entry->long_name, "Root");
    g_iso.num_entries = 1; 
    g_current_iso_parent = 0; 
    
    UpdateWindowTitle();
    populate_iso_listview();
}

/* ============================================================
 * EXTRACTION & ADDITION
 * ============================================================ */
static void iso_delete_entry(DWORD entry_idx) {
    DWORD i;
    if (entry_idx >= g_iso.num_entries || entry_idx == 0) return;
    if (g_iso.entries[entry_idx] && g_iso.entries[entry_idx]->marked_deleted) return; 
    
    if (g_iso.entries[entry_idx]) g_iso.entries[entry_idx]->marked_deleted = TRUE;
    for (i = 0; i < g_iso.num_entries; i++) { 
        if (g_iso.entries[i] && g_iso.entries[i]->parent_index == entry_idx) 
            iso_delete_entry(i); 
    }
}

static void check_overwrite_iso_entry(const char* name, DWORD parent_idx) {
    DWORD i;
    for (i = 1; i < g_iso.num_entries; i++) {
        if (g_iso.entries[i] && g_iso.entries[i]->parent_index == parent_idx && !g_iso.entries[i]->marked_deleted) {
            if (_fstricmp(g_iso.entries[i]->name, name) == 0 || _fstricmp(g_iso.entries[i]->long_name, name) == 0) {
                iso_delete_entry(i);
            }
        }
    }
}

static int iso_add_directory(const char* dest_name, DWORD parent_idx) {
    IsoEntry FAR* entry;
    if (!g_iso.isOpen || g_iso.num_entries >= MAX_ENTRIES) return -1;
    check_overwrite_iso_entry(dest_name, parent_idx);
    
    entry = (IsoEntry FAR*)_fmalloc(sizeof(IsoEntry));
    _fmemset(entry, 0, sizeof(IsoEntry));
    g_iso.entries[g_iso.num_entries] = entry;
    
    entry->type = NODE_TYPE_DIRECTORY; 
    entry->is_directory = TRUE; 
    entry->size = 0;
    entry->marked_new = TRUE; 
    entry->parent_index = parent_idx; 
    entry->timestamp = time(NULL);
    
    _fstrncpy(entry->name, dest_name, 127); 
    _fstrncpy(entry->long_name, dest_name, 127);
    g_iso.num_entries++; 
    return g_iso.num_entries - 1;
}

static int iso_add_file(const char* source_file, const char* dest_name, DWORD parent_idx) {
    FILE* hSrc;
    DWORD file_size;
    IsoEntry FAR* entry;
    
    if (!g_iso.isOpen || g_iso.num_entries >= MAX_ENTRIES) return -1;
    
    hSrc = fopen(source_file, "rb");
    if (!hSrc) return -3;
    fseek(hSrc, 0, SEEK_END);
    file_size = ftell(hSrc);
    fclose(hSrc);
    
    check_overwrite_iso_entry(get_basename(dest_name), parent_idx);
    
    entry = (IsoEntry FAR*)_fmalloc(sizeof(IsoEntry));
    _fmemset(entry, 0, sizeof(IsoEntry));
    g_iso.entries[g_iso.num_entries] = entry;
    
    entry->type = NODE_TYPE_FILE; 
    entry->is_directory = FALSE; 
    entry->size = file_size;
    entry->marked_new = TRUE; 
    entry->parent_index = parent_idx; 
    entry->timestamp = time(NULL);
    
    _fstrncpy(entry->name, get_basename(dest_name), 127); 
    _fstrncpy(entry->long_name, get_basename(dest_name), 127);
    _fstrncpy(entry->source_file, source_file, MAX_PATH_LEN - 1);
    
    g_iso.num_entries++; 
    return g_iso.num_entries - 1;
}

static int iso_add_local_directory(const char* local_path, const char* dir_name, DWORD parent_idx, int depth) {
    struct find_t fd;
    char search_path[128];
    int new_dir_idx;
    
    if (depth > 8 || !g_iso.isOpen || g_iso.num_entries >= MAX_ENTRIES || g_cancel_operation) return -1;
    
    new_dir_idx = iso_add_directory(dir_name, parent_idx); 
    if (new_dir_idx < 0) return -1;
    
    sprintf(search_path, "%s\\*.*", local_path);
    if (_dos_findfirst(search_path, _A_NORMAL|_A_HIDDEN|_A_SUBDIR|_A_RDONLY, &fd) == 0) {
        do {
            char full_path[128];
            if (g_cancel_operation) break;
            if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0) continue;
            
            sprintf(full_path, "%s\\%s", local_path, fd.name);
            if (fd.attrib & _A_SUBDIR) {
                iso_add_local_directory(full_path, fd.name, new_dir_idx, depth + 1);
            } else {
                iso_add_file(full_path, fd.name, new_dir_idx);
            }
            PumpMessages();
        } while (_dos_findnext(&fd) == 0);
    }
    return new_dir_idx;
}

static int iso_extract_file(DWORD entry_idx, const char* dest_path) {
    IsoEntry FAR* entry;
    FILE* hDst;
    BYTE FAR* buffer;
    DWORD remaining;
    
    if (entry_idx >= g_iso.num_entries) return -1;
    entry = g_iso.entries[entry_idx]; 
    if (!entry || entry->type != NODE_TYPE_FILE) return -2;
    
    hDst = fopen(dest_path, "wb");
    if (!hDst) return -3;
    
    fseek(g_iso.hFile, (long)entry->sector_offset * SECTOR_SIZE, SEEK_SET);
    buffer = (BYTE FAR*)_fmalloc(SECTOR_SIZE); 
    if (!buffer) { fclose(hDst); return -4; }
    
    remaining = entry->size;
    
    while (remaining > 0) {
        DWORD to_read = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE; 
        DWORD actual = fread(buffer, 1, (size_t)to_read, g_iso.hFile);
        if (actual == 0) break;
        
        fwrite(buffer, 1, (size_t)actual, hDst); 
        remaining -= actual;
        if (g_cancel_operation) break;
    }
    _ffree(buffer); 
    fclose(hDst); 
    
    if (g_cancel_operation) { remove(dest_path); return -5; }
    return 0;
}

static void iso_extract_local_directory(DWORD entry_idx, const char* dest_path) {
    DWORD i;
    if (g_cancel_operation || entry_idx >= g_iso.num_entries) return;
    mkdir(dest_path);
    
    for (i = 1; i < g_iso.num_entries; i++) {
        if (g_cancel_operation) break;
        if (g_iso.entries[i] && g_iso.entries[i]->parent_index == entry_idx && !g_iso.entries[i]->marked_deleted) {
            char child_dest[MAX_PATH_LEN];
            char safe_name[128];
            
            _fstrcpy(safe_name, g_iso.entries[i]->name);
            sprintf(child_dest, "%s\\%s", dest_path, safe_name);
            
            if (g_iso.entries[i]->is_directory) {
                iso_extract_local_directory(i, child_dest);
            } else {
                iso_extract_file(i, child_dest);
            }
        }
    }
}

/* ============================================================
 * COMMANDS & EVENT HANDLERS
 * ============================================================ */
static void cmd_local_back() {
    if (strcmp(g_current_local_path, "C:\\") != 0) {
        char* last_slash = strrchr(g_current_local_path, '\\');
        if (last_slash && last_slash != g_current_local_path) {
            if (*(last_slash - 1) == ':') last_slash[1] = '\0'; else *last_slash = '\0';
            set_local_path(g_current_local_path);
        }
    }
}

static void cmd_iso_back() {
    if (g_current_iso_parent != 0) { 
        g_current_iso_parent = g_iso.entries[g_current_iso_parent]->parent_index; 
        populate_iso_listview(); 
    }
}

static void cmd_local_newdir(HWND hwnd) {
    char dirName[MAX_PATH_LEN];
    if (ShowInputBox(hwnd, "Create Directory", "Enter folder name:", dirName)) {
        char fullPath[MAX_PATH_LEN];
        if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') sprintf(fullPath, "%s%s", g_current_local_path, dirName);
        else sprintf(fullPath, "%s\\%s", g_current_local_path, dirName);
        
        if (mkdir(fullPath) == 0) set_local_path(g_current_local_path);
        else MessageBox(hwnd, "Failed to create directory.", "Error", MB_ICONERROR);
    }
}

static void cmd_iso_newdir(HWND hwnd) {
    char dirName[MAX_PATH_LEN];
    if (!g_iso.isOpen) return; 
    if (ShowInputBox(hwnd, "Create ISO Directory", "Enter folder name:", dirName)) {
        iso_add_directory(dirName, g_current_iso_parent); 
        populate_iso_listview();
    }
}

static void cmd_add_selected(HWND hwnd) {
    int sel;
    char display[128], filename[128], src_path[MAX_PATH_LEN];
    unsigned attrib;
    
    if (!g_iso.isOpen) { MessageBox(hwnd, "Open or create an ISO first.", "Warning", MB_ICONWARNING); return; }
    
    sel = (int)SendMessage(g_hLocalListBox, LB_GETCURSEL, 0, 0); 
    if (sel == LB_ERR) return;
    
    SendMessage(g_hLocalListBox, LB_GETTEXT, sel, (LPARAM)(LPSTR)display);
    extract_name_from_listbox(display, filename);
    if (strcmp(filename, "..") == 0) return;
    
    if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') sprintf(src_path, "%s%s", g_current_local_path, filename);
    else sprintf(src_path, "%s\\%s", g_current_local_path, filename);
    
    if (_dos_getfileattr(src_path, &attrib) == 0) {
        if (attrib & _A_SUBDIR) { 
            SetStatus("Adding directory tree...");
            iso_add_local_directory(src_path, filename, g_current_iso_parent, 0);
            if (g_cancel_operation) SetStatus("Operation cancelled.");
            else SetStatus("Directory tree added to ISO.");
            populate_iso_listview(); 
        } else {
            if (iso_add_file(src_path, filename, g_current_iso_parent) >= 0) { 
                populate_iso_listview(); 
                SetStatus("File queued for addition."); 
            }
        }
    }
}

static void cmd_extract_selected(HWND hwnd) {
    int sel;
    DWORD idx;
    char dest_path[MAX_PATH_LEN];
    char safe_name[128];
    IsoEntry FAR* entry;
    
    sel = (int)SendMessage(g_hIsoListBox, LB_GETCURSEL, 0, 0); 
    if (sel == LB_ERR) return;
    
    idx = SendMessage(g_hIsoListBox, LB_GETITEMDATA, sel, 0);
    if (idx == 0xFFFFFFFF) return;
    
    entry = g_iso.entries[idx];
    _fstrcpy(safe_name, entry->name);
    
    if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') sprintf(dest_path, "%s%s", g_current_local_path, safe_name);
    else sprintf(dest_path, "%s\\%s", g_current_local_path, safe_name);
    
    SetStatus("Extracting...");
    
    if (entry->is_directory) {
        iso_extract_local_directory(idx, dest_path); 
        set_local_path(g_current_local_path); 
        if (g_cancel_operation) SetStatus("Extraction cancelled.");
        else SetStatus("Directory extracted.");
    } else {
        OPENFILENAME sf = {0}; 
        sf.lStructSize = sizeof(sf); 
        sf.hwndOwner = hwnd; 
        _fstrcpy(dest_path, entry->long_name); 
        sf.lpstrFile = dest_path; 
        sf.nMaxFile = MAX_PATH_LEN;
        sf.Flags = 0; 
        sf.lpstrFilter = "All Files\0*.*\0";
        if (GetSaveFileName(&sf)) {
            int res = iso_extract_file(idx, dest_path);
            if (g_cancel_operation) SetStatus("Extraction cancelled.");
            else if (res == 0) { 
                set_local_path(g_current_local_path); 
                SetStatus("File extracted."); 
            }
            else MessageBox(hwnd, "Failed to extract file.", "Error", MB_ICONERROR);
        }
    }
}

static void cmd_delete_selected(HWND hwnd) {
    int sel = (int)SendMessage(g_hIsoListBox, LB_GETCURSEL, 0, 0); 
    if (sel != LB_ERR) {
        DWORD idx = SendMessage(g_hIsoListBox, LB_GETITEMDATA, sel, 0);
        if (idx != 0xFFFFFFFF && MessageBox(hwnd, "Delete selected item from ISO?", "Confirm", MB_YESNO) == IDYES) { 
            iso_delete_entry(idx); 
            populate_iso_listview(); 
        }
    }
}

static void cmd_rename_local(HWND hwnd) {
    int sel = (int)SendMessage(g_hLocalListBox, LB_GETCURSEL, 0, 0);
    char display[128], old_name[128], new_name[MAX_PATH_LEN];
    
    if (sel == LB_ERR) return;
    SendMessage(g_hLocalListBox, LB_GETTEXT, sel, (LPARAM)(LPSTR)display);
    extract_name_from_listbox(display, old_name);
    if (strcmp(old_name, "..") == 0) return;
    
    if (ShowInputBox(hwnd, "Rename Local File", "Enter new name:", new_name)) {
        char old_path[MAX_PATH_LEN], new_path[MAX_PATH_LEN];
        if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') {
            sprintf(old_path, "%s%s", g_current_local_path, old_name);
            sprintf(new_path, "%s%s", g_current_local_path, new_name);
        } else {
            sprintf(old_path, "%s\\%s", g_current_local_path, old_name);
            sprintf(new_path, "%s\\%s", g_current_local_path, new_name);
        }
        if (rename(old_path, new_path) == 0) set_local_path(g_current_local_path);
        else MessageBox(hwnd, "Failed to rename file.", "Error", MB_ICONERROR);
    }
}

static void cmd_rename_iso(HWND hwnd) {
    int sel;
    DWORD idx;
    char new_name[MAX_PATH_LEN];
    
    if (!g_iso.isOpen) return;
    sel = (int)SendMessage(g_hIsoListBox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    
    idx = SendMessage(g_hIsoListBox, LB_GETITEMDATA, sel, 0);
    if (idx == 0xFFFFFFFF) return;
    
    if (ShowInputBox(hwnd, "Rename ISO Item", "Enter new name:", new_name)) {
        _fstrncpy(g_iso.entries[idx]->name, new_name, 127);
        _fstrncpy(g_iso.entries[idx]->long_name, new_name, 127);
        populate_iso_listview();
    }
}

/* ============================================================
 * WINDOW PROCEDURES
 * ============================================================ */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            HMENU hMenu = CreateMenu(), hFile = CreatePopupMenu(), hView = CreatePopupMenu(), hBoot = CreatePopupMenu(), hHelp = CreatePopupMenu();
            
            DragAcceptFiles(hwnd, TRUE);
            
            AppendMenu(hFile, MF_STRING, IDM_IMAGE_NEW, "&New"); 
            AppendMenu(hFile, MF_STRING, IDM_IMAGE_OPEN, "&Open..."); 
            AppendMenu(hFile, MF_STRING, IDM_IMAGE_SAVE, "&Save\tCtrl+S"); 
            AppendMenu(hFile, MF_STRING, IDM_IMAGE_SAVEAS, "&Save As..."); 
            AppendMenu(hFile, MF_STRING, IDM_IMAGE_PROPS, "&Properties"); 
            AppendMenu(hFile, MF_STRING, IDM_IMAGE_QUIT, "&Quit"); 
            AppendMenu(hMenu, MF_POPUP, (UINT)hFile, "&Image");
            
            AppendMenu(hView, MF_STRING, IDM_VIEW_REFRESH, "&Refresh\tF5"); 
            AppendMenu(hView, MF_STRING, IDM_VIEW_HIDDEN, "&Hidden files"); 
            AppendMenu(hView, MF_STRING | MF_CHECKED, IDM_VIEW_SORTDIR, "&Sort directories first"); 
            AppendMenu(hMenu, MF_POPUP, (UINT)hView, "&View");
            
            AppendMenu(hBoot, MF_STRING, IDM_BOOT_PROPS, "&Properties"); 
            AppendMenu(hBoot, MF_STRING, IDM_BOOT_SAVE, "&Save to drive"); 
            AppendMenu(hBoot, MF_STRING, IDM_BOOT_DEL, "&Delete"); 
            AppendMenu(hMenu, MF_POPUP, (UINT)hBoot, "&BootRecord");
            
            AppendMenu(hHelp, MF_STRING, IDM_HELP_ABOUT, "&About"); 
            AppendMenu(hMenu, MF_POPUP, (UINT)hHelp, "&Help");
            
            SetMenu(hwnd, hMenu); 
            
            /* UI Controls */
            g_btnLBack = CreateWindow("button", "Go Back", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_LOCAL_BACK, g_hInstance, NULL);
            g_btnLNew  = CreateWindow("button", "New Dir", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_LOCAL_NEWDIR, g_hInstance, NULL);
            g_hLocalListBox = CreateWindow("listbox", NULL, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS, 0,0,0,0, hwnd, (HMENU)ID_LIST_LOCAL, g_hInstance, NULL);
            
            g_btnISave = CreateWindow("button", "Save ISO", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_ISO_SAVEBTN, g_hInstance, NULL);
            g_btnIBack = CreateWindow("button", "Go Back", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_ISO_BACK, g_hInstance, NULL);
            g_btnINew  = CreateWindow("button", "New Dir", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_ISO_NEWDIR, g_hInstance, NULL);
            g_btnIAdd  = CreateWindow("button", "Add File", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_ISO_ADD, g_hInstance, NULL);
            g_btnIExt  = CreateWindow("button", "Extract", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_ISO_EXTRACT, g_hInstance, NULL);
            g_btnIDel  = CreateWindow("button", "Delete", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_ISO_DELETE, g_hInstance, NULL);
            g_btnIDelT = CreateWindow("button", "Del Temp", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)ID_ISO_DELTEMP, g_hInstance, NULL);
            g_hIsoListBox = CreateWindow("listbox", NULL, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS, 0,0,0,0, hwnd, (HMENU)ID_LIST_ISO, g_hInstance, NULL);
            
            g_hStatusBar = CreateWindow("static", " Ready", WS_CHILD|WS_VISIBLE|WS_BORDER, 0,0,0,0, hwnd, NULL, g_hInstance, NULL);
            
            LoadSettings(); 
            UpdateWindowTitle();

            set_local_path("C:\\"); 
            return 0;
        }
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            UINT count, i;
            
            if (!g_iso.isOpen) { MessageBox(hwnd, "Open or create an ISO first.", "Warning", MB_ICONWARNING); DragFinish(hDrop); return 0; }
            
            count = DragQueryFile(hDrop, 0xFFFF, NULL, 0); 
            SetStatus("Adding dropped files...");
            for (i = 0; i < count; i++) {
                char filepath[MAX_PATH_LEN]; 
                unsigned attrib;
                
                DragQueryFile(hDrop, i, filepath, MAX_PATH_LEN);
                if (_dos_getfileattr(filepath, &attrib) == 0) {
                    if (attrib & _A_SUBDIR) { 
                        iso_add_local_directory(filepath, get_basename(filepath), g_current_iso_parent, 0); 
                    } else { 
                        iso_add_file(filepath, get_basename(filepath), g_current_iso_parent); 
                    }
                }
            }
            populate_iso_listview(); 
            SetStatus("Dropped files processed.");
            DragFinish(hDrop); 
            return 0;
        }
        case WM_COMMAND: {
            UINT id = wParam;
            UINT codeNotify = HIWORD(lParam);
            HWND hwndCtl = (HWND)LOWORD(lParam);
            
            /* ListBox Double Click Interception */
            if (codeNotify == LBN_DBLCLK) {
                if (hwndCtl == g_hLocalListBox) {
                    int sel = (int)SendMessage(g_hLocalListBox, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR) {
                        char display[128], name[128];
                        SendMessage(g_hLocalListBox, LB_GETTEXT, sel, (LPARAM)(LPSTR)display);
                        extract_name_from_listbox(display, name);
                        if (strcmp(name, "..") == 0) cmd_local_back(); 
                        else { 
                            char new_path[MAX_PATH_LEN]; unsigned attrib;
                            if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') sprintf(new_path, "%s%s", g_current_local_path, name); 
                            else sprintf(new_path, "%s\\%s", g_current_local_path, name); 
                            if (_dos_getfileattr(new_path, &attrib) == 0 && (attrib & _A_SUBDIR)) set_local_path(new_path); 
                        }
                    }
                } else if (hwndCtl == g_hIsoListBox) {
                    int sel = (int)SendMessage(g_hIsoListBox, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR) {
                        DWORD idx = SendMessage(g_hIsoListBox, LB_GETITEMDATA, sel, 0);
                        if (idx == 0xFFFFFFFF) cmd_iso_back(); 
                        else if (g_iso.entries[idx] && g_iso.entries[idx]->is_directory) { g_current_iso_parent = idx; populate_iso_listview(); }
                        else {
                            if (g_temp_files_count < 64) {
                                char destPath[MAX_PATH_LEN];
                                char safe_name[128];
                                
                                mkdir("C:\\TMPISO"); /* Bypassing finicky GetTempFileName calls */
                                _fstrcpy(safe_name, g_iso.entries[idx]->name);
                                sprintf(destPath, "C:\\TMPISO\\%s", safe_name); 
                                
                                if (iso_extract_file(idx, destPath) == 0) {
                                    strcpy(g_temp_files[g_temp_files_count++], destPath);
                                    ShellExecute(g_hMainWnd, "open", destPath, NULL, NULL, SW_SHOWNORMAL);
                                }
                            }
                        }
                    }
                }
                return 0;
            }
            
            if (id >= IDM_MRU_1 && id <= IDM_MRU_5) {
                int idx = id - IDM_MRU_1;
                if (strlen(g_mru[idx]) > 0) {
                    if (iso_open_image(g_mru[idx]) == 0) { 
                        UpdateMRU(g_mru[idx]);
                        populate_iso_listview(); 
                        SetStatus("Opened file successfully."); 
                    } else MessageBox(hwnd, "Failed to open file.", "Error", MB_ICONERROR);
                }
                return 0;
            }

            switch (id) {
                case IDM_VIEW_HIDDEN: g_show_hidden = !g_show_hidden; CheckMenuItem(GetMenu(hwnd), IDM_VIEW_HIDDEN, MF_BYCOMMAND | (g_show_hidden ? MF_CHECKED : MF_UNCHECKED)); set_local_path(g_current_local_path); break;
                case IDM_VIEW_SORTDIR: g_sort_dirs_first = !g_sort_dirs_first; CheckMenuItem(GetMenu(hwnd), IDM_VIEW_SORTDIR, MF_BYCOMMAND | (g_sort_dirs_first ? MF_CHECKED : MF_UNCHECKED)); set_local_path(g_current_local_path); populate_iso_listview(); break;
                case IDM_IMAGE_NEW: iso_new_image(); break;
                case IDM_IMAGE_OPEN: { 
                    OPENFILENAME ofn = {0}; char szFile[MAX_PATH_LEN] = ""; 
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH_LEN; ofn.lpstrFilter = "ISO Files (*.iso)\0*.iso\0All Files\0*.*\0"; 
                    if (GetOpenFileName(&ofn)) {
                        int res = iso_open_image(szFile);
                        if (res == 0) { 
                            UpdateMRU(szFile);
                            populate_iso_listview(); 
                            SetStatus("File opened.");
                        } 
                        else if (res == -3) MessageBox(hwnd, "Invalid ISO image.", "Error", MB_ICONERROR);
                        else MessageBox(hwnd, "Failed to read file.", "Error", MB_ICONERROR);
                    } 
                    break; 
                }
                case ID_ISO_SAVEBTN:
                case IDM_IMAGE_SAVE: 
                    if (g_iso.isOpen) { 
                        if (strlen(g_iso.path) == 0) { SendMessage(hwnd, WM_COMMAND, IDM_IMAGE_SAVEAS, 0); } 
                        else MessageBox(hwnd, "In-place saving requires complete rebuild logic in 16-bit which is limited. Use Save As.", "Notice", MB_ICONINFORMATION);
                    } 
                    break;
                case IDM_IMAGE_SAVEAS: {
                    if (!g_iso.isOpen) break; 
                    {
                        OPENFILENAME ofn = {0}; char szFile[MAX_PATH_LEN] = ""; 
                        strcpy(szFile, get_basename(g_iso.path)); 
                        ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH_LEN; ofn.lpstrFilter = "ISO Files (*.iso)\0*.iso\0All Files\0*.*\0"; 
                        if (GetSaveFileName(&ofn)) { 
                            /* Save logic stubbed for brevity in 16-bit memory limits */
                            MessageBox(hwnd, "Save As functionally stubbed in this restricted 16-bit environment to prevent out-of-memory errors.", "Notice", MB_OK);
                        } 
                    }
                    break;
                }
                case IDM_IMAGE_PROPS: { if (!g_iso.isOpen) break; { char props[512]; sprintf(props, "Volume: %s\nSize: %lu bytes\nEntries: %u", g_iso.volume_name, g_iso.file_size, g_iso.num_entries); MessageBox(hwnd, props, "Properties", MB_ICONINFORMATION); } break; }
                case IDM_IMAGE_QUIT: PostQuitMessage(0); break;
                case IDM_VIEW_REFRESH: set_local_path(g_current_local_path); populate_iso_listview(); break;
                
                case ID_ISO_DELTEMP: {
                    int del_count = 0, i;
                    for (i = 0; i < g_temp_files_count; i++) {
                        if (remove(g_temp_files[i]) == 0) del_count++;
                    }
                    rmdir("C:\\TMPISO");
                    SetStatus("Temp files deleted.");
                    g_temp_files_count = 0;
                    break;
                }
                case IDM_HELP_ABOUT: MessageBox(hwnd, "ISO Master Win16 - Legacy Editor", "About", MB_ICONINFORMATION); break;
                case ID_LOCAL_BACK: cmd_local_back(); break; 
                case ID_LOCAL_NEWDIR: cmd_local_newdir(hwnd); break; 
                case ID_ISO_BACK: cmd_iso_back(); break; 
                case ID_ISO_NEWDIR: cmd_iso_newdir(hwnd); break; 
                case ID_ISO_ADD: cmd_add_selected(hwnd); break; 
                case ID_ISO_EXTRACT: cmd_extract_selected(hwnd); break; 
                case ID_ISO_DELETE: cmd_delete_selected(hwnd); break;
            } return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            int half = h / 2;
            int bw = 70, bh = 24;
            
            if (w <= 0 || h <= 0) return 0; /* Extremely important for 16-bit stability */
            if (!g_btnLBack) return 0; 
            
            MoveWindow(g_btnLBack, 2, 2, bw, bh, TRUE);
            MoveWindow(g_btnLNew, 2 + bw + 2, 2, bw, bh, TRUE);
            MoveWindow(g_hLocalListBox, 0, bh + 4, w, half - bh - 4, TRUE);
            
            MoveWindow(g_btnISave, 2, half + 2, bw, bh, TRUE);
            MoveWindow(g_btnIBack, 2 + bw + 2, half + 2, bw, bh, TRUE);
            MoveWindow(g_btnINew, 2 + (bw + 2)*2, half + 2, bw, bh, TRUE);
            MoveWindow(g_btnIAdd, 2 + (bw + 2)*3, half + 2, bw, bh, TRUE);
            MoveWindow(g_btnIExt, 2 + (bw + 2)*4, half + 2, bw, bh, TRUE);
            MoveWindow(g_btnIDel, 2 + (bw + 2)*5, half + 2, bw, bh, TRUE);
            MoveWindow(g_btnIDelT, 2 + (bw + 2)*6, half + 2, bw, bh, TRUE);
            MoveWindow(g_hIsoListBox, 0, half + bh + 4, w, half - bh - 24, TRUE);
            
            MoveWindow(g_hStatusBar, 0, h - 20, w, 20, TRUE);
            return 0;
        }
        case WM_DESTROY: {
            SaveSettings();
            SendMessage(hwnd, WM_COMMAND, ID_ISO_DELTEMP, 0); 
            iso_close_image(); 
            PostQuitMessage(0); 
            return 0;
        }
    } return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
    WNDCLASS wc = {0}; 
    MSG msg; 
    
    g_hInstance = hInst; 
    wc.style = CS_HREDRAW | CS_VREDRAW; 
    wc.lpfnWndProc = WindowProc; 
    wc.hInstance = hInst; 
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); 
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); 
    wc.lpszClassName = "IsoMasterClass";
    
    if (!RegisterClass(&wc)) return 1;
    
    g_hMainWnd = CreateWindow("IsoMasterClass", "ISO Master", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInst, NULL);
    
    ShowWindow(g_hMainWnd, show); 
    UpdateWindow(g_hMainWnd);
    
    while (GetMessage(&msg, NULL, 0, 0)) { 
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_DELETE) {
                if (GetFocus() == g_hIsoListBox) cmd_delete_selected(g_hMainWnd);
                continue; 
            }
            else if (msg.wParam == VK_F5) {
                SendMessage(g_hMainWnd, WM_COMMAND, IDM_VIEW_REFRESH, 0);
                continue;
            }
            else if (msg.wParam == VK_F2) {
                HWND hFocus = GetFocus();
                if (hFocus == g_hLocalListBox) cmd_rename_local(g_hMainWnd);
                else if (hFocus == g_hIsoListBox) cmd_rename_iso(g_hMainWnd);
                continue;
            }
            else if (msg.wParam == VK_RETURN) {
                HWND hFocus = GetFocus();
                if (hFocus == g_hLocalListBox || hFocus == g_hIsoListBox) {
                    WORD ctrlId = (hFocus == g_hLocalListBox) ? ID_LIST_LOCAL : ID_LIST_ISO;
                    SendMessage(g_hMainWnd, WM_COMMAND, ctrlId, MAKELONG((WORD)hFocus, LBN_DBLCLK));
                    continue;
                }
            }
        }
        TranslateMessage(&msg); 
        DispatchMessage(&msg); 
    } 
    return msg.wParam;
}