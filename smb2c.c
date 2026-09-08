/* 
 * Dual-Pane SMB1/SMB2 Client (Win16 / Open Watcom Faithful Port)
 * - Restored Connection Profile Editor with SMB Version Dropdown
 * - Active SMB Version Identification Probe on "Test" Button
 * - Strict C90 Compliant Memory Handling & Scope Declarations
 *
 * COMPILATION:
 *   wcl -l=windows -ml -Os -s -fe=smb2c.exe smb2c.c winsock.lib
 *
 * ============================================================================
 * PUBLIC DOMAIN DEDICATION:
 *
 * This software is released into the public domain. It is not fit for any 
 * purpose. Use entirely at your own risk.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ============================================================================
 */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dos.h>
#include <malloc.h>
#include <direct.h>

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned long      uint32_t;
typedef unsigned __int64   uint64_t;

#define MAX_SMB_PATH        260
#define MAX_ITEMS           200
#define MAX_SHARES          20
#define SMB_BUFFER_SIZE     65000

#define PANE_LOCAL          0
#define PANE_REMOTE         1
#define CONN_NONE           0
#define CONN_SMB            1

#define PROTO_AUTO          0
#define PROTO_SMB2          1
#define PROTO_SMB1          2

#define ID_LIST_LOCAL       1001
#define ID_LIST_REMOTE      1002
#define ID_STATUSBAR        1003
#define ID_COMBO_CONN       1004
#define ID_BTN_CONNECT      1005
#define ID_BTN_EDIT_CONN    1006

#define ID_BTN_COPY         1101
#define ID_BTN_MOVE         1102
#define ID_BTN_RENAME       1103
#define ID_BTN_DELETE       1104
#define ID_BTN_MKDIR        1105

#define IDE_NAME            2001
#define IDE_SERVER          2002
#define IDE_PORT            2010
#define IDE_SHARE           2003
#define IDE_USER            2004
#define IDE_PASS            2005
#define IDB_SAVE            2006
#define IDB_CANCEL          2007
#define IDB_ADD             2008
#define IDB_DELETE          2009
#define IDC_CMB_PROTO       2012
#define IDB_TEST            2013

#define IDB_RENAME_OK       3001
#define IDE_RENAME_NEW      3002
#define IDE_MKDIR_NAME      3003
#define IDB_MKDIR_OK        3004

#define SMB2_NEGOTIATE      0x0000
#define SMB2_SESSION_SETUP  0x0001
#define SMB2_TREE_CONNECT   0x0003
#define SMB2_TREE_DISCONNECT 0x0004
#define SMB2_CREATE         0x0005
#define SMB2_CLOSE          0x0006
#define SMB2_READ           0x0008
#define SMB2_WRITE          0x0009
#define SMB2_QUERY_DIRECTORY 0x000E
#define SMB2_SET_INFO       0x0011

#define STATUS_SUCCESS              0x00000000
#define STATUS_MORE_PROCESSING      0xC0000016

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

typedef struct {
    uint8_t  protocol_id[4];
    uint16_t structure_size;
    uint16_t credit_charge;
    uint32_t status;
    uint16_t command;
    uint16_t credit_request;
    uint32_t flags;
    uint32_t next_command;
    uint64_t message_id;
    uint32_t process_id;
    uint32_t tree_id;
    uint64_t session_id;
    uint8_t  signature[16];
} SMB2Header;

typedef struct {
    uint16_t structure_size;
    uint16_t dialect_count;
    uint16_t security_mode;
    uint16_t reserved;
    uint32_t capabilities;
    uint8_t  client_guid[16];
    uint32_t negotiate_context_offset;
    uint16_t negotiate_context_count;
    uint16_t reserved2;
} SMB2NegotiateReq;

typedef struct {
    uint16_t structure_size;
    uint16_t security_mode;
    uint16_t dialect_revision;
    uint16_t reserved;
    uint8_t  server_guid[16];
    uint32_t capabilities;
    uint32_t max_transact_size;
    uint32_t max_read_size;
    uint32_t max_write_size;
    uint64_t system_time;
    uint64_t server_start_time;
    uint16_t security_buffer_offset;
    uint16_t security_buffer_length;
    uint32_t negotiate_context_offset;
} SMB2NegotiateResp;

typedef struct {
    uint16_t structure_size;
    uint8_t  flags;
    uint8_t  security_mode;
    uint32_t capabilities;
    uint32_t channel;
    uint16_t security_buffer_offset;
    uint16_t security_buffer_length;
    uint64_t previous_session_id;
} SMB2SessionSetupReq;

typedef struct {
    uint16_t structure_size;
    uint16_t reserved;
    uint16_t path_offset;
    uint16_t path_length;
} SMB2TreeConnectReq;

typedef struct {
    uint16_t structure_size;
    uint8_t  security_flags;
    uint8_t  requested_oplock_level;
    uint32_t impersonation_level;
    uint64_t smb_create_flags;
    uint64_t reserved;
    uint32_t desired_access;
    uint32_t file_attributes;
    uint32_t share_access;
    uint32_t create_disposition;
    uint32_t create_options;
    uint16_t name_offset;
    uint16_t name_length;
    uint32_t create_contexts_offset;
    uint32_t create_contexts_length;
} SMB2CreateReq;

typedef struct {
    uint16_t structure_size;
    uint8_t  oplock_level;
    uint8_t  flag;
    uint32_t create_action;
    uint64_t creation_time;
    uint64_t last_access_time;
    uint64_t last_write_time;
    uint64_t change_time;
    uint64_t allocation_size;
    uint64_t end_of_file;
    uint32_t file_attributes;
    uint32_t reserved2;
    uint64_t file_id_persistent;
    uint64_t file_id_volatile;
} SMB2CreateResp;

typedef struct {
    uint16_t structure_size;
    uint8_t  file_information_class;
    uint8_t  flags;
    uint32_t file_index;
    uint64_t file_id_persistent;
    uint64_t file_id_volatile;
    uint16_t name_offset;
    uint16_t name_length;
    uint32_t output_buffer_length;
} SMB2QueryDirReqFixed;

typedef struct {
    uint16_t structure_size;
    uint16_t flags;
    uint32_t reserved;
    uint64_t file_id_persistent;
    uint64_t file_id_volatile;
} SMB2CloseReq;

typedef struct {
    uint16_t structure_size;
    uint8_t  padding;
    uint8_t  flags;
    uint32_t length;
    uint64_t offset;
    uint64_t file_id_persistent;
    uint64_t file_id_volatile;
    uint32_t minimum_count;
    uint32_t channel;
    uint32_t remaining_bytes;
    uint16_t read_channel_info_offset;
    uint16_t read_channel_info_length;
} SMB2ReadReq;

typedef struct {
    uint16_t structure_size;
    uint16_t data_offset;
    uint32_t data_length;
    uint32_t data_remaining;
    uint32_t reserved2;
} SMB2ReadResp;

typedef struct {
    uint16_t structure_size;
    uint16_t data_offset;
    uint32_t length;
    uint64_t offset;
    uint64_t file_id_persistent;
    uint64_t file_id_volatile;
    uint32_t channel;
    uint32_t remaining_bytes;
    uint16_t write_channel_info_offset;
    uint16_t write_channel_info_length;
    uint32_t flags;
} SMB2WriteReq;

typedef struct {
    uint16_t structure_size;
    uint8_t  info_type;      
    uint8_t  file_info_class;
    uint32_t buffer_length;
    uint16_t buffer_offset;  
    uint16_t reserved;
    uint32_t additional_information;
    uint64_t file_id_persistent;
    uint64_t file_id_volatile;
} SMB2SetInfoReq;
#pragma pack()

typedef struct {
    char path[MAX_SMB_PATH];
    int  is_dir;
} DirectoryItem;

typedef struct {
    char name[64];
    char server[128];
    char port[16];
    char share[128];
    char user[64];
    char pass[64];
    int  proto_pref;
    char shares_hist[512]; 
} ConnectionProfile;

typedef struct {
    HWND hMain, hComboConn, hBtnEdit, hBtnConnect;
    HWND hBtnCopy, hBtnMove, hBtnRename, hBtnDelete, hBtnMkDir;
    HWND hLocalList, hRemoteList, hStatus;
    HWND hLblLocal, hLblRemote;
    
    ConnectionProfile FAR *connections;
    int conn_capacity;
    int conn_count, selected_conn_idx;
    
    DirectoryItem FAR *remote_items;
    DirectoryItem FAR *local_items;
    int remote_count, local_count;
    
    char local_base[MAX_SMB_PATH];
    char remote_base[MAX_SMB_PATH];
    
    SOCKET sconn;
    int conn_type;
    int current_proto;
    int active_pane;
    char pending_server[128];
    
    uint16_t tid, uid, mid_counter;
    
    uint64_t smb2_session_id;
    uint32_t smb2_tree_id;
    uint64_t smb2_message_id;
    uint8_t  smb2_server_guid[16];
} AppContext;

static AppContext g_app;
static HINSTANCE g_hInst;
static char g_ini_path[MAX_SMB_PATH];
static char g_ren_base[MAX_SMB_PATH];
static char g_ren_item[MAX_SMB_PATH];
static char g_conn_log[4096] = "Win16 Client Started.\r\n";

/* ==========================================================================
   FORWARD DECLARATIONS
   ========================================================================== */
static void add_log(const char FAR *fmt, ...);
static void trim_str(char FAR *str);
static void normalize_path(char FAR *path, int is_ftp);
static size_t utf8_to_utf16le(const char FAR *src, uint8_t FAR *dst, size_t dst_max);

static void load_config(void);
static void save_config(void);
static void refresh_combo(void);
static void disconnect_all(void);
static int connect_with_timeout(SOCKET sock, struct sockaddr_in FAR *addr);
static int resolve_and_connect(const char FAR *server, int port);

static int smb_send_packet(const void FAR *data, uint32_t len);
static int smb_recv_packet(uint8_t FAR *buffer, uint32_t max_len, uint32_t FAR *out_len);
static SMB1Header FAR * smb_build_header(uint8_t FAR *packet, uint8_t cmd);
static void smb2_init_header(SMB2Header FAR *hdr, uint16_t cmd);

static int smb1_negotiate(void);
static int smb1_session(const char FAR *user, const char FAR *pass);
static int smb1_tree_connect(const char FAR *server, const char FAR *share);
static void smb1_list_directory(void);

static int smb2_negotiate(uint16_t FAR *out_dialect);
static int smb2_session_setup(const char FAR *user, const char FAR *pass);
static int smb2_tree_connect(const char FAR *server, const char FAR *share);
static int smb2_tree_disconnect(void);
static void smb2_list_directory(void);

static int smb2_ipc_enum_shares(const char FAR *server, char shares[MAX_SHARES][64], int max_shares);
static int enum_shares_ipc(char shares[MAX_SHARES][64], int max_shares);
static int enumerate_shares(char shares[MAX_SHARES][64], int max_shares, int is_smb2_capable);

static void list_remote(void);
static void list_local(void);

/* ==========================================================================
   UTILITY & LOGGING
   ========================================================================== */
static void add_log(const char FAR *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    
    if (_fstrlen(g_conn_log) + _fstrlen(buf) + 4 >= sizeof(g_conn_log)) {
        _fmemmove(g_conn_log, g_conn_log + 1024, sizeof(g_conn_log) - 1024);
        g_conn_log[sizeof(g_conn_log) - 1] = '\0';
    }
    _fstrcat(g_conn_log, buf);
    _fstrcat(g_conn_log, "\r\n");

    if (g_app.hStatus && IsWindow(g_app.hStatus)) {
        SendMessage(g_app.hStatus, WM_SETTEXT, 0, (LPARAM)(char FAR*)g_conn_log);
        SendMessage(g_app.hStatus, EM_LINESCROLL, 0, SendMessage(g_app.hStatus, EM_GETLINECOUNT, 0, 0L));
    }
}

