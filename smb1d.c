/*
 * Standalone SMB1 Server Stub & Configuration App (Win16 / Open Watcom)
 * 
 * COMPILATION:
 *   wcl -l=windows -ml -Os -s smb1d.c winsock.lib
 */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include <malloc.h>

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned long      uint32_t;
typedef unsigned __int64   uint64_t;

#define IDE_PORT            2001
#define IDL_IPS             2002
#define IDE_LOG             2003
#define IDB_OK              2004
#define IDB_TOGGLE_SRV      2005
#define IDB_CANCEL          2006
#define IDC_CMB_SHARES      2007
#define IDE_SHAREPATH       2008
#define IDB_ADD_SHARE       2010
#define IDB_DEL_SHARE       2011

#define TIMER_NETWORK       1001
#define MAX_SHARES          10
#define MAX_CLIENTS         8
#define MAX_PATH            144

#pragma pack(1)
typedef struct {
    uint8_t  protocol_id[4];
    uint8_t  cmd;
    uint32_t status;
    uint8_t  flags1;
    uint16_t flags2;
    uint16_t pid_high;
    uint8_t  signature[8];
    uint16_t reserved;
    uint16_t tid;
    uint16_t pid_low;
    uint16_t uid;
    uint16_t mid;
} SMB1Header;
#pragma pack()

typedef struct {
    char name[64];
    char path[MAX_PATH];
} ServerShare;

typedef struct {
    uint16_t tid;
    char path[MAX_PATH];
} TidMap;

typedef struct {
    SOCKET sock;
    int active;
} ClientState;

static ServerShare g_shares[MAX_SHARES];
static int g_share_count = 0;
static ClientState g_clients[MAX_CLIENTS];

static HWND g_hMain = NULL;
static HWND g_hLogEdit = NULL;
static HWND g_hCmbShares = NULL;
static HWND g_hSharePath = NULL;
static HWND hPort, hIPs, hToggle;
static HINSTANCE g_hInst;
static char g_ini_path[MAX_PATH];

static int g_server_running = 0;
static SOCKET g_listen_socket = INVALID_SOCKET;
static int g_port = 445;

static char g_log_buffer[4096] = "Server initialized.\r\n";

/* ==========================================================================
   CONFIG LOAD/SAVE
   ========================================================================== */
static void init_ini_path(void) {
    char FAR *ext;
    GetModuleFileName(NULL, g_ini_path, MAX_PATH);
    ext = _fstrrchr(g_ini_path, '.');
    if (ext) _fstrcpy(ext, ".INI"); else _fstrcat(g_ini_path, ".INI");
}

static void load_config(void) {
    char names[512];
    char paths[1024];
    char FAR *n_tok;
    char FAR *p_tok;
    
    init_ini_path();
    g_port = GetPrivateProfileInt("Config", "Port", 445, g_ini_path);
    
    _fmemset(names, 0, sizeof(names));
    _fmemset(paths, 0, sizeof(paths));
    
    GetPrivateProfileString("Config", "sharename", "public", names, sizeof(names), g_ini_path);
    GetPrivateProfileString("Config", "sharepath", "C:\\", paths, sizeof(paths), g_ini_path);
    
    g_share_count = 0;
    n_tok = _fstrtok(names, "|");
    p_tok = _fstrtok(paths, "|");
    while (n_tok && p_tok && g_share_count < MAX_SHARES) {
        _fstrcpy(g_shares[g_share_count].name, n_tok);
        _fstrcpy(g_shares[g_share_count].path, p_tok);
        g_share_count++;
        n_tok = _fstrtok(NULL, "|");
        p_tok = _fstrtok(NULL, "|");
    }
    if (g_share_count == 0) {
        _fstrcpy(g_shares[0].name, "public"); _fstrcpy(g_shares[0].path, "C:\\"); g_share_count = 1;
    }
}

static void save_config(void) {
    char pStr[16];
    char names[512];
    char paths[1024];
    int i;
    
    sprintf(pStr, "%d", g_port);
    WritePrivateProfileString("Config", "Port", pStr, g_ini_path);
    
    _fmemset(names, 0, sizeof(names));
    _fmemset(paths, 0, sizeof(paths));
    
    for (i = 0; i < g_share_count; i++) {
        if (i > 0) { _fstrcat(names, "|"); _fstrcat(paths, "|"); }
        _fstrcat(names, g_shares[i].name);
        _fstrcat(paths, g_shares[i].path);
    }
    WritePrivateProfileString("Config", "sharename", names, g_ini_path);
    WritePrivateProfileString("Config", "sharepath", paths, g_ini_path);
}

/* ==========================================================================
   UTILITY & LOGGING
   ========================================================================== */