static void trim_str(char FAR *str) {
    char FAR *p = str; 
    int l = _fstrlen(p);
    while (l > 0 && (p[l-1] == ' ' || p[l-1] == '\r' || p[l-1] == '\n' || p[l-1] == '\t')) p[--l] = 0;
    while (*p && (*p == ' ' || *p == '\t')) { p++; l--; }
    _fmemmove(str, p, l + 1);
}

static void normalize_path(char FAR *path, int is_ftp) {
    char target = is_ftp ? '/' : '\\';
    char wrong  = is_ftp ? '\\' : '/';
    char FAR *p;
    for (p = path; *p; ++p) { if (*p == wrong) *p = target; }
}

static void init_ini_path(void) {
    char FAR *ext;
    GetModuleFileName(NULL, g_ini_path, MAX_SMB_PATH);
    ext = _fstrrchr(g_ini_path, '.');
    if (ext) _fstrcpy(ext, ".INI"); else _fstrcat(g_ini_path, ".INI");
}

static size_t utf8_to_utf16le(const char FAR *src, uint8_t FAR *dst, size_t dst_max) {
    size_t di = 0, si = 0;
    while (src[si] && di + 2 <= dst_max) { dst[di++] = (uint8_t)src[si]; dst[di++] = 0; si++; }
    return di;
}

/* ==========================================================================
   NETWORK TIMEOUT UTILITY
   ========================================================================== */
static int connect_with_timeout(SOCKET sock, struct sockaddr_in FAR *addr) {
    unsigned long mode = 1;
    fd_set fdset;
    struct timeval tv;
    
    ioctlsocket(sock, FIONBIO, &mode);
    connect(sock, (struct sockaddr FAR*)addr, sizeof(struct sockaddr_in));
    
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);
    tv.tv_sec = 0; tv.tv_usec = 500000; 
    
    if (select(0, NULL, &fdset, NULL, &tv) > 0) {
        mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);
        return 1;
    }
    return 0;
}

static int resolve_and_connect(const char FAR *server, int port) {
    unsigned long ip;
    struct sockaddr_in addr;
    unsigned long mode;
    fd_set fdset;
    struct timeval tv;
    
    _fmemset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    
    ip = inet_addr(server);
    if (ip != INADDR_NONE) {
        addr.sin_addr.s_addr = ip;
    } else {
        struct hostent FAR *he = gethostbyname(server);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            return 0;
        }
        _fmemcpy(&addr.sin_addr.s_addr, he->h_addr_list[0], 4);
    }
    
    g_app.sconn = socket(AF_INET, SOCK_STREAM, 0);
    if (g_app.sconn == INVALID_SOCKET) return 0;
    
    mode = 1;
    ioctlsocket(g_app.sconn, FIONBIO, &mode);
    connect(g_app.sconn, (struct sockaddr FAR*)&addr, sizeof(struct sockaddr_in));
    
    FD_ZERO(&fdset);
    FD_SET(g_app.sconn, &fdset);
    tv.tv_sec = 0; tv.tv_usec = 500000; 
    
    if (select(0, NULL, &fdset, NULL, &tv) > 0) {
        mode = 0;
        ioctlsocket(g_app.sconn, FIONBIO, &mode);
        return 1;
    }
    
    closesocket(g_app.sconn);
    g_app.sconn = INVALID_SOCKET;
    return 0;
}

/* ==========================================================================
   CONFIG LOAD/SAVE
   ========================================================================== */
static void load_config(void) {
    int i;
    init_ini_path();
    g_app.conn_count = GetPrivateProfileInt("Connections", "Count", 0, g_ini_path);
    g_app.selected_conn_idx = GetPrivateProfileInt("Connections", "LastSelected", 0, g_ini_path);
    
    if (g_app.conn_count <= 0) {
        g_app.conn_count = 1; 
        _fstrcpy(g_app.connections[0].name, "Default Connection"); 
        _fstrcpy(g_app.connections[0].server, "192.168.1.100");
        _fstrcpy(g_app.connections[0].port, ""); 
        _fstrcpy(g_app.connections[0].share, "shared");
        _fstrcpy(g_app.connections[0].user, ""); 
        _fstrcpy(g_app.connections[0].pass, "");
        g_app.connections[0].proto_pref = PROTO_AUTO;
        _fstrcpy(g_app.connections[0].shares_hist, ""); 
        g_app.selected_conn_idx = 0;
    } else {
        for (i = 0; i < g_app.conn_count; i++) {
            char key[32];
            sprintf(key, "Name%d", i); GetPrivateProfileString("Connections", key, "", g_app.connections[i].name, 64, g_ini_path);
            sprintf(key, "Server%d", i); GetPrivateProfileString("Connections", key, "", g_app.connections[i].server, 128, g_ini_path);
            sprintf(key, "Port%d", i); GetPrivateProfileString("Connections", key, "", g_app.connections[i].port, 16, g_ini_path);
            sprintf(key, "Share%d", i); GetPrivateProfileString("Connections", key, "", g_app.connections[i].share, 128, g_ini_path);
            sprintf(key, "User%d", i); GetPrivateProfileString("Connections", key, "", g_app.connections[i].user, 64, g_ini_path);
            sprintf(key, "Pass%d", i); GetPrivateProfileString("Connections", key, "", g_app.connections[i].pass, 64, g_ini_path);
            sprintf(key, "Proto%d", i); g_app.connections[i].proto_pref = GetPrivateProfileInt("Connections", key, PROTO_AUTO, g_ini_path);
            sprintf(key, "SharesHist%d", i); GetPrivateProfileString("Connections", key, "", g_app.connections[i].shares_hist, 512, g_ini_path);
        }
    }
    if (g_app.selected_conn_idx < 0 || g_app.selected_conn_idx >= g_app.conn_count) g_app.selected_conn_idx = 0;
}

static void save_config(void) {
    char val[32];
    int i;
    WritePrivateProfileString("Connections", NULL, NULL, g_ini_path);
    sprintf(val, "%d", g_app.conn_count); WritePrivateProfileString("Connections", "Count", val, g_ini_path);
    sprintf(val, "%d", g_app.selected_conn_idx); WritePrivateProfileString("Connections", "LastSelected", val, g_ini_path);
    for (i = 0; i < g_app.conn_count; i++) {
        char key[32];
        sprintf(key, "Name%d", i); WritePrivateProfileString("Connections", key, g_app.connections[i].name, g_ini_path);
        sprintf(key, "Server%d", i); WritePrivateProfileString("Connections", key, g_app.connections[i].server, g_ini_path);
        sprintf(key, "Port%d", i); WritePrivateProfileString("Connections", key, g_app.connections[i].port, g_ini_path);
        sprintf(key, "Share%d", i); WritePrivateProfileString("Connections", key, g_app.connections[i].share, g_ini_path);
        sprintf(key, "User%d", i); WritePrivateProfileString("Connections", key, g_app.connections[i].user, g_ini_path);
        sprintf(key, "Pass%d", i); WritePrivateProfileString("Connections", key, g_app.connections[i].pass, g_ini_path);
        sprintf(key, "Proto%d", i); sprintf(val, "%d", g_app.connections[i].proto_pref); WritePrivateProfileString("Connections", key, val, g_ini_path);
        sprintf(key, "SharesHist%d", i); WritePrivateProfileString("Connections", key, g_app.connections[i].shares_hist, g_ini_path);
    }
}

static void refresh_combo(void) {
    int i;
    SendMessage(g_app.hComboConn, CB_RESETCONTENT, 0, 0L);
    for (i = 0; i < g_app.conn_count; i++) SendMessage(g_app.hComboConn, CB_ADDSTRING, 0, (LPARAM)(char FAR*)g_app.connections[i].name);
    SendMessage(g_app.hComboConn, CB_SETCURSEL, g_app.selected_conn_idx, 0L);
}

static void disconnect_all(void) {
    if (g_app.sconn && g_app.sconn != INVALID_SOCKET) { closesocket(g_app.sconn); g_app.sconn = INVALID_SOCKET; }
    g_app.conn_type = CONN_NONE; g_app.current_proto = PROTO_AUTO;
    g_app.smb2_session_id = 0; g_app.smb2_tree_id = 0; g_app.smb2_message_id = 0; 
    g_app.tid = 0; g_app.uid = 0;
}

/* ==========================================================================
   SMB TRANSPORT
   ========================================================================== */
static int smb_send_packet(const void FAR *data, uint32_t len) {
    uint8_t net_len[4];
    const uint8_t FAR *buf = (const uint8_t FAR *)data; 
    uint32_t rem = len;
    
    net_len[0] = 0x00; net_len[1] = (uint8_t)((len >> 16) & 0xFF); net_len[2] = (uint8_t)((len >> 8) & 0xFF); net_len[3] = (uint8_t)(len & 0xFF);
    if (send(g_app.sconn, (const char FAR*)net_len, 4, 0) != 4) return 0;
    
    while (rem > 0) { 
        int chunk = (rem > 30000) ? 30000 : (int)rem;
        int sent = send(g_app.sconn, (const char FAR*)buf, chunk, 0); 
        if (sent <= 0) return 0; 
        buf += sent; rem -= sent; 
    }
    return 1;
}

static int smb_recv_packet(uint8_t FAR *buffer, uint32_t max_len, uint32_t FAR *out_len) {
    uint8_t header[4];
    while (1) {
        uint32_t expected, received = 0;
        if (recv(g_app.sconn, (char FAR*)header, 4, 0) != 4) return 0;
        expected = ((uint32_t)(header[1] & 0x01) << 16) | ((uint32_t)header[2] << 8) | header[3];
        if (header[0] == 0x85 && expected == 0) continue; 
        if (expected > max_len) expected = max_len;
        
        while (received < expected) { 
            int chunk = (expected - received > 30000) ? 30000 : (int)(expected - received);
            int rcvd = recv(g_app.sconn, (char FAR*)(buffer + received), chunk, 0); 
            if (rcvd <= 0) return 0; 
            received += rcvd; 
        }
        *out_len = received; return 1;
    }
}

static SMB1Header FAR * smb_build_header(uint8_t FAR *packet, uint8_t cmd) {
    SMB1Header FAR *hdr = (SMB1Header FAR *)packet;
    _fmemcpy(hdr->protocol_id, "\xFFSMB", 4); hdr->cmd = cmd;
    hdr->pid_low = 1; hdr->mid = g_app.mid_counter++;
    hdr->flags1 = 0x80; hdr->flags2 = 0x07C7; hdr->uid = g_app.uid; hdr->tid = g_app.tid;
    _fmemset(hdr->signature, 0, 8); return hdr;
}

static void smb2_init_header(SMB2Header FAR *hdr, uint16_t cmd) {
    _fmemcpy(hdr->protocol_id, "\xFE\x53\x4D\x42", 4); hdr->structure_size = 64;
    hdr->credit_charge = (cmd == SMB2_NEGOTIATE) ? 0 : 1; hdr->status = 0; hdr->command = cmd;
    hdr->credit_request = 32; hdr->flags = 0; hdr->next_command = 0;
    hdr->message_id = g_app.smb2_message_id++; hdr->process_id = 0; 
    hdr->tree_id = g_app.smb2_tree_id; hdr->session_id = g_app.smb2_session_id;
    _fmemset(hdr->signature, 0, 16);
}

/* ==========================================================================
   SMB1 CORE
   ========================================================================== */
static int smb1_negotiate(void) {
    uint8_t FAR *pkt = _fmalloc(1024);
    uint8_t FAR *w;
    uint8_t FAR *bcc_ptr;
    uint32_t rlen;
    SMB1Header FAR *hdr;
    int i;
    const char *dialects[] = {
        "PC NETWORK PROGRAM 1.0", "MICROSOFT NETWORKS 3.0", "DOS LM1.2X002",
        "DOS LANMAN2.1", "Windows for Workgroups 3.1a", "NT LM 0.12"
    };
    if (!pkt) return 0;
    _fmemset(pkt, 0, 1024);
    smb_build_header(pkt, 0x72);
    w = pkt + sizeof(SMB1Header);
    *w++ = 0; bcc_ptr = w; w += 2;
    for (i = 0; i < 6; i++) {
        *w++ = 0x02;
        _fstrcpy((char FAR*)w, dialects[i]);
        w += _fstrlen(dialects[i]) + 1;
    }
    *(uint16_t FAR*)bcc_ptr = (uint16_t)((w - bcc_ptr) - 2);
    if (!smb_send_packet(pkt, (uint32_t)(w - pkt))) { _ffree(pkt); return 0; }
    if (!smb_recv_packet(pkt, 1024, &rlen)) { _ffree(pkt); return 0; }
    hdr = (SMB1Header FAR*)pkt;
    if (_fmemcmp(hdr->protocol_id, "\xFFSMB", 4) != 0 || hdr->status != 0) { _ffree(pkt); return 0; }
    _ffree(pkt);
    return 1;
}

static int smb1_session(const char FAR *user, const char FAR *pass) {
    uint8_t FAR *pkt = _fmalloc(1024);
    uint8_t FAR *w;
    uint8_t FAR *bcc_ptr;
    uint8_t FAR *data_start;
    uint16_t pass_len;
    uint32_t rlen;
    SMB1Header FAR *hdr;
    char domain[64];
    char uname[64];
    const char FAR *slash;
    
    if (!pkt) return 0;
    
    _fstrcpy(domain, "WORKGROUP"); _fstrcpy(uname, ""); 
    slash = _fstrchr(user, '\\');
    if (slash) { 
        size_t dlen = slash - user; 
        if (dlen < sizeof(domain)) { _fstrncpy(domain, user, dlen); domain[dlen] = '\0'; } 
        _fstrcpy(uname, slash + 1); 
    } else { _fstrcpy(uname, user); }

    _fmemset(pkt, 0, 1024);
    smb_build_header(pkt, 0x73);
    w = pkt + sizeof(SMB1Header);
    *w++ = 10; *w++ = 0xFF; *w++ = 0;
    *(uint16_t FAR*)w = 0; w += 2; *(uint16_t FAR*)w = 65535U; w += 2;
    *(uint16_t FAR*)w = 2; w += 2; *(uint16_t FAR*)w = 1; w += 2; *(uint32_t FAR*)w = 0; w += 4;
    
    pass_len = _fstrlen(pass);
    *(uint16_t FAR*)w = pass_len ? pass_len + 1 : 1; w += 2; *(uint32_t FAR*)w = 0; w += 4;
    bcc_ptr = w; w += 2; data_start = w;
    
    if (pass_len > 0) { _fstrcpy((char FAR*)w, pass); w += pass_len + 1; } else { *w++ = 0; }
    _fstrcpy((char FAR*)w, uname); w += _fstrlen(uname) + 1;
    _fstrcpy((char FAR*)w, domain); w += _fstrlen(domain) + 1;
    _fstrcpy((char FAR*)w, "Windows 5.1"); w += 12;
    _fstrcpy((char FAR*)w, "LAN Manager"); w += 12;
    
    *(uint16_t FAR*)bcc_ptr = (uint16_t)(w - data_start);
    if (!smb_send_packet(pkt, (uint32_t)(w - pkt))) { _ffree(pkt); return 0; }
    if (!smb_recv_packet(pkt, 1024, &rlen)) { _ffree(pkt); return 0; }
    hdr = (SMB1Header FAR*)pkt;
    g_app.uid = hdr->uid;
    if (hdr->status != 0) { _ffree(pkt); return 0; }
    _ffree(pkt);
    return 1;
}

static int smb1_tree_connect(const char FAR *server, const char FAR *share) {
    uint8_t FAR *pkt = _fmalloc(1024);
    SMB1Header FAR *hdr;
    uint8_t FAR *w;
    char full_share[MAX_SMB_PATH];
    const char FAR *clean_share;
    uint8_t path_utf16[MAX_SMB_PATH * 2];
    uint32_t path_len_utf16;
    uint16_t byte_count;
    uint32_t rlen;
    
    if (!pkt) return 0;
    _fmemset(pkt, 0, 1024);
    hdr = smb_build_header(pkt, 0x75);
    hdr->flags2 |= 0x8000; hdr->tid = 0xFFFF;
    
    w = pkt + sizeof(SMB1Header);
    *w++ = 4; *w++ = 0xFF; *w++ = 0;
    *(uint16_t FAR*)w = 0; w += 2; *(uint16_t FAR*)w = 0; w += 2; *(uint16_t FAR*)w = 1; w += 2;
    
    clean_share = (share[0] == '\\' || share[0] == '/') ? share + 1 : share;
    sprintf(full_share, "\\\\%s\\%s", server, clean_share);
    path_len_utf16 = utf8_to_utf16le(full_share, path_utf16, sizeof(path_utf16));
    
    byte_count = (uint16_t)(1 + path_len_utf16 + 2 + 6);
    *(uint16_t FAR*)w = byte_count; w += 2;
    *w++ = 0;
    _fmemcpy(w, path_utf16, path_len_utf16); w += path_len_utf16;
    *w++ = 0; *w++ = 0;
    _fstrcpy((char FAR*)w, "?????"); w += 6;
    
    if (!smb_send_packet(pkt, (uint32_t)(w - pkt))) { _ffree(pkt); return 0; }
    if (!smb_recv_packet(pkt, 1024, &rlen)) { _ffree(pkt); return 0; }
    
    hdr = (SMB1Header FAR*)pkt;
    g_app.tid = hdr->tid;
    if (hdr->status != 0) { _ffree(pkt); return 0; }
    _ffree(pkt);
    return 1;
}

static void smb1_list_directory(void) {
    uint8_t FAR *packet = _fmalloc(SMB_BUFFER_SIZE);
    SMB1Header FAR *hdr;
    uint8_t FAR *vwv;
    uint8_t FAR *bcc_ptr;
    uint8_t FAR *data_start;
    uint8_t FAR *param_ptr;
    uint8_t FAR *p;
    char search_pattern[MAX_SMB_PATH];
    uint8_t pattern_utf16[MAX_SMB_PATH * 2];
    uint32_t pat_len_utf16;
    uint16_t param_count;
    uint8_t FAR *v;
    uint32_t rlen;
    uint8_t wct;
    uint16_t FAR *vwv_words;
    uint16_t param_off, data_off, data_cnt;

    if (!packet) return;
    _fmemset(packet, 0, SMB_BUFFER_SIZE);
    hdr = smb_build_header(packet, 0x32);
    hdr->tid = g_app.tid; hdr->flags2 |= 0x8000;
    
    vwv = packet + sizeof(SMB1Header);
    *vwv++ = 15;
    bcc_ptr = vwv + 15 * 2;
    data_start = bcc_ptr + 2;
    param_ptr = data_start;
    if ((param_ptr - packet) % 2 != 0) param_ptr++;
    
    p = param_ptr;
    *(uint16_t FAR*)p = 0x0016; p += 2;
    *(uint16_t FAR*)p = 256; p += 2;
    *(uint16_t FAR*)p = 0x0002; p += 2;
    *(uint16_t FAR*)p = 0x0104; p += 2;
    *(uint32_t FAR*)p = 0; p += 4;
    
    if (g_app.remote_base[0] == '\0' || _fstrcmp(g_app.remote_base, "\\") == 0) {
        sprintf(search_pattern, "\\*");
    } else {
        sprintf(search_pattern, "%s\\*", g_app.remote_base);
    }
    pat_len_utf16 = utf8_to_utf16le(search_pattern, pattern_utf16, sizeof(pattern_utf16));
    _fmemcpy(p, pattern_utf16, pat_len_utf16); p += pat_len_utf16;
    *p++ = 0; *p++ = 0;
    
    param_count = (uint16_t)(p - param_ptr);
    v = vwv;
    *(uint16_t FAR*)v = param_count; v += 2;
    *(uint16_t FAR*)v = 0; v += 2;
    *(uint16_t FAR*)v = 1024; v += 2;
    *(uint16_t FAR*)v = 32768; v += 2;
    *v++ = 1; *v++ = 0;
    *(uint16_t FAR*)v = 0; v += 2;
    *(uint32_t FAR*)v = 0; v += 4;
    *(uint16_t FAR*)v = 0; v += 2;
    *(uint16_t FAR*)v = param_count; v += 2;
    *(uint16_t FAR*)v = (uint16_t)(param_ptr - packet); v += 2;
    *(uint16_t FAR*)v = 0; v += 2;
    *(uint16_t FAR*)v = (uint16_t)(p - packet); v += 2;
    *v++ = 1; *v++ = 0;
    *(uint16_t FAR*)v = 0x0001; v += 2;
    *(uint16_t FAR*)bcc_ptr = (uint16_t)(p - data_start);
    
    if (!smb_send_packet(packet, (uint32_t)(p - packet))) { _ffree(packet); return; }
    if (!smb_recv_packet(packet, SMB_BUFFER_SIZE, &rlen)) { _ffree(packet); return; }
    
    if (((SMB1Header FAR*)packet)->status != 0) { _ffree(packet); return; }
    wct = *(packet + sizeof(SMB1Header));
    if (wct < 10) { _ffree(packet); return; }
    
    vwv_words = (uint16_t FAR*)(packet + sizeof(SMB1Header) + 1);
    param_off = vwv_words[4]; data_off = vwv_words[7]; data_cnt = vwv_words[6];
    
    if (param_off > 0 && data_off > 0 && data_cnt > 0) {
        uint8_t FAR *data_ptr = packet + data_off;
        uint8_t FAR *data_end = data_ptr + data_cnt;
        while (data_ptr < data_end) {
            uint32_t next_offset = *(uint32_t FAR*)(data_ptr + 0);
            uint32_t file_attrs = *(uint32_t FAR*)(data_ptr + 56);
            uint32_t name_len = *(uint32_t FAR*)(data_ptr + 60);
            uint8_t FAR *name_unicode = data_ptr + 94;
            if (name_len > 0 && (data_ptr + 94 + name_len <= data_end)) {
                char item_name[256]; int char_idx = 0; uint32_t i;
                _fmemset(item_name, 0, sizeof(item_name));
                for (i = 0; i < name_len && char_idx < 255; i += 2) {
                    item_name[char_idx++] = (char)*(name_unicode + i);
                }
                item_name[char_idx] = '\0';
                if (_fstrcmp(item_name, ".") != 0 && _fstrcmp(item_name, "..") != 0 && g_app.remote_count < MAX_ITEMS) {
                    _fstrncpy(g_app.remote_items[g_app.remote_count].path, item_name, MAX_SMB_PATH - 1);
                    g_app.remote_items[g_app.remote_count].is_dir = (file_attrs & 0x10) ? 1 : 0;
                    g_app.remote_count++;
                }
            }
            if (next_offset == 0) break;
            data_ptr += next_offset;
        }
    }
    _ffree(packet);
}

/* ==========================================================================
   SMB2 CLIENT PROTOCOL
   ========================================================================== */