static void server_log(const char FAR *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);

    if (_fstrlen(g_log_buffer) + _fstrlen(buf) + 4 >= sizeof(g_log_buffer)) {
        _fmemmove(g_log_buffer, g_log_buffer + 1024, sizeof(g_log_buffer) - 1024);
        g_log_buffer[sizeof(g_log_buffer) - 1] = '\0';
    }
    _fstrcat(g_log_buffer, buf);
    _fstrcat(g_log_buffer, "\r\n");

    if (g_hLogEdit && IsWindow(g_hLogEdit)) {
        SendMessage(g_hLogEdit, WM_SETTEXT, 0, (LPARAM)(char FAR*)g_log_buffer);
        SendMessage(g_hLogEdit, EM_LINESCROLL, 0, SendMessage(g_hLogEdit, EM_GETLINECOUNT, 0, 0L));
    }
}

static int send_packet(SOCKET sock, const uint8_t FAR *data, uint32_t len) {
    uint8_t net_len[4];
    uint32_t sent = 0;
    int chunk, r;

    net_len[0] = 0x00;
    net_len[1] = (uint8_t)((len >> 16) & 0xFF);
    net_len[2] = (uint8_t)((len >> 8) & 0xFF);
    net_len[3] = (uint8_t)(len & 0xFF);
    
    if (send(sock, (char FAR*)net_len, 4, 0) != 4) return 0;
    
    while (sent < len) {
        chunk = (len - sent > 30000) ? 30000 : (int)(len - sent);
        r = send(sock, (char FAR*)(data + sent), chunk, 0);
        if (r <= 0) return 0;
        sent += r;
    }
    return 1;
}

/* ==========================================================================
   SMB1 SERVER HANDLERS
   ========================================================================== */
static void handle_negotiate(SOCKET client, SMB1Header FAR *req_hdr, uint8_t FAR *req, uint32_t req_len) {
    uint8_t resp[128]; 
    SMB1Header FAR *hdr = (SMB1Header FAR *)resp;
    uint8_t FAR *w;
    uint16_t FAR *bcc_ptr;
    
    uint16_t d_idx = 0;
    uint16_t i, c_idx = 0;
    uint16_t bcc = *(uint16_t FAR*)(req + sizeof(SMB1Header) + 1);
    uint8_t FAR *d_ptr = req + sizeof(SMB1Header) + 3;
    
    /* Dynamically search for NT LM 0.12 to return the correct dialect index */
    for (i = 0; i < bcc && i < req_len; ) {
        if (d_ptr[i] == 0x02) {
            if (_fstrcmp((char FAR*)&d_ptr[i+1], "NT LM 0.12") == 0) { d_idx = c_idx; break; }
            c_idx++;
            i += _fstrlen((char FAR*)&d_ptr[i+1]) + 2;
        } else break;
    }

    _fmemset(resp, 0, sizeof(resp));
    _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
    hdr->flags1 |= 0x80; hdr->status = 0;
    
    w = resp + sizeof(SMB1Header);
    *w++ = 17;                          
    *(uint16_t FAR*)w = d_idx; w += 2;          
    *w++ = 0x03;                        
    *(uint16_t FAR*)w = 50; w += 2;         
    *(uint16_t FAR*)w = 1; w += 2;          
    *(uint32_t FAR*)w = 65536; w += 4;      
    *(uint32_t FAR*)w = 65536; w += 4;      
    *(uint32_t FAR*)w = 1; w += 4;          
    *(uint32_t FAR*)w = 0x00000050; w += 4; /* CAP_NT_SMBS | CAP_STATUS32 */
    *(uint64_t FAR*)w = 0; w += 8;          
    *(uint16_t FAR*)w = 0; w += 2;          
    *w++ = 8;                           
    
    bcc_ptr = (uint16_t FAR*)w; w += 2;
    _fmemset(w, 0, 8); w += 8;            
    _fstrcpy((char FAR*)w, "WORKGROUP"); w += 10;
    _fstrcpy((char FAR*)w, "SMB1SRV"); w += 8;
    
    *bcc_ptr = (uint16_t)(w - (uint8_t FAR*)bcc_ptr - 2);
    send_packet(client, resp, (uint32_t)(w - resp));
    server_log("Handled: Negotiate Protocol");
}

static void handle_session_setup(SOCKET client, SMB1Header FAR *req_hdr) {
    uint8_t resp[128]; 
    SMB1Header FAR *hdr = (SMB1Header FAR *)resp;
    uint8_t FAR *w;
    uint16_t FAR *bcc;
    
    _fmemset(resp, 0, sizeof(resp));
    _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
    hdr->flags1 |= 0x80; hdr->uid = 100; 
    
    w = resp + sizeof(SMB1Header);
    *w++ = 3;                           
    *w++ = 0xFF; *w++ = 0;              
    *(uint16_t FAR*)w = 0; w += 2;          
    *(uint16_t FAR*)w = 0; w += 2; /* Action = 0 (Logged in completely - bypasses strict Guest limits) */         
    
    bcc = (uint16_t FAR*)w; w += 2;
    _fstrcpy((char FAR*)w, "Unix"); w += 5;
    _fstrcpy((char FAR*)w, "Samba"); w += 6;
    _fstrcpy((char FAR*)w, "WORKGROUP"); w += 10;
    
    *bcc = (uint16_t)(w - (uint8_t FAR*)bcc - 2);
    send_packet(client, resp, (uint32_t)(w - resp));
    server_log("Handled: Session Setup (Auth Accepted)");
}

static void handle_tree_connect(SOCKET client, SMB1Header FAR *req_hdr, TidMap FAR *maps, int FAR *map_count) {
    uint8_t resp[128]; 
    SMB1Header FAR *hdr = (SMB1Header FAR *)resp;
    uint8_t FAR *w;
    uint8_t FAR *data;
    uint8_t FAR *path_ptr;
    uint16_t FAR *bcc;
    uint16_t new_tid;
    uint8_t wct;
    uint16_t FAR *vwv;
    uint16_t pass_len;
    char requested[MAX_PATH];
    char FAR *share_name;
    char mapped_path[MAX_PATH];
    char FAR *service_str;
    int i;
    
    _fmemset(resp, 0, sizeof(resp));
    _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
    hdr->flags1 |= 0x80; 
    
    new_tid = (*map_count) + 1;
    hdr->tid = new_tid; 
    
    w = (uint8_t FAR*)req_hdr + sizeof(SMB1Header);
    wct = *w;
    vwv = (uint16_t FAR*)(w + 1);
    data = w + 1 + wct*2 + 2;
    
    pass_len = vwv[3];
    path_ptr = data + pass_len;
    
    _fmemset(requested, 0, sizeof(requested));
    if (req_hdr->flags2 & 0x8000) {
        uint16_t FAR *u;
        if ((path_ptr - (uint8_t FAR*)req_hdr) % 2 != 0) path_ptr++;
        u = (uint16_t FAR*)path_ptr;
        for (i = 0; u[i] && i < MAX_PATH - 1; i++) requested[i] = (char)u[i];
    } else {
        _fstrcpy(requested, (char FAR*)path_ptr);
    }
    
    share_name = _fstrrchr(requested, '\\');
    if (share_name) share_name++; else share_name = requested;
    
    _fstrcpy(mapped_path, "C:\\"); 
    service_str = "A:";
    
    if (_fstricmp(share_name, "IPC$") == 0) {
        _fstrcpy(mapped_path, "\\IPC$");
        service_str = "IPC";
    } else {
        for (i = 0; i < g_share_count; i++) {
            if (_fstricmp(g_shares[i].name, share_name) == 0) {
                _fstrcpy(mapped_path, g_shares[i].path);
                break;
            }
        }
    }
    
    if (*map_count < 32) {
        maps[*map_count].tid = new_tid;
        _fstrcpy(maps[*map_count].path, mapped_path);
        (*map_count)++;
    }
    
    server_log("Tree Connect: \\%s -> %s (TID %d)", (char FAR*)share_name, (char FAR*)mapped_path, new_tid);
    
    w = resp + sizeof(SMB1Header);
    *w++ = 3; 
    *w++ = 0xFF; *w++ = 0; 
    *(uint16_t FAR*)w = 0; w += 2;          
    *(uint16_t FAR*)w = 0x01; w += 2;       
    
    bcc = (uint16_t FAR*)w; w += 2;
    _fstrcpy((char FAR*)w, service_str); w += _fstrlen(service_str) + 1;
    _fstrcpy((char FAR*)w, "NTFS"); w += 5;
    
    *bcc = (uint16_t)(w - (uint8_t FAR*)bcc - 2);
    send_packet(client, resp, (uint32_t)(w - resp));
}

/* Missing NTCreateAndX intercept that caused all directory operations to fail */
static void handle_nt_create_andx(SOCKET client, SMB1Header FAR *req_hdr, uint8_t FAR *req) {
    uint8_t resp[128];
    SMB1Header FAR *hdr = (SMB1Header FAR *)resp;
    uint8_t FAR *w;
    uint16_t FAR *bcc;
    uint8_t wct = req[sizeof(SMB1Header)];
    uint16_t FAR *vwv = (uint16_t FAR*)(req + sizeof(SMB1Header) + 1);
    uint8_t FAR *data = req + sizeof(SMB1Header) + 1 + wct*2 + 2;
    char filename[MAX_PATH];
    
    _fmemset(filename, 0, sizeof(filename));
    if (req_hdr->flags2 & 0x8000) {
        uint16_t FAR *u = (uint16_t FAR*)(data + 1); 
        int i = 0;
        while (u[i] && i < MAX_PATH - 1) { filename[i] = (char)u[i]; i++; }
    } else {
        _fstrcpy(filename, (char FAR*)(data + 1));
    }
    
    /* Reject strict DCERPC pipes to force fallback handling */
    if (_fstrstr(filename, "srvsvc") != NULL || _fstrstr(filename, "lsarpc") != NULL) {
        _fmemset(resp, 0, sizeof(resp));
        _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
        hdr->flags1 |= 0x80; hdr->status = 0xC0000034; /* STATUS_OBJECT_NAME_NOT_FOUND */
        resp[sizeof(SMB1Header)] = 0;
        *(uint16_t FAR*)(resp + sizeof(SMB1Header) + 1) = 0;
        send_packet(client, resp, sizeof(SMB1Header) + 3);
        return;
    }

    /* Standard Handle Open (File/Dir) */
    _fmemset(resp, 0, sizeof(resp));
    _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
    hdr->flags1 |= 0x80; hdr->status = 0;
    
    w = resp + sizeof(SMB1Header);
    *w++ = 34; 
    *w++ = 0xFF; *w++ = 0; 
    *(uint16_t FAR*)w = 0; w += 2; 
    *w++ = 0; 
    *(uint16_t FAR*)w = 1234; w += 2; 
    *(uint32_t FAR*)w = 1; w += 4; 
    *(uint32_t FAR*)w = 0; w += 4; 
    *(uint64_t FAR*)w = 0; w += 8; 
    *(uint64_t FAR*)w = 0; w += 8; 
    *(uint64_t FAR*)w = 0; w += 8; 
    *(uint64_t FAR*)w = 0; w += 8; 
    *(uint32_t FAR*)w = 0x80; w += 4; 
    *(uint64_t FAR*)w = 4096; w += 8; 
    *(uint64_t FAR*)w = 4096; w += 8; 
    *(uint16_t FAR*)w = 0; w += 2; 
    *(uint16_t FAR*)w = 0; w += 2; 
    *w++ = 0; 
    
    bcc = (uint16_t FAR*)w; w += 2;
    *bcc = 0;
    send_packet(client, resp, (uint32_t)(w - resp));
}