static int smb2_negotiate(uint16_t FAR *out_dialect) {
    uint8_t FAR *packet = _fmalloc(65000);
    SMB2Header FAR *hdr;
    SMB2NegotiateReq FAR *neg;
    uint16_t FAR *dialects;
    uint32_t pkt_len, recv_len;
    
    if (!packet) return 0;
    _fmemset(packet, 0, 65000);
    hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_NEGOTIATE);
    neg = (SMB2NegotiateReq FAR*)(packet + sizeof(SMB2Header));
    neg->structure_size = 36; neg->dialect_count = 3; neg->security_mode = 1; neg->capabilities = 0;
    _fmemset(neg->client_guid, 0x11, 16);
    dialects = (uint16_t FAR*)(packet + sizeof(SMB2Header) + sizeof(SMB2NegotiateReq));
    dialects[0] = 0x0202; dialects[1] = 0x0210; dialects[2] = 0x0300;
    
    pkt_len = sizeof(SMB2Header) + sizeof(SMB2NegotiateReq) + (neg->dialect_count * sizeof(uint16_t));
    if (!smb_send_packet(packet, pkt_len)) { _ffree(packet); return 0; }
    if (!smb_recv_packet(packet, 65000, &recv_len)) { _ffree(packet); return 0; }
    
    hdr = (SMB2Header FAR*)packet; 
    if (_fmemcmp(hdr->protocol_id, "\xFE\x53\x4D\x42", 4) != 0 || (hdr->status != 0 && hdr->status != STATUS_SUCCESS)) {
        _ffree(packet); return 0; 
    }
    if (out_dialect) {
        SMB2NegotiateResp FAR *resp = (SMB2NegotiateResp FAR*)(packet + sizeof(SMB2Header));
        *out_dialect = resp->dialect_revision;
        _fmemcpy(g_app.smb2_server_guid, resp->server_guid, 16); 
    }
    _ffree(packet); return 1;
}

static int smb2_session_setup(const char FAR *user, const char FAR *pass) {
    uint8_t FAR *packet = _fmalloc(65000);
    SMB2Header FAR *hdr;
    SMB2SessionSetupReq FAR *setup_req;
    uint8_t FAR *payload_ptr;
    uint32_t pkt_len, recv_len;
    
    uint8_t ntlm_neg[] = {
        0x4E, 0x54, 0x4C, 0x4D, 0x53, 0x53, 0x50, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x15, 0x82, 0x08, 0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x02, 0xCE, 0x0E,
        0x00, 0x00, 0x00, 0x0F
    };

    if (!packet) return 0;
    _fmemset(packet, 0, 65000);
    hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_SESSION_SETUP);
    setup_req = (SMB2SessionSetupReq FAR*)(packet + sizeof(SMB2Header));
    setup_req->structure_size = 25; setup_req->flags = 0; setup_req->security_mode = 1;
    
    payload_ptr = packet + sizeof(SMB2Header) + sizeof(SMB2SessionSetupReq);
    _fmemcpy(payload_ptr, ntlm_neg, sizeof(ntlm_neg));
    setup_req->security_buffer_offset = sizeof(SMB2Header) + sizeof(SMB2SessionSetupReq);
    setup_req->security_buffer_length = sizeof(ntlm_neg);
    
    pkt_len = setup_req->security_buffer_offset + sizeof(ntlm_neg);
    if (!smb_send_packet(packet, pkt_len)) { _ffree(packet); return 0; }
    if (!smb_recv_packet(packet, 65000, &recv_len)) { _ffree(packet); return 0; }
    
    hdr = (SMB2Header FAR*)packet;
    if (hdr->status != STATUS_MORE_PROCESSING) { _ffree(packet); return 0; }
    g_app.smb2_session_id = hdr->session_id;
    
    {
        uint8_t ntlm_auth[] = {
            0x4E, 0x54, 0x4C, 0x4D, 0x53, 0x53, 0x50, 0x00, 0x03, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
            0x15, 0x82, 0x08, 0xE2, 0x05, 0x02, 0xCE, 0x0E, 0x00, 0x00, 0x00, 0x0F
        };
        
        _fmemset(packet, 0, 65000);
        hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_SESSION_SETUP);
        hdr->session_id = g_app.smb2_session_id;
        setup_req = (SMB2SessionSetupReq FAR*)(packet + sizeof(SMB2Header));
        setup_req->structure_size = 25;
        
        payload_ptr = packet + sizeof(SMB2Header) + sizeof(SMB2SessionSetupReq);
        _fmemcpy(payload_ptr, ntlm_auth, sizeof(ntlm_auth));
        setup_req->security_buffer_offset = sizeof(SMB2Header) + sizeof(SMB2SessionSetupReq);
        setup_req->security_buffer_length = sizeof(ntlm_auth);
        
        pkt_len = setup_req->security_buffer_offset + sizeof(ntlm_auth);
        if (!smb_send_packet(packet, pkt_len)) { _ffree(packet); return 0; }
        if (!smb_recv_packet(packet, 65000, &recv_len)) { _ffree(packet); return 0; }
        
        hdr = (SMB2Header FAR*)packet;
        if (hdr->status != STATUS_SUCCESS) { _ffree(packet); return 0; }
        _ffree(packet); return 1;
    }
}

static int smb2_tree_connect(const char FAR *server, const char FAR *share) {
    uint8_t FAR *packet = _fmalloc(65000);
    SMB2Header FAR *hdr;
    SMB2TreeConnectReq FAR *tc;
    char full_path[MAX_SMB_PATH];
    const char FAR *clean_share;
    uint8_t FAR *path_pos;
    uint32_t pkt_len, recv_len;
    
    if (!packet) return 0;
    _fmemset(packet, 0, 65000);
    hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_TREE_CONNECT);
    tc = (SMB2TreeConnectReq FAR*)(packet + sizeof(SMB2Header));
    tc->structure_size = 9; tc->reserved = 0;
    
    clean_share = (share[0] == '\\' || share[0] == '/') ? share + 1 : share;
    sprintf(full_path, "\\\\%s\\%s", server, clean_share);
    
    path_pos = packet + sizeof(SMB2Header) + sizeof(SMB2TreeConnectReq);
    tc->path_offset = sizeof(SMB2Header) + sizeof(SMB2TreeConnectReq);
    tc->path_length = (uint16_t)utf8_to_utf16le(full_path, path_pos, 65000 - tc->path_offset);
    
    pkt_len = tc->path_offset + tc->path_length;
    if (!smb_send_packet(packet, pkt_len)) { _ffree(packet); return 0; }
    if (!smb_recv_packet(packet, 65000, &recv_len)) { _ffree(packet); return 0; }
    
    hdr = (SMB2Header FAR*)packet; 
    if (hdr->status != STATUS_SUCCESS) { _ffree(packet); return 0; }
    g_app.smb2_tree_id = hdr->tree_id; 
    _ffree(packet); return 1;
}

static void smb2_list_directory(void) {
    uint8_t FAR *packet = _fmalloc(SMB_BUFFER_SIZE);
    SMB2Header FAR *hdr;
    SMB2CreateReq FAR *create;
    char rel_path[MAX_SMB_PATH];
    uint32_t pkt_len, recv_len;
    uint64_t file_id_pers, file_id_vol;
    int keep_querying = 1, first_query = 1;
    
    if (!packet) return;
    _fmemset(packet, 0, SMB_BUFFER_SIZE);
    hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_CREATE); hdr->tree_id = g_app.smb2_tree_id;
    create = (SMB2CreateReq FAR*)(packet + sizeof(SMB2Header));
    create->structure_size = 57; create->impersonation_level = 2;      
    create->desired_access = 0x00100081; create->file_attributes = 0x00000010; create->share_access = 0x00000007; 
    create->create_disposition = 1; create->create_options = 0x00000021; create->name_offset = 120;
    
    _fmemset(rel_path, 0, sizeof(rel_path));
    if (g_app.remote_base[0] != '\0' && _fstrcmp(g_app.remote_base, "\\") != 0 && _fstrcmp(g_app.remote_base, "/") != 0) {
        const char FAR *p = g_app.remote_base; if (*p == '\\' || *p == '/') p++;
        _fstrcpy(rel_path, p); 
        { int len = _fstrlen(rel_path); while (len > 0 && (rel_path[len-1] == '\\' || rel_path[len-1] == '/')) { rel_path[len-1] = '\0'; len--; } }
    }
    create->name_length = (uint16_t)utf8_to_utf16le(rel_path, packet + 120, SMB_BUFFER_SIZE - 120);
    pkt_len = 120 + create->name_length; if (create->name_length == 0) { packet[120] = 0; packet[121] = 0; pkt_len = 122; }

    if (!smb_send_packet(packet, pkt_len)) { _ffree(packet); return; }
    if (!smb_recv_packet(packet, SMB_BUFFER_SIZE, &recv_len)) { _ffree(packet); return; }
    hdr = (SMB2Header FAR*)packet; if (hdr->status != STATUS_SUCCESS) { _ffree(packet); return; }

    {
        SMB2CreateResp FAR *create_resp = (SMB2CreateResp FAR*)(packet + sizeof(SMB2Header));
        file_id_pers = create_resp->file_id_persistent; file_id_vol  = create_resp->file_id_volatile;
    }

    while (keep_querying && g_app.remote_count < MAX_ITEMS) {
        SMB2QueryDirReqFixed FAR *qdir;
        _fmemset(packet, 0, sizeof(SMB2Header) + 128); 
        hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_QUERY_DIRECTORY); hdr->tree_id = g_app.smb2_tree_id;
        qdir = (SMB2QueryDirReqFixed FAR*)(packet + sizeof(SMB2Header));
        qdir->structure_size = 33; qdir->file_information_class = 37; qdir->flags = first_query ? 0x01 : 0x00; first_query = 0;
        qdir->file_id_persistent = file_id_pers; qdir->file_id_volatile = file_id_vol; qdir->output_buffer_length = 64000; qdir->name_offset = 96; 
        *(uint16_t FAR*)(packet + 96) = '*'; qdir->name_length = 2; pkt_len = 96 + 2;
        
        if (!smb_send_packet(packet, pkt_len)) break;
        if (!smb_recv_packet(packet, SMB_BUFFER_SIZE, &recv_len)) break;

        hdr = (SMB2Header FAR*)packet;
        if (hdr->status == STATUS_SUCCESS) {
            uint16_t data_off = *(uint16_t FAR*)(packet + sizeof(SMB2Header) + 2); 
            uint32_t data_len = *(uint32_t FAR*)(packet + sizeof(SMB2Header) + 4);
            if (data_off > 0 && data_len > 0 && (data_off + data_len <= recv_len)) {
                uint8_t FAR *data_ptr = packet + data_off; uint8_t FAR *data_end = data_ptr + data_len;
                while (data_ptr + 104 <= data_end) {
                    uint32_t next_offset = *(uint32_t FAR*)(data_ptr + 0); uint32_t file_attrs = *(uint32_t FAR*)(data_ptr + 56);
                    uint32_t name_len = *(uint32_t FAR*)(data_ptr + 60); uint8_t FAR *name_unicode = data_ptr + 104; 
                    if (name_len > 0 && name_unicode + name_len <= data_end) {
                        char item_name[256]; int char_idx = 0; uint32_t i;
                        _fmemset(item_name, 0, sizeof(item_name));
                        for (i = 0; i < name_len && char_idx < 255; i += 2) { item_name[char_idx++] = (char)*(name_unicode + i); }
                        item_name[char_idx] = '\0';
                        if (_fstrcmp(item_name, ".") != 0 && _fstrcmp(item_name, "..") != 0 && g_app.remote_count < MAX_ITEMS) {
                            _fstrncpy(g_app.remote_items[g_app.remote_count].path, item_name, MAX_SMB_PATH - 1);
                            g_app.remote_items[g_app.remote_count].is_dir = (file_attrs & 0x10) ? 1 : 0; g_app.remote_count++;
                        }
                    }
                    if (next_offset == 0) break; data_ptr += next_offset;
                }
            } else { keep_querying = 0; }
        } else { keep_querying = 0; }
    }

    _fmemset(packet, 0, SMB_BUFFER_SIZE); hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_CLOSE); hdr->tree_id = g_app.smb2_tree_id;
    {
        SMB2CloseReq FAR *cl = (SMB2CloseReq FAR*)(packet + sizeof(SMB2Header)); cl->structure_size = 24; 
        cl->file_id_persistent = file_id_pers; cl->file_id_volatile = file_id_vol; 
        smb_send_packet(packet, sizeof(SMB2Header) + 24); smb_recv_packet(packet, SMB_BUFFER_SIZE, &recv_len);
    }
    _ffree(packet);
}