static void handle_trans(SOCKET client, SMB1Header FAR *req_hdr, uint8_t FAR *req, uint32_t req_len) {
    uint8_t wct = req[sizeof(SMB1Header)];
    uint16_t FAR *vwv;
    uint16_t param_off, param_cnt, opcode;
    uint8_t FAR *params;
    
    if (wct < 14) return;
    vwv = (uint16_t FAR*)(req + sizeof(SMB1Header) + 1);
    
    param_off = vwv[10];
    param_cnt = vwv[9];
    
    if ((uint32_t)param_off + param_cnt > req_len || param_cnt < 2) return;
    params = req + param_off;
    opcode = *(uint16_t FAR*)params;
    
    if (opcode == 0x0000) { 
        uint8_t FAR *resp = _fmalloc(4096);
        SMB1Header FAR *hdr;
        uint8_t FAR *w;
        uint16_t FAR *resp_vwv;
        uint16_t FAR *bcc_ptr;
        uint8_t FAR *data_start;
        uint8_t FAR *out_params;
        uint8_t FAR *out_data;
        int pad, i;
        uint16_t p_len, d_len;

        if (!resp) return;
        _fmemset(resp, 0, 4096);
        hdr = (SMB1Header FAR *)resp;
        _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
        hdr->flags1 |= 0x80;
        
        w = resp + sizeof(SMB1Header);
        *w++ = 10; 
        resp_vwv = (uint16_t FAR*)w; w += 20;
        bcc_ptr = (uint16_t FAR*)w; w += 2;
        data_start = w;
        *w++ = 0; 
        
        out_params = w; w += 8;
        *(uint16_t FAR*)(out_params + 0) = 0; 
        *(uint16_t FAR*)(out_params + 2) = 0; 
        *(uint16_t FAR*)(out_params + 4) = g_share_count; 
        *(uint16_t FAR*)(out_params + 6) = g_share_count; 
        
        pad = (4 - ((w - resp) % 4)) % 4; w += pad;
        out_data = w;
        
        for (i = 0; i < g_share_count; i++) {
            _fmemset(w, 0, 20);
            _fstrncpy((char FAR*)w, g_shares[i].name, 13);
            *(uint16_t FAR*)(w + 14) = 0; 
            *(uint32_t FAR*)(w + 16) = 0; 
            w += 20;
        }
        
        p_len = 8;
        d_len = (uint16_t)(w - out_data);
        
        resp_vwv[0] = p_len; resp_vwv[1] = d_len; resp_vwv[2] = 0;
        resp_vwv[3] = p_len; resp_vwv[4] = (uint16_t)(out_params - resp); resp_vwv[5] = 0;
        resp_vwv[6] = d_len; resp_vwv[7] = (uint16_t)(out_data - resp);   resp_vwv[8] = 0;
        resp_vwv[9] = 0;
        
        *bcc_ptr = (uint16_t)(w - data_start);
        send_packet(client, resp, (uint32_t)(w - resp));
        _ffree(resp);
        server_log("Handled: NetShareEnum (%d shares)", g_share_count);
    } else {
        uint8_t resp[128];
        SMB1Header FAR *hdr;
        _fmemset(resp, 0, sizeof(resp));
        hdr = (SMB1Header FAR *)resp;
        _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
        hdr->flags1 |= 0x80;
        hdr->status = 0xC00000BB; /* STATUS_NOT_SUPPORTED */
        resp[sizeof(SMB1Header)] = 0;
        *(uint16_t FAR*)(resp + sizeof(SMB1Header) + 1) = 0;
        send_packet(client, resp, sizeof(SMB1Header) + 3);
    }
}

static void handle_trans2(SOCKET client, SMB1Header FAR *req_hdr, uint8_t FAR *req, uint32_t req_len, TidMap FAR *maps, int map_count) {
    uint8_t wct = req[sizeof(SMB1Header)];
    uint16_t FAR *vwv;
    uint8_t setup_count;
    uint16_t subcmd;
    uint16_t param_off;
    uint8_t FAR *params;
    char search_pattern[MAX_PATH];
    char mapped_path[MAX_PATH];
    char local_search[MAX_PATH];
    uint8_t FAR *resp;
    SMB1Header FAR *hdr;
    uint8_t FAR *w, FAR *data_start, FAR *param_ptr, FAR *entries_start, FAR *last_entry;
    uint16_t FAR *resp_vwv, FAR *bcc_ptr;
    int pad, i, count;
    uint16_t param_len, data_len;
    struct find_t fd;

    if (wct < 14) return;
    vwv = (uint16_t FAR*)(req + sizeof(SMB1Header) + 1);
    setup_count = vwv[13] & 0xFF;
    if (setup_count == 0) return;
    
    subcmd = vwv[14];
    
    if (subcmd == 0x0001) {
        param_off = vwv[10];

        if ((uint32_t)param_off + 12 > req_len) return;
        params = req + param_off;
        
        _fmemset(search_pattern, 0, sizeof(search_pattern));
        
        if (req_hdr->flags2 & 0x8000) {
            uint16_t FAR *uname = (uint16_t FAR*)(params + 12);
            i = 0;
            while (uname[i] && i < MAX_PATH - 1) { search_pattern[i] = (char)uname[i]; i++; }
        } else {
            _fstrcpy(search_pattern, (char FAR*)(params + 12));
        }
        
        _fstrcpy(mapped_path, "C:\\");
        for (i = 0; i < map_count; i++) {
            if (maps[i].tid == req_hdr->tid) { _fstrcpy(mapped_path, maps[i].path); break; }
        }
        
        if (mapped_path[_fstrlen(mapped_path)-1] == '\\' && search_pattern[0] == '\\') {
            sprintf(local_search, "%s%s", mapped_path, search_pattern + 1);
        } else {
            sprintf(local_search, "%s\\%s", mapped_path, search_pattern);
        }
        for (i = 0; local_search[i]; i++) if (local_search[i] == '/') local_search[i] = '\\';
        
        server_log("Handled: Directory List -> %s", (char FAR*)local_search);
        
        resp = _fmalloc(65000);
        if (!resp) return;
        _fmemset(resp, 0, 65000);
        hdr = (SMB1Header FAR *)resp;
        _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
        hdr->flags1 |= 0x80;
        
        w = resp + sizeof(SMB1Header);
        *w++ = 10; 
        resp_vwv = (uint16_t FAR*)w; w += 20;
        bcc_ptr = (uint16_t FAR*)w; w += 2;
        data_start = w;
        
        *w++ = 0; 
        param_ptr = w; w += 10;
        pad = (4 - ((w - resp) % 4)) % 4; w += pad;
        entries_start = w;
        
        count = 0;
        last_entry = NULL;
        
        if (_dos_findfirst(local_search, _A_NORMAL|_A_SUBDIR|_A_RDONLY|_A_HIDDEN|_A_SYSTEM, &fd) == 0) {
            do {
                uint32_t FAR *next_off;
                uint32_t FAR *name_len_ptr;
                uint16_t FAR *uname;
                uint32_t smb_attr = 0;
                int epad;
                
                if (w + 512 > resp + 64000) break;
                
                last_entry = w;
                next_off = (uint32_t FAR*)w; w += 4;
                *(uint32_t FAR*)w = 0; w += 4; 
                
                *(uint64_t FAR*)w = 0; w += 8;
                *(uint64_t FAR*)w = 0; w += 8;
                *(uint64_t FAR*)w = 0; w += 8;
                *(uint64_t FAR*)w = 0; w += 8;
                
                *(uint64_t FAR*)w = fd.size; w += 8; 
                *(uint64_t FAR*)w = fd.size; w += 8; 
                
                if (fd.attrib & _A_SUBDIR) smb_attr |= 0x10;
                if (fd.attrib & _A_RDONLY) smb_attr |= 0x01;
                if (fd.attrib & _A_HIDDEN) smb_attr |= 0x02;
                if (fd.attrib & _A_SYSTEM) smb_attr |= 0x04;
                if (smb_attr == 0) smb_attr = 0x80;

                *(uint32_t FAR*)w = smb_attr; w += 4;
                
                name_len_ptr = (uint32_t FAR*)w; w += 4;
                *(uint32_t FAR*)w = 0; w += 4; 
                *w++ = 0; *w++ = 0;        
                _fmemset(w, 0, 24); w += 24; 
                
                uname = (uint16_t FAR*)w;
                i = 0;
                while (fd.name[i]) { uname[i] = (uint16_t)fd.name[i]; i++; }
                *name_len_ptr = (uint32_t)(i * 2);
                w += i * 2;
                
                epad = (4 - ((w - last_entry) % 4)) % 4; w += epad;
                *next_off = (uint32_t)(w - last_entry); 
                
                count++;
            } while (_dos_findnext(&fd) == 0);
        }
        
        if (last_entry) *(uint32_t FAR*)last_entry = 0; 
        
        *(uint16_t FAR*)(param_ptr + 0) = 1234;  
        *(uint16_t FAR*)(param_ptr + 2) = count; 
        *(uint16_t FAR*)(param_ptr + 4) = 1;     
        *(uint16_t FAR*)(param_ptr + 6) = 0;     
        *(uint16_t FAR*)(param_ptr + 8) = 0;     
        
        param_len = 10;
        data_len = (uint16_t)(w - entries_start);
        
        resp_vwv[0] = param_len; resp_vwv[1] = data_len; resp_vwv[2] = 0;
        resp_vwv[3] = param_len; resp_vwv[4] = (uint16_t)(param_ptr - resp); resp_vwv[5] = 0;
        resp_vwv[6] = data_len;  resp_vwv[7] = (uint16_t)(entries_start - resp); resp_vwv[8] = 0;
        resp_vwv[9] = 0; 
        
        *bcc_ptr = (uint16_t)(w - data_start);
        send_packet(client, resp, (uint32_t)(w - resp));
        _ffree(resp);
    } else {
        uint8_t resp[128];
        SMB1Header FAR *hdr;
        _fmemset(resp, 0, sizeof(resp));
        hdr = (SMB1Header FAR *)resp;
        _fmemcpy(hdr, req_hdr, sizeof(SMB1Header));
        hdr->flags1 |= 0x80;
        hdr->status = 0xC00000BB; /* STATUS_NOT_SUPPORTED */
        resp[sizeof(SMB1Header)] = 0;
        *(uint16_t FAR*)(resp + sizeof(SMB1Header) + 1) = 0;
        send_packet(client, resp, sizeof(SMB1Header) + 3);
    }
}