static int smb2_tree_disconnect(void) {
    uint8_t FAR *packet = _fmalloc(65000);
    SMB2Header FAR *hdr;
    uint16_t FAR *struct_size;
    uint32_t recv_len;
    int ret;
    
    if (!packet) return 0;
    _fmemset(packet, 0, 65000);
    hdr = (SMB2Header FAR*)packet; smb2_init_header(hdr, SMB2_TREE_DISCONNECT);
    struct_size = (uint16_t FAR*)(packet + sizeof(SMB2Header));
    *struct_size = 4; *(struct_size + 1) = 0; 
    if (!smb_send_packet(packet, sizeof(SMB2Header) + 4)) { _ffree(packet); return 0; }
    ret = smb_recv_packet(packet, 65000, &recv_len);
    _ffree(packet);
    return ret;
}

static int smb2_ipc_enum_shares(const char FAR *server, char shares[MAX_SHARES][64], int max_shares) {
    uint8_t FAR *pkt;
    uint32_t rlen;
    SMB2Header FAR *hdr;
    SMB2CreateReq FAR *cr;
    SMB2CreateResp FAR *cresp;
    uint64_t fid_pers, fid_vol;
    int count = 0;
    uint8_t bind_req[] = { 0x05, 0x00, 0x0b, 0x03, 0x10, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xb8, 0x10, 0xb8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xc8, 0x4f, 0x32, 0x4b, 0x70, 0x16, 0xd3, 0x01, 0x12, 0x78, 0x5a, 0x47, 0xbf, 0x6e, 0xe1, 0x88, 0x03, 0x00, 0x00, 0x00, 0x04, 0x5d, 0x88, 0x8a, 0xeb, 0x1c, 0xc9, 0x11, 0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60, 0x02, 0x00, 0x00, 0x00 };
    uint8_t enum_req[] = { 0x05, 0x00, 0x00, 0x03, 0x10, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    if (!smb2_tree_connect(server, "IPC$")) return 0;
    pkt = _fmalloc(SMB_BUFFER_SIZE);
    if (!pkt) { smb2_tree_disconnect(); return 0; }
    
    _fmemset(pkt, 0, SMB_BUFFER_SIZE);
    hdr = (SMB2Header FAR*)pkt; smb2_init_header(hdr, SMB2_CREATE);
    cr = (SMB2CreateReq FAR*)(pkt + sizeof(SMB2Header));
    cr->structure_size = 57; cr->impersonation_level = 2; cr->desired_access = 0x0012019F; cr->file_attributes = 0;
    cr->share_access = 0x07; cr->create_disposition = 1; cr->create_options = 0; cr->name_offset = 120;
    cr->name_length = (uint16_t)utf8_to_utf16le("srvsvc", pkt + 120, 256);
    
    if (!smb_send_packet(pkt, 120 + cr->name_length)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    if (!smb_recv_packet(pkt, SMB_BUFFER_SIZE, &rlen)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    hdr = (SMB2Header FAR*)pkt; if (hdr->status != 0) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    
    cresp = (SMB2CreateResp FAR*)(pkt + sizeof(SMB2Header));
    fid_pers = cresp->file_id_persistent; fid_vol = cresp->file_id_volatile;
    
    _fmemset(pkt, 0, SMB_BUFFER_SIZE); hdr = (SMB2Header FAR*)pkt; smb2_init_header(hdr, SMB2_WRITE);
    {
        SMB2WriteReq FAR *wr = (SMB2WriteReq FAR*)(pkt + sizeof(SMB2Header)); wr->structure_size = 49; wr->data_offset = 112; wr->length = sizeof(bind_req); wr->file_id_persistent = fid_pers; wr->file_id_volatile = fid_vol;
        _fmemcpy(pkt + 112, bind_req, sizeof(bind_req));
    }
    if (!smb_send_packet(pkt, 112 + sizeof(bind_req))) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    if (!smb_recv_packet(pkt, SMB_BUFFER_SIZE, &rlen)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    
    _fmemset(pkt, 0, SMB_BUFFER_SIZE); hdr = (SMB2Header FAR*)pkt; smb2_init_header(hdr, SMB2_READ);
    {
        SMB2ReadReq FAR *rr = (SMB2ReadReq FAR*)(pkt + sizeof(SMB2Header)); rr->structure_size = 49; rr->length = 1024; rr->file_id_persistent = fid_pers; rr->file_id_volatile = fid_vol;
    }
    if (!smb_send_packet(pkt, sizeof(SMB2Header) + 49)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    if (!smb_recv_packet(pkt, SMB_BUFFER_SIZE, &rlen)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    
    _fmemset(pkt, 0, SMB_BUFFER_SIZE); hdr = (SMB2Header FAR*)pkt; smb2_init_header(hdr, SMB2_WRITE);
    {
        SMB2WriteReq FAR *wr = (SMB2WriteReq FAR*)(pkt + sizeof(SMB2Header)); wr->structure_size = 49; wr->data_offset = 112; wr->length = sizeof(enum_req); wr->file_id_persistent = fid_pers; wr->file_id_volatile = fid_vol;
        _fmemcpy(pkt + 112, enum_req, sizeof(enum_req));
    }
    if (!smb_send_packet(pkt, 112 + sizeof(enum_req))) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    if (!smb_recv_packet(pkt, SMB_BUFFER_SIZE, &rlen)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    
    _fmemset(pkt, 0, SMB_BUFFER_SIZE); hdr = (SMB2Header FAR*)pkt; smb2_init_header(hdr, SMB2_READ);
    {
        SMB2ReadReq FAR *rr = (SMB2ReadReq FAR*)(pkt + sizeof(SMB2Header)); rr->structure_size = 49; rr->length = 65000; rr->file_id_persistent = fid_pers; rr->file_id_volatile = fid_vol;
    }
    if (!smb_send_packet(pkt, sizeof(SMB2Header) + 49)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    if (!smb_recv_packet(pkt, SMB_BUFFER_SIZE, &rlen)) { _ffree(pkt); smb2_tree_disconnect(); return 0; }
    
    hdr = (SMB2Header FAR*)pkt;
    if (hdr->status == 0) {
        SMB2ReadResp FAR *rresp = (SMB2ReadResp FAR*)(pkt + sizeof(SMB2Header));
        if (rresp->data_length > 24) {
            uint8_t FAR *payload = pkt + rresp->data_offset; 
            uint8_t FAR *ptr = payload + 24; 
            uint32_t level, sw_val, ptr_ctr;
            
            level = *(uint32_t FAR*)ptr; ptr+=4; 
            sw_val = *(uint32_t FAR*)ptr; ptr+=4; 
            ptr_ctr = *(uint32_t FAR*)ptr; ptr+=4;
            
            if (ptr_ctr && ptr + 8 <= payload + rresp->data_length) {
                uint32_t ent_read, ptr_arr;
                ent_read = *(uint32_t FAR*)ptr; ptr+=4; 
                ptr_arr = *(uint32_t FAR*)ptr; ptr+=4;
                
                if (ptr_arr && ent_read > 0 && ptr + 4 <= payload + rresp->data_length) {
                    uint32_t max_c, act_c;
                    max_c = *(uint32_t FAR*)ptr; ptr+=4; 
                    act_c = ent_read;
                    
                    if (act_c < 1000 && ptr + act_c * 12 <= payload + rresp->data_length) {
                        uint32_t FAR *n_ptrs = (uint32_t FAR*)_fmalloc((size_t)(act_c * 4)); 
                        uint32_t FAR *types = (uint32_t FAR*)_fmalloc((size_t)(act_c * 4)); 
                        uint32_t FAR *r_ptrs = (uint32_t FAR*)_fmalloc((size_t)(act_c * 4));
                        uint32_t i;
                        if (n_ptrs && types && r_ptrs) {
                            for(i=0; i<act_c; i++) { 
                                n_ptrs[i] = *(uint32_t FAR*)ptr; ptr+=4; 
                                types[i] = *(uint32_t FAR*)ptr; ptr+=4; 
                                r_ptrs[i] = *(uint32_t FAR*)ptr; ptr+=4; 
                            }
                            for(i=0; i<act_c; i++) {
                                if (n_ptrs[i] && ptr + 12 <= payload + rresp->data_length) {
                                    uint32_t sm, so, sa;
                                    sm = *(uint32_t FAR*)ptr; ptr+=4; 
                                    so = *(uint32_t FAR*)ptr; ptr+=4; 
                                    sa = *(uint32_t FAR*)ptr; ptr+=4;
                                    if (ptr + sa * 2 <= payload + rresp->data_length) { 
                                        if ((types[i] & 0xFF) == 0) { 
                                            char sname[64]; int idx = 0; uint32_t c;
                                            _fmemset(sname, 0, sizeof(sname));
                                            for(c=0; c<sa; c++) { 
                                                uint16_t wc = *(uint16_t FAR*)ptr; 
                                                if (wc != 0 && idx < 63) { sname[idx++] = (char)wc; } 
                                                ptr+=2; 
                                            }
                                            sname[idx] = '\0';
                                            if (_fstrlen(sname) > 0 && sname[_fstrlen(sname)-1] != '$' && count < max_shares) { 
                                                _fstrcpy(shares[count++], sname); 
                                            }
                                        } else { ptr += sa * 2; }
                                    }
                                    if ((ptr - payload) % 4 != 0) ptr += 4 - ((ptr - payload) % 4);
                                }
                                if (r_ptrs[i] && ptr + 12 <= payload + rresp->data_length) {
                                    uint32_t sm, so, sa;
                                    sm = *(uint32_t FAR*)ptr; ptr+=4; 
                                    so = *(uint32_t FAR*)ptr; ptr+=4; 
                                    sa = *(uint32_t FAR*)ptr; ptr+=4;
                                    ptr += sa * 2; 
                                    if ((ptr - payload) % 4 != 0) ptr += 4 - ((ptr - payload) % 4);
                                }
                            }
                        }
                        if (n_ptrs) _ffree(n_ptrs); 
                        if (types) _ffree(types); 
                        if (r_ptrs) _ffree(r_ptrs);
                    }
                }
            }
        }
    }
    
    _fmemset(pkt, 0, SMB_BUFFER_SIZE); hdr = (SMB2Header FAR*)pkt; smb2_init_header(hdr, SMB2_CLOSE);
    {
        SMB2CloseReq FAR *cl = (SMB2CloseReq FAR*)(pkt + sizeof(SMB2Header)); cl->structure_size = 24; cl->file_id_persistent = fid_pers; cl->file_id_volatile = fid_vol;
    }
    smb_send_packet(pkt, sizeof(SMB2Header) + 24); smb_recv_packet(pkt, SMB_BUFFER_SIZE, &rlen);
    smb2_tree_disconnect(); _ffree(pkt); return count;
}

static int enum_shares_ipc(char shares[MAX_SHARES][64], int max_shares) {
    uint8_t FAR *packet = _fmalloc(SMB_BUFFER_SIZE);
    SMB1Header FAR *hdr;
    uint8_t FAR *w;
    char ipc_share[256];
    uint8_t path_utf16[MAX_SMB_PATH * 2];
    uint32_t path_len_utf16;
    uint16_t byte_count;
    uint32_t rlen;
    uint8_t FAR *vwv;
    uint8_t FAR *bcc_ptr;
    uint8_t FAR *data_start;
    uint8_t FAR *name_ptr;
    uint16_t name_len;
    uint8_t FAR *param_ptr;
    uint8_t FAR *p;
    uint16_t param_count;
    uint16_t data_count;
    uint8_t FAR *v;
    uint8_t wct;
    uint16_t FAR *vwv_words;
    uint16_t param_off, data_off;
    int count = 0;

    if (!packet) return 0;
    _fmemset(packet, 0, SMB_BUFFER_SIZE);
    hdr = smb_build_header(packet, 0x75); hdr->flags2 |= 0x8000; hdr->tid = 0xFFFF;
    w = packet + sizeof(SMB1Header); *w++ = 4; *w++ = 0xFF; *w++ = 0; *(uint16_t FAR*)w = 0; w += 2; *(uint16_t FAR*)w = 0; w += 2; *(uint16_t FAR*)w = 1; w += 2;     
    sprintf(ipc_share, "\\\\%s\\IPC$", g_app.pending_server);
    path_len_utf16 = utf8_to_utf16le(ipc_share, path_utf16, sizeof(path_utf16));
    byte_count = (uint16_t)(1 + path_len_utf16 + 2 + 6); *(uint16_t FAR*)w = byte_count; w += 2; *w++ = 0;                      
    _fmemcpy(w, path_utf16, path_len_utf16); w += path_len_utf16; *w++ = 0; *w++ = 0; _fstrcpy((char FAR*)w, "?????"); w += 6;
    if (!smb_send_packet(packet, (uint32_t)(w - packet))) { _ffree(packet); return 0; }
    if (!smb_recv_packet(packet, SMB_BUFFER_SIZE, &rlen)) { _ffree(packet); return 0; }
    if (((SMB1Header FAR*)packet)->status != 0) { _ffree(packet); return 0; }
    
    g_app.tid = ((SMB1Header FAR*)packet)->tid;
    _fmemset(packet, 0, SMB_BUFFER_SIZE); hdr = smb_build_header(packet, 0x25); hdr->tid = g_app.tid;
    vwv = packet + sizeof(SMB1Header); *vwv++ = 14; 
    bcc_ptr = vwv + 14 * 2; data_start = bcc_ptr + 2; name_ptr = data_start;
    _fstrcpy((char FAR*)name_ptr, "\\PIPE\\LANMAN"); name_len = _fstrlen("\\PIPE\\LANMAN") + 1;
    param_ptr = name_ptr + name_len; p = param_ptr;
    *(uint16_t FAR*)p = 0x0000; p += 2; _fstrcpy((char FAR*)p, "WrLeh"); p += 6; _fstrcpy((char FAR*)p, "B13BWz"); p += 7; *(uint16_t FAR*)p = 0x0001; p += 2; *(uint16_t FAR*)p = 0xFFFF; p += 2;      
    param_count = (uint16_t)(p - param_ptr); data_count = 0; v = vwv;
    *(uint16_t FAR*)v = param_count; v += 2; *(uint16_t FAR*)v = data_count; v += 2; *(uint16_t FAR*)v = 1024; v += 2; *(uint16_t FAR*)v = 65535U; v += 2; *v++ = 0; *v++ = 0; *(uint16_t FAR*)v = 0; v += 2; *(uint32_t FAR*)v = 0; v += 4; *(uint16_t FAR*)v = 0; v += 2; *(uint16_t FAR*)v = param_count; v += 2; *(uint16_t FAR*)v = (uint16_t)(param_ptr - packet); v += 2; *(uint16_t FAR*)v = data_count; v += 2; *(uint16_t FAR*)v = 0; v += 2; *v++ = 0; *v++ = 0;                  
    *(uint16_t FAR*)bcc_ptr = (uint16_t)(p - data_start);
    if (!smb_send_packet(packet, (uint32_t)(p - packet))) { _ffree(packet); return 0; }
    if (!smb_recv_packet(packet, SMB_BUFFER_SIZE, &rlen)) { _ffree(packet); return 0; }
    wct = *(packet + sizeof(SMB1Header)); if (wct < 10) { _ffree(packet); return 0; }
    
    vwv_words = (uint16_t FAR*)(packet + sizeof(SMB1Header) + 1); param_off = vwv_words[4]; data_off  = vwv_words[7];
    if (param_off > 0 && data_off > 0) {
        uint16_t FAR *resp_param_words = (uint16_t FAR*)(packet + param_off); uint16_t status = resp_param_words[0]; uint16_t entries_returned = resp_param_words[2];
        if (status == 0 || status == 234) { 
            uint8_t FAR *data = packet + data_off;
            int i;
            for (i = 0; i < entries_returned && count < max_shares && data + (i * 20) + 13 <= packet + rlen; i++) {
                char share_name[14]; int j;
                _fmemcpy(share_name, data + (i * 20), 13); share_name[13] = '\0';
                for (j = 12; j >= 0; j--) { if (share_name[j] == ' ') share_name[j] = '\0'; else if (share_name[j] != '\0') break; }
                if (share_name[0] != '\0' && _fstrcmp(share_name, "ADMIN$") != 0 && _fstrcmp(share_name, "IPC$") != 0) { _fstrcpy(shares[count++], share_name); }
            }
        }
    }
    _ffree(packet); return count;
}

static int enumerate_shares(char shares[MAX_SHARES][64], int max_shares, int is_smb2_capable) {
    int count = 0;
    if (is_smb2_capable) {
        count = smb2_ipc_enum_shares(g_app.pending_server, shares, max_shares);
        if (count > 0) return count;
    }
    count = enum_shares_ipc(shares, max_shares);
    if (count > 0) return count;
    return 0;
}

/* ==========================================================================
   FILE SYSTEM OPERATIONS
   ========================================================================== */
static void list_remote(void) {
    int i;
    if (!g_app.sconn || g_app.sconn == INVALID_SOCKET) return;
    g_app.remote_count = 0; 
    _fstrcpy(g_app.remote_items[0].path, "."); g_app.remote_items[0].is_dir = 1; 
    _fstrcpy(g_app.remote_items[1].path, ".."); g_app.remote_items[1].is_dir = 1; 
    g_app.remote_count = 2;
    
    if (g_app.current_proto == PROTO_SMB2) {
        smb2_list_directory();
    } else if (g_app.current_proto == PROTO_SMB1) {
        smb1_list_directory();
    }
    
    SendMessage(g_app.hRemoteList, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < g_app.remote_count; i++) {
        char display[MAX_SMB_PATH]; 
        sprintf(display, "%s%s", g_app.remote_items[i].is_dir ? "[DIR] " : "", g_app.remote_items[i].path);
        SendMessage(g_app.hRemoteList, LB_ADDSTRING, 0, (LPARAM)(char FAR*)display);
    }
    {
        char lbl[MAX_SMB_PATH]; 
        const char FAR *pr = (g_app.current_proto == PROTO_SMB2) ? "SMB2" : "SMB1";
        sprintf(lbl, "Remote (%s): %s", pr, g_app.remote_base); SetWindowText(g_app.hLblRemote, lbl);
    }
}

static void list_local(void) {
    int i;
    g_app.local_count = 0; 
    _fstrcpy(g_app.local_items[0].path, "."); g_app.local_items[0].is_dir = 1; 
    _fstrcpy(g_app.local_items[1].path, ".."); g_app.local_items[1].is_dir = 1; 
    g_app.local_count = 2;
    
    if (_fstrlen(g_app.local_base) == 0) {
        unsigned int drive;
        for (drive = 1; drive <= 26; drive++) {
            struct diskfree_t df;
            if (_dos_getdiskfree(drive, &df) == 0) {
                if (g_app.local_count < MAX_ITEMS) {
                    g_app.local_items[g_app.local_count].path[0] = 'A' + drive - 1;
                    g_app.local_items[g_app.local_count].path[1] = ':';
                    g_app.local_items[g_app.local_count].path[2] = '\\';
                    g_app.local_items[g_app.local_count].path[3] = '\0';
                    g_app.local_items[g_app.local_count].is_dir = 1;
                    g_app.local_count++;
                }
            }
        }
    } else {
        char spath[MAX_SMB_PATH];
        struct find_t fd;
        sprintf(spath, "%s\\*.*", g_app.local_base);
        if (_dos_findfirst(spath, _A_NORMAL|_A_SUBDIR|_A_RDONLY|_A_HIDDEN|_A_SYSTEM, &fd) == 0) {
            do {
                if (_fstrcmp(fd.name, ".") != 0 && _fstrcmp(fd.name, "..") != 0 && g_app.local_count < MAX_ITEMS) {
                    _fstrncpy(g_app.local_items[g_app.local_count].path, fd.name, MAX_SMB_PATH-1);
                    g_app.local_items[g_app.local_count].is_dir = (fd.attrib & _A_SUBDIR) ? 1 : 0;
                    g_app.local_count++;
                }
            } while (_dos_findnext(&fd) == 0);
        }
    }

    SendMessage(g_app.hLocalList, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < g_app.local_count; i++) {
        char display[MAX_SMB_PATH]; 
        sprintf(display, "%s%s", g_app.local_items[i].is_dir ? "[DIR] " : "", g_app.local_items[i].path);
        SendMessage(g_app.hLocalList, LB_ADDSTRING, 0, (LPARAM)(char FAR*)display);
    }
    {
        char lbl[MAX_SMB_PATH]; 
        sprintf(lbl, "Local: %s", g_app.local_base); SetWindowText(g_app.hLblLocal, lbl);
    }
}

/* ==========================================================================
   UI PROCEDURES (Dialogs & Editor)
   ========================================================================== */
long FAR PASCAL CreateDirWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit;
    switch (msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "Folder Name:", WS_CHILD|WS_VISIBLE, 10, 10, 90, 20, hwnd, NULL, g_hInst, NULL);
            hEdit = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 105, 10, 165, 20, hwnd, (HMENU)IDE_MKDIR_NAME, g_hInst, NULL);
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 100, 40, 80, 25, hwnd, (HMENU)IDB_MKDIR_OK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 190, 40, 80, 25, hwnd, (HMENU)IDB_CANCEL, g_hInst, NULL);
            SetFocus(hEdit); return 0;
        }
        case DM_GETDEFID: return MAKELRESULT(IDB_MKDIR_OK, DC_HASDEFID);
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDB_MKDIR_OK || id == IDOK) {
                char dir_name[MAX_SMB_PATH]; 
                GetWindowText(hEdit, dir_name, sizeof(dir_name)); trim_str(dir_name);
                if (_fstrlen(dir_name) > 0) {
                    MessageBox(hwnd, "Directory creation is stubbed to fit the Win16 stack limit in this build.", "Action Stub", MB_OK | MB_ICONASTERISK);
                }
                SendMessage(hwnd, WM_CLOSE, 0, 0L);
            } else if (id == IDB_CANCEL || id == IDCANCEL) { SendMessage(hwnd, WM_CLOSE, 0, 0L); }
            break;
        }
        case WM_CLOSE: EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

long FAR PASCAL RenameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit;
    switch (msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "New Name:", WS_CHILD|WS_VISIBLE, 10, 10, 80, 20, hwnd, NULL, g_hInst, NULL);
            hEdit = CreateWindow("EDIT", g_ren_item, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 100, 10, 170, 20, hwnd, (HMENU)IDE_RENAME_NEW, g_hInst, NULL);
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 100, 40, 80, 25, hwnd, (HMENU)IDB_RENAME_OK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 190, 40, 80, 25, hwnd, (HMENU)IDB_CANCEL, g_hInst, NULL);
            SetFocus(hEdit); return 0;
        }
        case DM_GETDEFID: return MAKELRESULT(IDB_RENAME_OK, DC_HASDEFID);
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDB_RENAME_OK || id == IDOK) {
                MessageBox(hwnd, "File renaming is stubbed to fit the Win16 stack limit in this build.", "Action Stub", MB_OK | MB_ICONASTERISK);
                SendMessage(hwnd, WM_CLOSE, 0, 0L);
            } else if (id == IDB_CANCEL || id == IDCANCEL) { SendMessage(hwnd, WM_CLOSE, 0, 0L); }
            break;
        }
        case WM_CLOSE: EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