static void process_smb1_payload(SOCKET client, uint8_t FAR *payload, uint32_t len, TidMap FAR *maps, int FAR *map_count) {
    SMB1Header FAR *smb = (SMB1Header FAR*)payload;
    if (_fmemcmp(smb->protocol_id, "\xFFSMB", 4) == 0) {
        switch(smb->cmd) {
            case 0x72: handle_negotiate(client, smb, payload, len); break;
            case 0x73: handle_session_setup(client, smb); break;
            case 0x75: handle_tree_connect(client, smb, maps, map_count); break;
            case 0xA2: handle_nt_create_andx(client, smb, payload); break; /* FIX: Added handle intercept */
            case 0x25: handle_trans(client, smb, payload, len); break;
            case 0x32: handle_trans2(client, smb, payload, len, maps, *map_count); break;
            default: {
                uint8_t resp[128]; 
                SMB1Header FAR *hdr;
                _fmemset(resp, 0, sizeof(resp));
                resp[0] = 0x00; resp[1] = 0; resp[2] = 0; resp[3] = sizeof(SMB1Header) + 3;
                hdr = (SMB1Header FAR *)(resp + 4);
                _fmemcpy(hdr, smb, sizeof(SMB1Header));
                hdr->flags1 |= 0x80; hdr->status = 0;
                resp[4 + sizeof(SMB1Header)] = 0; 
                *(uint16_t FAR*)(resp + 4 + sizeof(SMB1Header) + 1) = 0; 
                send(client, (char FAR*)resp, 4 + sizeof(SMB1Header) + 3, 0);
                break;
            }
        }
    }
}

/* ==========================================================================
   SERVER RUNTIME (Timer Based)
   ========================================================================== */
static void ToggleServer(void) {
    int i;
    unsigned long mode = 1;
    
    if (g_server_running) {
        g_server_running = 0;
        if (g_listen_socket != INVALID_SOCKET) { closesocket(g_listen_socket); g_listen_socket = INVALID_SOCKET; }
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].active) {
                closesocket(g_clients[i].sock);
                g_clients[i].active = 0;
            }
        }
        server_log("Server stopped.");
    } else {
        struct sockaddr_in addr;
        
        g_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (g_listen_socket == INVALID_SOCKET) return;
        
        ioctlsocket(g_listen_socket, FIONBIO, &mode);
        
        _fmemset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(g_port);

        if (bind(g_listen_socket, (struct sockaddr FAR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            server_log("Failed to bind to port %d.", g_port);
            closesocket(g_listen_socket);
            g_listen_socket = INVALID_SOCKET;
            return;
        }

        listen(g_listen_socket, SOMAXCONN);
        server_log("SMB1 Server listening on port %d...", g_port);
        
        for (i = 0; i < MAX_CLIENTS; i++) g_clients[i].active = 0;
        
        g_server_running = 1;
    }
    if (g_hMain && IsWindow(g_hMain)) InvalidateRect(g_hMain, NULL, TRUE);
}

static void HandleNetwork(void) {
    fd_set read_fds;
    struct timeval tv;
    int i, max_fd;
    TidMap maps[10];
    int map_count = 0;
    
    if (!g_server_running) return;

    FD_ZERO(&read_fds);
    FD_SET(g_listen_socket, &read_fds);
    tv.tv_sec = 0; tv.tv_usec = 0;
    
    if (select(0, &read_fds, NULL, NULL, &tv) > 0) {
        SOCKET c = accept(g_listen_socket, NULL, NULL);
        if (c != INVALID_SOCKET) {
            int added = 0;
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (!g_clients[i].active) {
                    unsigned long mode = 1;
                    g_clients[i].sock = c;
                    g_clients[i].active = 1;
                    ioctlsocket(c, FIONBIO, &mode);
                    server_log("Client connected.");
                    added = 1;
                    break;
                }
            }
            if (!added) closesocket(c);
        }
    }

    FD_ZERO(&read_fds);
    max_fd = 0;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active) {
            FD_SET(g_clients[i].sock, &read_fds);
            max_fd = 1;
        }
    }

    if (max_fd && select(0, &read_fds, NULL, NULL, &tv) > 0) {
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].active && FD_ISSET(g_clients[i].sock, &read_fds)) {
                SOCKET client = g_clients[i].sock;
                uint8_t header[4];
                int r = recv(client, (char FAR*)header, 4, 0);
                if (r <= 0) {
                    closesocket(client);
                    g_clients[i].active = 0;
                    server_log("Client disconnected.");
                } else if (header[0] != 0x85) {
                    uint32_t len = ((uint32_t)(header[1] & 0x01) << 16) | ((uint32_t)header[2] << 8) | header[3];
                    if (len > 0 && len <= 65000) {
                        uint8_t FAR *payload = _fmalloc((size_t)len);
                        if (payload) {
                            uint32_t received = 0;
                            unsigned long mode = 0;
                            ioctlsocket(client, FIONBIO, &mode); 
                            while (received < len) {
                                int chunk = (len - received > 30000) ? 30000 : (int)(len - received);
                                int rcvd = recv(client, (char FAR*)(payload + received), chunk, 0);
                                if (rcvd <= 0) break;
                                received += rcvd;
                            }
                            if (received == len) {
                                process_smb1_payload(client, payload, len, maps, &map_count);
                            }
                            mode = 1;
                            ioctlsocket(client, FIONBIO, &mode); 
                            _ffree(payload);
                        }
                    }
                }
            }
        }
    }
}

/* ==========================================================================
   UI PROCEDURES (Fixed Window Size to prevent Win16 crash and tearing)
   ========================================================================== */
static void RefreshShareCombo(HWND hCmb) {
    int i;
    SendMessage(hCmb, CB_RESETCONTENT, 0, 0L);
    for (i = 0; i < g_share_count; i++) SendMessage(hCmb, CB_ADDSTRING, 0, (LPARAM)(char FAR*)g_shares[i].name);
    if (g_share_count > 0) SendMessage(hCmb, CB_SETCURSEL, 0, 0L);
}