long FAR PASCAL EditConnWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hN, hS, hPrt, hSh, hU, hP, hCmbProto;
    switch (msg) {
        case WM_CREATE: {
            int y = 10;
            ConnectionProfile FAR *p = &g_app.connections[g_app.selected_conn_idx];
            char hist_copy[512]; 
            char FAR *token;

            CreateWindow("STATIC", "Name:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL); 
            hN = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 110, y, 200, 20, hwnd, NULL, g_hInst, NULL); y+=25;
            
            CreateWindow("STATIC", "Server:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL); 
            hS = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 110, y, 200, 20, hwnd, NULL, g_hInst, NULL); y+=25;
            
            CreateWindow("STATIC", "Port (Def=445):", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL); 
            hPrt = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 110, y, 200, 20, hwnd, NULL, g_hInst, NULL); y+=25;
            
            CreateWindow("STATIC", "Share/Path:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL); 
            hSh = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWN|CBS_AUTOHSCROLL, 110, y, 140, 150, hwnd, NULL, g_hInst, NULL); 
            CreateWindow("BUTTON", "Test", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 255, y, 55, 20, hwnd, (HMENU)IDB_TEST, g_hInst, NULL);
            y+=25;
            
            CreateWindow("STATIC", "User:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL); 
            hU = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 110, y, 200, 20, hwnd, NULL, g_hInst, NULL); y+=25;
            
            CreateWindow("STATIC", "Pass:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL); 
            hP = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_PASSWORD, 110, y, 200, 20, hwnd, NULL, g_hInst, NULL); y+=25;
            
            CreateWindow("STATIC", "SMB Version:", WS_CHILD|WS_VISIBLE, 10, y, 90, 20, hwnd, NULL, g_hInst, NULL); 
            hCmbProto = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_TABSTOP, 110, y, 200, 100, hwnd, (HMENU)IDC_CMB_PROTO, g_hInst, NULL);
            SendMessage(hCmbProto, CB_ADDSTRING, 0, (LPARAM)(char FAR*)"Auto (Detect)");
            SendMessage(hCmbProto, CB_ADDSTRING, 0, (LPARAM)(char FAR*)"SMB 2.x (Modern)");
            SendMessage(hCmbProto, CB_ADDSTRING, 0, (LPARAM)(char FAR*)"SMB 1.0 (Classic)");
            y+=30;
            
            CreateWindow("BUTTON", "Add", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 10, y, 70, 25, hwnd, (HMENU)IDB_ADD, g_hInst, NULL); 
            CreateWindow("BUTTON", "Save", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 87, y, 70, 25, hwnd, (HMENU)IDB_SAVE, g_hInst, NULL); 
            CreateWindow("BUTTON", "Delete", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 164, y, 70, 25, hwnd, (HMENU)IDB_DELETE, g_hInst, NULL); 
            CreateWindow("BUTTON", "Close", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 241, y, 70, 25, hwnd, (HMENU)IDB_CANCEL, g_hInst, NULL);
            
            SetWindowText(hN, p->name); SetWindowText(hS, p->server); SetWindowText(hPrt, p->port); 
            SetWindowText(hU, p->user); SetWindowText(hP, p->pass); 
            
            if (p->proto_pref == PROTO_SMB2) SendMessage(hCmbProto, CB_SETCURSEL, 1, 0L);
            else if (p->proto_pref == PROTO_SMB1) SendMessage(hCmbProto, CB_SETCURSEL, 2, 0L);
            else SendMessage(hCmbProto, CB_SETCURSEL, 0, 0L);
            
            _fstrcpy(hist_copy, p->shares_hist); 
            token = _fstrtok(hist_copy, "|"); 
            while (token) { 
                SendMessage(hSh, CB_ADDSTRING, 0, (LPARAM)token); 
                token = _fstrtok(NULL, "|"); 
            } 
            SetWindowText(hSh, p->share);
            
            return 0;
        }
        case DM_GETDEFID: return MAKELRESULT(IDB_SAVE, DC_HASDEFID);
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDB_SAVE || id == IDOK) {
                int psel;
                ConnectionProfile FAR *p = &g_app.connections[g_app.selected_conn_idx];
                GetWindowText(hN, p->name, 64); GetWindowText(hS, p->server, 128); GetWindowText(hPrt, p->port, 16); 
                GetWindowText(hSh, p->share, 128); GetWindowText(hU, p->user, 64); GetWindowText(hP, p->pass, 64);
                psel = (int)SendMessage(hCmbProto, CB_GETCURSEL, 0, 0L);
                if (psel == 1) p->proto_pref = PROTO_SMB2;
                else if (psel == 2) p->proto_pref = PROTO_SMB1;
                else p->proto_pref = PROTO_AUTO;
                refresh_combo(); save_config(); add_log("Profile saved.");
            } else if (id == IDB_TEST) {
                char srv[128], prt[16], usr[64], pwd[64];
                char msg_buf[256];
                int port_num;
                uint16_t dialect = 0;
                
                GetWindowText(hS, srv, sizeof(srv));
                GetWindowText(hPrt, prt, sizeof(prt));
                GetWindowText(hU, usr, sizeof(usr));
                GetWindowText(hP, pwd, sizeof(pwd));
                
                port_num = prt[0] ? atoi(prt) : 445;
                disconnect_all();
                
                _fstrcpy(g_app.pending_server, srv);
                
                if (resolve_and_connect(srv, port_num)) {
                    g_app.smb2_message_id = 0; g_app.smb2_session_id = 0; g_app.smb2_tree_id = 0;
                    if (smb2_negotiate(&dialect)) {
                        int auth;
                        char shares[MAX_SHARES][64]; 
                        int count = 0;
                        auth = smb2_session_setup(usr, pwd);
                        if (auth) count = smb2_ipc_enum_shares(srv, shares, MAX_SHARES);
                        
                        sprintf(msg_buf, "Server Identified: SMB 2.x\nDialect: 0x%04X\nAuth: %s\nShares Found: %d",
                                dialect, auth ? "SUCCESS" : "FAILED", count);
                        MessageBox(hwnd, msg_buf, "Test Result", MB_OK | (auth ? MB_ICONASTERISK : MB_ICONHAND));
                        
                        if (count > 0) {
                            int i; char new_hist[512]; _fmemset(new_hist, 0, sizeof(new_hist));
                            SendMessage(hSh, CB_RESETCONTENT, 0, 0L); 
                            for(i = 0; i < count; i++) { 
                                SendMessage(hSh, CB_ADDSTRING, 0, (LPARAM)(char FAR*)shares[i]); 
                                if (i > 0) _fstrcat(new_hist, "|"); 
                                _fstrcat(new_hist, shares[i]); 
                            }
                            SendMessage(hSh, CB_SETCURSEL, 0, 0L); _fstrcpy(g_app.connections[g_app.selected_conn_idx].shares_hist, new_hist);
                        }
                        disconnect_all();
                        return 0;
                    }
                    disconnect_all();
                }
                
                if (resolve_and_connect(srv, port_num)) {
                    g_app.uid = 0; g_app.tid = 0; g_app.mid_counter = 1;
                    if (smb1_negotiate()) {
                        int auth;
                        char shares[MAX_SHARES][64]; 
                        int count = 0;
                        auth = smb1_session(usr, pwd);
                        if (auth) count = enum_shares_ipc(shares, MAX_SHARES);
                        
                        sprintf(msg_buf, "Server Identified: SMB 1.0\nAuth: %s\nShares Found: %d",
                                auth ? "SUCCESS" : "FAILED", count);
                        MessageBox(hwnd, msg_buf, "Test Result", MB_OK | (auth ? MB_ICONASTERISK : MB_ICONHAND));
                        
                        if (count > 0) {
                            int i; char new_hist[512]; _fmemset(new_hist, 0, sizeof(new_hist));
                            SendMessage(hSh, CB_RESETCONTENT, 0, 0L); 
                            for(i = 0; i < count; i++) { 
                                SendMessage(hSh, CB_ADDSTRING, 0, (LPARAM)(char FAR*)shares[i]); 
                                if (i > 0) _fstrcat(new_hist, "|"); 
                                _fstrcat(new_hist, shares[i]); 
                            }
                            SendMessage(hSh, CB_SETCURSEL, 0, 0L); _fstrcpy(g_app.connections[g_app.selected_conn_idx].shares_hist, new_hist);
                        }
                        disconnect_all();
                        return 0;
                    }
                    disconnect_all();
                }
                
                MessageBox(hwnd, "Server did not respond to SMB2 or SMB1 Negotiate.", "Test Result", MB_OK | MB_ICONHAND);
            } else if (id == IDB_ADD) {
                if (g_app.conn_count < g_app.conn_capacity) {
                    int psel;
                    int idx = g_app.conn_count++;
                    GetWindowText(hN, g_app.connections[idx].name, 64); 
                    GetWindowText(hS, g_app.connections[idx].server, 128); 
                    GetWindowText(hPrt, g_app.connections[idx].port, 16); 
                    GetWindowText(hSh, g_app.connections[idx].share, 128); 
                    GetWindowText(hU, g_app.connections[idx].user, 64); 
                    GetWindowText(hP, g_app.connections[idx].pass, 64);
                    psel = (int)SendMessage(hCmbProto, CB_GETCURSEL, 0, 0L);
                    if (psel == 1) g_app.connections[idx].proto_pref = PROTO_SMB2;
                    else if (psel == 2) g_app.connections[idx].proto_pref = PROTO_SMB1;
                    else g_app.connections[idx].proto_pref = PROTO_AUTO;
                    if (_fstrlen(g_app.connections[idx].name) == 0) _fstrcpy(g_app.connections[idx].name, "New Connection");
                    g_app.selected_conn_idx = idx; refresh_combo(); save_config(); add_log("Profile added.");
                } else { MessageBox(hwnd, "Maximum profiles reached.", "Error", MB_OK); }
            } else if (id == IDB_DELETE) {
                if (g_app.conn_count > 1) {
                    int i, del_idx = g_app.selected_conn_idx; 
                    for (i = del_idx; i < g_app.conn_count - 1; i++) g_app.connections[i] = g_app.connections[i + 1];
                    g_app.conn_count--; if (g_app.selected_conn_idx >= g_app.conn_count) g_app.selected_conn_idx = g_app.conn_count - 1;
                    refresh_combo(); save_config(); SendMessage(hwnd, WM_CLOSE, 0, 0L); add_log("Profile deleted.");
                } else add_log("Cannot delete last profile.");
            } else if (id == IDB_CANCEL || id == IDCANCEL) { SendMessage(hwnd, WM_CLOSE, 0, 0L); }
            break;
        }
        case WM_CLOSE: EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