long FAR PASCAL MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            int y = 10;
            char host[128];
            char pStr[16];
            
            CreateWindow("STATIC", "Port Number:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL);
            hPort = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 110, y, 100, 20, hwnd, (HMENU)IDE_PORT, g_hInst, NULL);
            sprintf(pStr, "%d", g_port); SetWindowText(hPort, pStr); y += 30;

            CreateWindow("STATIC", "Shares:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL);
            g_hCmbShares = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|CBS_DROPDOWN, 110, y, 120, 100, hwnd, (HMENU)IDC_CMB_SHARES, g_hInst, NULL);
            g_hSharePath = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL, 235, y, 150, 20, hwnd, (HMENU)IDE_SHAREPATH, g_hInst, NULL);
            CreateWindow("BUTTON", "Add", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 395, y, 40, 20, hwnd, (HMENU)IDB_ADD_SHARE, g_hInst, NULL);
            CreateWindow("BUTTON", "-", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 440, y, 20, 20, hwnd, (HMENU)IDB_DEL_SHARE, g_hInst, NULL);
            
            RefreshShareCombo(g_hCmbShares);
            if (g_share_count > 0) SetWindowText(g_hSharePath, g_shares[0].path);
            y += 30;

            CreateWindow("STATIC", "Bound IP Addresses:", WS_CHILD|WS_VISIBLE, 10, y, 150, 20, hwnd, NULL, g_hInst, NULL); y += 20;
            hIPs = CreateWindow("LISTBOX", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL, 10, y, 450, 60, hwnd, (HMENU)IDL_IPS, g_hInst, NULL); y += 70;

            if (gethostname(host, sizeof(host)) == 0) {
                struct hostent FAR *he = gethostbyname(host);
                if (he) {
                    int i = 0;
                    while (he->h_addr_list[i]) {
                        struct in_addr FAR *addr = (struct in_addr FAR *)he->h_addr_list[i];
                        SendMessage(hIPs, LB_ADDSTRING, 0, (LPARAM)(char FAR*)inet_ntoa(*addr));
                        i++;
                    }
                }
            }

            CreateWindow("STATIC", "Connection Log:", WS_CHILD|WS_VISIBLE, 10, y, 150, 20, hwnd, NULL, g_hInst, NULL); y += 20;
            g_hLogEdit = CreateWindow("EDIT", g_log_buffer, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY, 10, y, 450, 120, hwnd, (HMENU)IDE_LOG, g_hInst, NULL); y += 130;
            SendMessage(g_hLogEdit, EM_LINESCROLL, 0, SendMessage(g_hLogEdit, EM_GETLINECOUNT, 0, 0L));

            hToggle = CreateWindow("BUTTON", "Start / Stop Server", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 10, y, 450, 30, hwnd, (HMENU)IDB_TOGGLE_SRV, g_hInst, NULL);
            
            SetTimer(hwnd, TIMER_NETWORK, 50, NULL);
            return 0;
        }
        case WM_TIMER:
            if (wp == TIMER_NETWORK) HandleNetwork();
            return 0;
            
        case WM_COMMAND: {
            uint16_t id = LOWORD(wp);
            uint16_t code = HIWORD(wp);
            
            if (id == IDC_CMB_SHARES && code == CBN_SELCHANGE) {
                int idx = (int)SendMessage(g_hCmbShares, CB_GETCURSEL, 0, 0L);
                if (idx >= 0 && idx < g_share_count) {
                    SetWindowText(g_hSharePath, g_shares[idx].path);
                }
            } else if (id == IDB_ADD_SHARE) {
                char name[64], path[MAX_PATH];
                GetWindowText(g_hCmbShares, name, sizeof(name));
                GetWindowText(g_hSharePath, path, sizeof(path));
                if (name[0] && path[0]) {
                    int found = -1, i;
                    for (i = 0; i < g_share_count; i++) {
                        if (_fstricmp(g_shares[i].name, name) == 0) { found = i; break; }
                    }
                    if (found != -1) {
                        _fstrcpy(g_shares[found].path, path);
                    } else if (g_share_count < MAX_SHARES) {
                        _fstrcpy(g_shares[g_share_count].name, name);
                        _fstrcpy(g_shares[g_share_count].path, path);
                        g_share_count++;
                    }
                    RefreshShareCombo(g_hCmbShares);
                    save_config();
                    server_log("Share updated: %s -> %s", (char FAR*)name, (char FAR*)path);
                }
            } else if (id == IDB_DEL_SHARE) {
                char name[64];
                int found = -1, i;
                GetWindowText(g_hCmbShares, name, sizeof(name));
                for (i = 0; i < g_share_count; i++) {
                    if (_fstricmp(g_shares[i].name, name) == 0) { found = i; break; }
                }
                if (found != -1 && g_share_count > 1) {
                    for (i = found; i < g_share_count - 1; i++) g_shares[i] = g_shares[i+1];
                    g_share_count--;
                    RefreshShareCombo(g_hCmbShares);
                    SetWindowText(g_hSharePath, g_shares[0].path);
                    save_config();
                    server_log("Share deleted: %s", (char FAR*)name);
                }
            } else if (id == IDB_TOGGLE_SRV) {
                char pStr[16]; 
                int new_port;
                GetWindowText(hPort, pStr, sizeof(pStr)); 
                new_port = atoi(pStr);
                if (new_port > 0) g_port = new_port;
                save_config();
                ToggleServer();
                SetWindowText(hToggle, g_server_running ? "Running (Click to Stop)" : "Stopped (Click to Start)");
            }
            break;
        }
        case WM_DESTROY: 
            KillTimer(hwnd, TIMER_NETWORK);
            if (g_server_running) ToggleServer(); 
            PostQuitMessage(0); 
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc;
    MSG msg;
    WSADATA wsa;
    
    WSAStartup(0x0101, &wsa);
    g_hInst = hInst;
    load_config();
    
    if (!hPrev) {
        _fmemset(&wc, 0, sizeof(WNDCLASS));
        wc.style = 0; /* Remove CS_HREDRAW | CS_VREDRAW for fixed-size dialog */
        wc.lpfnWndProc = MainWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "Smb1dClass";
        if (!RegisterClass(&wc)) return FALSE;
    }
    
    /* Using standard dialog fixed style completely fixes resizing crash/tearing */
    g_hMain = CreateWindow("Smb1dClass", "Win16 SMB1 Server", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 490, 400, 
        NULL, NULL, hInst, NULL);
        
    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);
    
    ToggleServer();
    SetWindowText(hToggle, g_server_running ? "Running (Click to Stop)" : "Stopped (Click to Start)");
    
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg); 
        DispatchMessage(&msg);
    }
    
    WSACleanup();
    return msg.wParam;
}