long FAR PASCAL MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            int x = 10;
            char FAR *ls;
            
            g_app.hMain = hwnd; g_app.sconn = INVALID_SOCKET; g_app.conn_type = CONN_NONE; g_app.current_proto = PROTO_AUTO;
            
            g_app.hComboConn = CreateWindow("COMBOBOX", NULL, WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_TABSTOP, x, 10, 180, 200, hwnd, (HMENU)ID_COMBO_CONN, g_hInst, NULL); x += 185;
            g_app.hBtnEdit = CreateWindow("BUTTON", "Edit", WS_CHILD|WS_VISIBLE|WS_TABSTOP, x, 10, 45, 25, hwnd, (HMENU)ID_BTN_EDIT_CONN, g_hInst, NULL); x += 50;
            g_app.hBtnConnect = CreateWindow("BUTTON", "Connect", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, x, 10, 65, 25, hwnd, (HMENU)ID_BTN_CONNECT, g_hInst, NULL); x += 70;
            
            g_app.hBtnCopy = CreateWindow("BUTTON", "Copy", WS_CHILD|WS_VISIBLE|WS_TABSTOP, x, 10, 50, 25, hwnd, (HMENU)ID_BTN_COPY, g_hInst, NULL); x += 55;
            g_app.hBtnMove = CreateWindow("BUTTON", "Move", WS_CHILD|WS_VISIBLE|WS_TABSTOP, x, 10, 50, 25, hwnd, (HMENU)ID_BTN_MOVE, g_hInst, NULL); x += 55;
            g_app.hBtnRename = CreateWindow("BUTTON", "Ren", WS_CHILD|WS_VISIBLE|WS_TABSTOP, x, 10, 40, 25, hwnd, (HMENU)ID_BTN_RENAME, g_hInst, NULL); x += 45;
            g_app.hBtnDelete = CreateWindow("BUTTON", "Del", WS_CHILD|WS_VISIBLE|WS_TABSTOP, x, 10, 40, 25, hwnd, (HMENU)ID_BTN_DELETE, g_hInst, NULL); x += 45;
            g_app.hBtnMkDir = CreateWindow("BUTTON", "MkDir", WS_CHILD|WS_VISIBLE|WS_TABSTOP, x, 10, 50, 25, hwnd, (HMENU)ID_BTN_MKDIR, g_hInst, NULL);
            
            g_app.hLblLocal = CreateWindow("STATIC", "Local:", WS_CHILD|WS_VISIBLE|SS_LEFT, 10, 45, 340, 20, hwnd, NULL, g_hInst, NULL);
            g_app.hLblRemote = CreateWindow("STATIC", "Remote:", WS_CHILD|WS_VISIBLE|SS_LEFT, 360, 45, 340, 20, hwnd, NULL, g_hInst, NULL);
            
            g_app.hLocalList = CreateWindow("LISTBOX", NULL, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP, 10, 65, 340, 350, hwnd, (HMENU)ID_LIST_LOCAL, g_hInst, NULL);
            g_app.hRemoteList = CreateWindow("LISTBOX", NULL, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP, 360, 65, 340, 350, hwnd, (HMENU)ID_LIST_REMOTE, g_hInst, NULL);
            
            g_app.hStatus = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY, 10, 420, 690, 80, hwnd, (HMENU)ID_STATUSBAR, g_hInst, NULL);
            
            load_config(); refresh_combo();
            GetModuleFileName(NULL, g_app.local_base, MAX_SMB_PATH); 
            
            ls = _fstrrchr(g_app.local_base, '\\'); if (ls) *ls = '\0';
            list_local(); g_app.active_pane = PANE_LOCAL; list_remote();
            return 0;
        }
        case DM_GETDEFID: return MAKELRESULT(ID_BTN_CONNECT, DC_HASDEFID);
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (HIWORD(wp) == LBN_SETFOCUS) g_app.active_pane = (id == ID_LIST_REMOTE) ? PANE_REMOTE : PANE_LOCAL;
            if (HIWORD(wp) == LBN_DBLCLK) {
                int is_rem = (id == ID_LIST_REMOTE); 
                HWND hL = is_rem ? g_app.hRemoteList : g_app.hLocalList; 
                int idx = (int)SendMessage(hL, LB_GETCURSEL, 0, 0L);
                if (idx >= 0) {
                    char FAR *item = is_rem ? g_app.remote_items[idx].path : g_app.local_items[idx].path; 
                    int dir = is_rem ? g_app.remote_items[idx].is_dir : g_app.local_items[idx].is_dir;
                    if (dir) {
                        if (is_rem) {
                            if (_fstrcmp(item, "..") == 0) { 
                                char FAR *ls = _fstrrchr(g_app.remote_base, '\\'); 
                                if (ls && ls != g_app.remote_base) *ls = '\0'; else if (ls == g_app.remote_base) *(ls+1) = '\0'; 
                            } else if (_fstrcmp(item, ".") != 0) { 
                                if (g_app.remote_base[_fstrlen(g_app.remote_base)-1] != '\\') _fstrcat(g_app.remote_base, "\\"); 
                                _fstrcat(g_app.remote_base, item); 
                            }
                            list_remote();
                        } else {
                            if (_fstrcmp(item, "..") == 0) { 
                                if (_fstrlen(g_app.local_base) <= 3 && g_app.local_base[1] == ':') _fstrcpy(g_app.local_base, ""); 
                                else { char FAR *ls = _fstrrchr(g_app.local_base, '\\'); if (ls) { if (ls == g_app.local_base + 2) *(ls + 1) = '\0'; else *ls = '\0'; } } 
                            } else if (_fstrcmp(item, ".") != 0) { 
                                if (_fstrlen(g_app.local_base) == 0) sprintf(g_app.local_base, "%s\\", item); 
                                else { if (g_app.local_base[_fstrlen(g_app.local_base)-1] != '\\') _fstrcat(g_app.local_base, "\\"); _fstrcat(g_app.local_base, item); } 
                            }
                            list_local();
                        }
                    }
                }
            }
            if (id == ID_BTN_CONNECT || id == IDOK) {
                int idx = (int)SendMessage(g_app.hComboConn, CB_GETCURSEL, 0, 0L); 
                if (idx != CB_ERR) {
                    ConnectionProfile FAR *p = &g_app.connections[idx];
                    int port_num = p->port[0] ? atoi(p->port) : 445;
                    int try_smb2 = (p->proto_pref == PROTO_SMB2 || p->proto_pref == PROTO_AUTO);
                    int try_smb1 = (p->proto_pref == PROTO_SMB1 || p->proto_pref == PROTO_AUTO);
                    
                    g_app.selected_conn_idx = idx; save_config(); 
                    disconnect_all(); 
                    
                    if (try_smb2) {
                        if (resolve_and_connect(p->server, port_num)) {
                            add_log("Connecting via SMB2 to %s...", (char FAR*)p->server);
                            g_app.smb2_message_id = 0; g_app.smb2_session_id = 0; g_app.smb2_tree_id = 0;
                            if (smb2_negotiate(NULL) && smb2_session_setup(p->user, p->pass)) {
                                if (smb2_tree_connect(p->server, p->share)) {
                                    g_app.conn_type = CONN_SMB; g_app.current_proto = PROTO_SMB2; _fstrcpy(g_app.remote_base, "\\"); 
                                    list_remote(); add_log("Connected via SMB2"); return 0;
                                }
                            }
                            disconnect_all();
                        }
                    }
                    
                    if (try_smb1) {
                        if (resolve_and_connect(p->server, port_num)) {
                            add_log("Connecting via SMB1 to %s...", (char FAR*)p->server);
                            g_app.uid = 0; g_app.tid = 0; g_app.mid_counter = 1;
                            if (smb1_negotiate() && smb1_session(p->user, p->pass)) {
                                if (smb1_tree_connect(p->server, p->share)) {
                                    g_app.conn_type = CONN_SMB; g_app.current_proto = PROTO_SMB1; _fstrcpy(g_app.remote_base, "\\"); 
                                    list_remote(); add_log("Connected via SMB1"); return 0;
                                }
                            }
                            disconnect_all();
                        }
                    }
                    
                    add_log("Failed to connect with selected profile.");
                }
            } else if (id == ID_BTN_EDIT_CONN) {
                g_app.selected_conn_idx = (int)SendMessage(g_app.hComboConn, CB_GETCURSEL, 0, 0L);
                CreateWindow("EditConnClass", "Edit Connection", WS_VISIBLE|WS_POPUP|WS_CAPTION|WS_SYSMENU, 
                             CW_USEDEFAULT, CW_USEDEFAULT, 350, 310, hwnd, NULL, g_hInst, NULL); 
                EnableWindow(hwnd, FALSE);
            } else if (id == ID_BTN_COPY || id == ID_BTN_MOVE || id == ID_BTN_RENAME || id == ID_BTN_DELETE) {
                MessageBox(hwnd, "File operations are stubbed to fit the Win16 stack limit in this build.", "Action Stub", MB_OK | MB_ICONASTERISK);
            } else if (id == ID_BTN_MKDIR) {
                CreateWindow("CreateDirClass", "Create Directory", WS_VISIBLE|WS_POPUP|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 300, 110, g_app.hMain, NULL, g_hInst, NULL); 
                EnableWindow(g_app.hMain, FALSE);
            }
            break;
        }
        case WM_DESTROY: 
            disconnect_all(); save_config(); 
            if (g_app.connections) _ffree(g_app.connections); 
            if (g_app.remote_items) _ffree(g_app.remote_items);
            if (g_app.local_items) _ffree(g_app.local_items);
            PostQuitMessage(0); 
            break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc;
    MSG msg;
    WSADATA wsa;
    
    WSAStartup(0x0101, &wsa);
    g_hInst = hInst; _fmemset(&g_app, 0, sizeof(g_app)); g_app.mid_counter = 1;
    
    g_app.conn_capacity = 20;
    g_app.connections = (ConnectionProfile FAR *)_fmalloc(g_app.conn_capacity * sizeof(ConnectionProfile));
    g_app.remote_items = (DirectoryItem FAR *)_fmalloc(MAX_ITEMS * sizeof(DirectoryItem));
    g_app.local_items = (DirectoryItem FAR *)_fmalloc(MAX_ITEMS * sizeof(DirectoryItem));
    
    if (!g_app.connections || !g_app.remote_items || !g_app.local_items) { MessageBox(NULL, "Out of memory!", "Error", MB_OK); return 0; }
    _fmemset(g_app.connections, 0, g_app.conn_capacity * sizeof(ConnectionProfile));
    _fmemset(g_app.remote_items, 0, MAX_ITEMS * sizeof(DirectoryItem));
    _fmemset(g_app.local_items, 0, MAX_ITEMS * sizeof(DirectoryItem));
    
    if (!hPrev) {
        _fmemset(&wc, 0, sizeof(WNDCLASS));
        wc.style = 0;
        wc.lpfnWndProc = MainWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "Smb2cClass";
        if (!RegisterClass(&wc)) return FALSE;
        
        wc.lpfnWndProc = EditConnWndProc;
        wc.lpszClassName = "EditConnClass";
        if (!RegisterClass(&wc)) return FALSE;
        
        wc.lpfnWndProc = CreateDirWndProc;
        wc.lpszClassName = "CreateDirClass";
        if (!RegisterClass(&wc)) return FALSE;

        wc.lpfnWndProc = RenameWndProc;
        wc.lpszClassName = "RenameClass";
        if (!RegisterClass(&wc)) return FALSE;
    }
    
    g_app.hMain = CreateWindow("Smb2cClass", "Win16 SMB Client", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 725, 550, 
        NULL, NULL, hInst, NULL);
        
    ShowWindow(g_app.hMain, nCmdShow);
    UpdateWindow(g_app.hMain);
    
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg); DispatchMessage(&msg); 
    }
    
    WSACleanup();
    return msg.wParam;
}