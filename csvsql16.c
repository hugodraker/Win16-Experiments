/* ============================================================================
 * CSV SQL - 16-Bit In-Memory Mutator (Open Watcom Win16)
 *
 * COMPILATION INSTRUCTIONS (Open Watcom C/C++):
 *   wcl -ml -bcl=windows -Os -s -k16k -fe=csvsql.exe csvsql16.c winsock.lib commdlg.lib
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <winsock.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef WM_USER
#define WM_USER 0x0400
#endif

#ifndef WM_SOCKET
#define WM_SOCKET (WM_USER + 2)
#endif

#ifndef MAKEWORD
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#endif

#ifndef MB_ICONERROR
#define MB_ICONERROR 0x0010
#endif

/* --- Resource IDs --- */
#define ID_EDIT_QUERY       101
#define ID_BTN_RUN          102
#define ID_BTN_OPEN         103
#define ID_LIST_TABLES      104
#define ID_STATUSBAR        106
#define ID_BTN_CLEAR        107
#define ID_COMBO_HISTORY    108
#define ID_COMBO_SERVER     109
#define ID_BTN_CONNECT      110
#define ID_BTN_COPY         111
#define ID_BTN_PASTE        112
#define ID_EDIT_OUTPUT      113

/* --- Constants --- */
#define MAX_TABLES            16
#define MAX_COLUMNS           32
#define MAX_COLUMN_NAME       32
#define QUERY_BUFFER_SIZE     4096
#define MAX_CELL_SIZE         1024
#define MAX_OUTPUT_SIZE       16384
#define MAX_CONCURRENT_CLIENTS 10
#define SERVER_PORT_DEFAULT   4040
#define TELNET_PORT_DEFAULT   23

#define COLOR_BG_LIGHT        RGB(255, 255, 255)

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */
typedef struct {
    char name[MAX_COLUMN_NAME];
    char* filename;
    char delim;
    char** columns;
    int num_columns;
    char*** rows;
    int num_rows;
    int capacity_rows;
    int column_widths[MAX_COLUMNS];
    char* def_vals[MAX_COLUMNS];
    int is_pk[MAX_COLUMNS];
    char* fk_table[MAX_COLUMNS];
    char* fk_col[MAX_COLUMNS];
} Table;

typedef enum {
    TK_EOF, TK_IDENT, TK_STR, TK_NUM,
    TK_SELECT, TK_FROM, TK_WHERE, TK_INSERT, TK_INTO, TK_VALUES,
    TK_UPDATE, TK_SET, TK_DELETE, TK_CREATE, TK_TABLE, TK_DROP,
    TK_JOIN, TK_ON, TK_AND, TK_OR,
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_COMMA, TK_LPAREN, TK_RPAREN, TK_STAR, TK_SEMI,
    TK_DEFAULT, TK_PRIMARY, TK_KEY, TK_FOREIGN, TK_REFERENCES
} TokenKind;

typedef struct { TokenKind kind; char value[512]; } Token;
typedef enum { OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_AND, OP_OR, VAL_STR, VAL_IDENT } ExprKind;
typedef struct ExprNode { ExprKind kind; char* value; struct ExprNode* left; struct ExprNode* right; } ExprNode;
typedef enum { STMT_SELECT, STMT_INSERT, STMT_UPDATE, STMT_DELETE, STMT_CREATE, STMT_DROP } StmtKind;

typedef struct {
    StmtKind kind;
    char table[MAX_COLUMN_NAME];
    char** cols; int num_cols;
    char** vals; int num_vals;
    char** defs;
    int* pks;
    char** fk_tbls; char** fk_cols;
    ExprNode* where;
    char join_table[MAX_COLUMN_NAME];
    ExprNode* join_cond;
} SQLStmt;

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */
static Table tables[MAX_TABLES];
static int num_tables = 0;
static int current_table_idx = -1;
static char* dropped_files[MAX_TABLES];
static int num_dropped_files = 0;
static HINSTANCE hInstGlobal = NULL;

static HWND hMainWnd, hEditQuery, hEditOutput, hListTables, hComboHistory;
static HWND hComboServer, hBtnConnect, hBtnRun, hBtnClear, hBtnCopy, hBtnPaste, hBtnOpen, hStatusBar;
static HFONT hFontFixed, hFontNormal;
static HBRUSH hBrushBg;

static char startup_tables[2048] = "";
static char startup_query[QUERY_BUFFER_SIZE] = "";
static char ui_status_msg[256] = "";

static const char* lex_ptr;
static Token curr_tok;

/* --- Telnet server state --- */
static int telnet_enabled = 0;
static int telnet_plaintext = 0;
static int telnet_port = TELNET_PORT_DEFAULT;
static int telnet_timeout = 20;
static char telnet_magic[128] = "\xA6";
static char telnet_password[128] = "admin";
static SOCKET hListenSock = INVALID_SOCKET;
static SOCKET server_clients[MAX_CONCURRENT_CLIENTS];
static int server_client_count = 0;

/* --- Telnet client state --- */
static SOCKET client_sock = INVALID_SOCKET;
static int is_connected = 0;
static int client_userid = 1;
static char client_password[128] = "admin";

/* --- Edit subclass --- */
static WNDPROC OldEditProc = NULL;

/* ============================================================================
 * PROTOTYPES
 * ============================================================================ */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM);

void InitApplication(void);
ATOM RegisterAppClass(HINSTANCE);
BOOL CreateMainWindow(HINSTANCE, int);
void ProcessStartup(void);

void SyncUI(const char* status, const char* result);
void AddToHistory(const char* query);
void LoadHistory(void);
void SaveHistory(void);
void SaveTablesToINI(void);
void RefreshTablesList(void);
void ResetTableData(Table* tbl);
void ClearTable(Table* tbl);
int FindTable(const char* name);
int GetColIndex(Table* tbl, const char* colname);

char* strdup_safe(const char* s);
char* trim(char* str);
char* StripQuotes(char* str);
int IsStrictNumeric(const char* s);

void CopyToClipboard(const char* text);
void PasteToEdit(HWND hEdit);
void UpdateStatusBar(const char* fmt, ...);

/* 6-bit & RLE */
unsigned char CharTo6Bit(char c);
char Bit6ToChar(unsigned char b);
int Pack6Bit(const char* in, int in_len, char* out, int out_max);
int Unpack6Bit(const char* in, int in_len, char* out, int out_max);
int CompressRLE(const char* in, int in_len, char* out, int out_max);
int DecompressRLE(const char* in, int in_len, char* out, int out_max);

/* Parser */
void NextToken(void);
int Match(TokenKind kind);
int MatchIdent(char* out_name);
ExprNode* MakeNode(ExprKind k, ExprNode* l, ExprNode* r, const char* v);
void FreeAST(ExprNode* n);
ExprNode* ParseOr(void);
ExprNode* ParseAnd(void);
ExprNode* ParseCmp(void);
ExprNode* ParsePrimary(void);
SQLStmt* ParseStmt(void);
void FreeStmt(SQLStmt* s);

/* Executor */
char* GetASTValJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2);
int EvalExprJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2);
void ExecuteAST(SQLStmt* s, char* out_buf, size_t out_max);
void ExecuteQueryEx(const char* query, char* out_buf, size_t out_max);

/* CSV */
char DetectDelimiter(const char* data);
BOOL LoadCSV(const char* filename, const char* tablename);
void SaveCSV(Table* tbl);

/* Network */
void ToggleConnection(void);
void SendToRemote(const char* query);
void HandleServerRead(SOCKET sock, char* buf, int len);
void ProcessSocketEvent(SOCKET sock, WORD event, WORD error);

/* ============================================================================
 * 6-BIT PACKING & RLE COMPRESSION ENGINE
 * ============================================================================ */
unsigned char CharTo6Bit(char c) {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A');
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0' + 52);
    if (c == ' ') return 62;
    return 63;
}

char Bit6ToChar(unsigned char b) {
    if (b < 26) return (char)('A' + b);
    if (b < 52) return (char)('a' + (b - 26));
    if (b < 62) return (char)('0' + (b - 52));
    if (b == 62) return ' ';
    return '\0';
}

int Pack6Bit(const char* in, int in_len, char* out, int out_max) {
    unsigned long bit_buf = 0;
    int bit_len = 0, o = 0, i;
    unsigned char c, b6;
    for (i = 0; i < in_len; i++) {
        c = (unsigned char)in[i];
        b6 = CharTo6Bit(c);
        bit_buf = (bit_buf << 6) | b6;
        bit_len += 6;
        if (b6 == 63) { bit_buf = (bit_buf << 8) | c; bit_len += 8; }
        while (bit_len >= 8) {
            if (o >= out_max) return -1;
            bit_len -= 8;
            out[o++] = (char)((bit_buf >> bit_len) & 0xFF);
        }
    }
    if (bit_len > 0) {
        if (o >= out_max) return -1;
        out[o++] = (char)((bit_buf << (8 - bit_len)) & 0xFF);
    }
    return o;
}

int Unpack6Bit(const char* in, int in_len, char* out, int out_max) {
    unsigned long bit_buf = 0;
    int bit_len = 0, i = 0, o = 0;
    unsigned char b6, raw;
    while (i < in_len || bit_len >= 6) {
        while (bit_len < 14 && i < in_len) { bit_buf = (bit_buf << 8) | ((unsigned char)in[i++]); bit_len += 8; }
        if (bit_len < 6) break;
        b6 = (unsigned char)((bit_buf >> (bit_len - 6)) & 0x3F);
        bit_len -= 6;
        if (b6 == 63) {
            if (bit_len < 8) {
                if (i < in_len) { bit_buf = (bit_buf << 8) | ((unsigned char)in[i++]); bit_len += 8; }
                else break;
            }
            raw = (unsigned char)((bit_buf >> (bit_len - 8)) & 0xFF);
            bit_len -= 8;
            if (o < out_max) out[o++] = (char)raw;
            if (raw == '\0') break;
        } else {
            if (o < out_max) out[o++] = Bit6ToChar(b6);
        }
    }
    if (o < out_max) out[o] = '\0';
    return o;
}

int CompressRLE(const char* in, int in_len, char* out, int out_max) {
    int i = 0, o = 0, run, j;
    while (i < in_len && o < out_max - 4) {
        run = 1;
        while (i + run < in_len && in[i + run] == in[i] && run < 255) run++;
        if (run >= 3 || in[i] == '\x1B') {
            out[o++] = '\x1B'; out[o++] = (char)run; out[o++] = in[i];
            i += run;
        } else {
            for (j = 0; j < run; j++) out[o++] = in[i];
            i += run;
        }
    }
    return o;
}

int DecompressRLE(const char* in, int in_len, char* out, int out_max) {
    int i = 0, o = 0;
    unsigned char run;
    char val;
    int j;
    while (i < in_len && o < out_max - 1) {
        if (in[i] == '\x1B') {
            if (i + 2 >= in_len) break;
            run = (unsigned char)in[i+1];
            val = in[i+2];
            for (j = 0; j < run && o < out_max - 1; j++) out[o++] = val;
            i += 3;
        } else { out[o++] = in[i++]; }
    }
    out[o] = '\0';
    return o;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */
char* trim(char* str) {
    char* end;
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str) { end = str + strlen(str) - 1; while (end > str && isspace((unsigned char)*end)) *end-- = '\0'; }
    return str;
}

char* strdup_safe(const char* s) {
    size_t len; char* dup;
    if (!s) return NULL;
    len = strlen(s) + 1;
    dup = (char*)malloc(len);
    return dup ? (char*)memcpy(dup, s, len) : NULL;
}

char* StripQuotes(char* str) {
    size_t len;
    str = trim(str);
    len = strlen(str);
    if (len >= 2 && ((str[0] == '\'' && str[len-1] == '\'') || (str[0] == '"' && str[len-1] == '"'))) {
        str[len-1] = '\0'; str++;
    }
    return str;
}

int IsStrictNumeric(const char* s) {
    int has_dot = 0, has_digit = 0;
    if (!s || !*s) return 0;
    if (*s == '-') s++;
    while (*s) {
        if (*s == '.') { if (has_dot) return 0; has_dot = 1; }
        else if (isdigit((unsigned char)*s)) has_digit = 1;
        else return 0;
        s++;
    }
    return has_digit;
}

int FindTable(const char* name) {
    int i;
    for (i = 0; i < num_tables; i++)
        if (_stricmp(tables[i].name, name) == 0) return i;
    return -1;
}

int GetColIndex(Table* tbl, const char* colname) {
    int i;
    for (i = 0; i < tbl->num_columns; i++)
        if (_stricmp(tbl->columns[i], colname) == 0) return i;
    return -1;
}

void ResetTableData(Table* tbl) {
    int r, c;
    if (tbl->columns) {
        for (c = 0; c < tbl->num_columns; c++) free(tbl->columns[c]);
        free(tbl->columns); tbl->columns = NULL;
    }
    if (tbl->rows) {
        for (r = 0; r < tbl->num_rows; r++) {
            for (c = 0; c < tbl->num_columns; c++) free(tbl->rows[r][c]);
            free(tbl->rows[r]);
        }
        free(tbl->rows); tbl->rows = NULL;
    }
    for (c = 0; c < MAX_COLUMNS; c++) { 
        if (tbl->def_vals[c]) { free(tbl->def_vals[c]); tbl->def_vals[c] = NULL; } 
        if (tbl->fk_table[c]) { free(tbl->fk_table[c]); tbl->fk_table[c] = NULL; }
        if (tbl->fk_col[c])   { free(tbl->fk_col[c]);   tbl->fk_col[c]   = NULL; }
    }
    tbl->num_columns = 0; tbl->num_rows = 0; tbl->capacity_rows = 0;
}

void ClearTable(Table* tbl) {
    ResetTableData(tbl);
    if (tbl->filename) { free(tbl->filename); tbl->filename = NULL; }
    memset(tbl, 0, sizeof(Table));
}

/* ============================================================================
 * HISTORY MANAGEMENT
 * ============================================================================ */
void LoadHistory(void) {
    FILE* fp;
    char* line = (char*)malloc(QUERY_BUFFER_SIZE);
    char* trimmed;
    fp = fopen("queries.csv", "r");
    if (!fp || !line) { if (line) free(line); return; }
    while (fgets(line, QUERY_BUFFER_SIZE, fp)) {
        trimmed = trim(line);
        if (strlen(trimmed) > 0) SendMessage(hComboHistory, CB_ADDSTRING, 0, (LPARAM)trimmed);
    }
    fclose(fp);
    free(line);
}

void SaveHistory(void) {
    FILE* fp;
    int count;
    int i;
    char* buf = (char*)malloc(QUERY_BUFFER_SIZE);
    if (!buf) return;
    fp = fopen("queries.csv", "w");
    if (!fp) { free(buf); return; }
    count = SendMessage(hComboHistory, CB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) {
        SendMessage(hComboHistory, CB_GETLBTEXT, i, (LPARAM)buf);
        fprintf(fp, "%s\n", buf);
    }
    fclose(fp);
    free(buf);
}

void AddToHistory(const char* query) {
    char* buf = (char*)malloc(QUERY_BUFFER_SIZE);
    int i;
    char* trimmed;
    if (!buf) return;
    strncpy(buf, query, QUERY_BUFFER_SIZE - 1);
    buf[QUERY_BUFFER_SIZE - 1] = '\0';
    for (i = 0; buf[i]; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') buf[i] = ' ';
    }
    trimmed = trim(buf);
    if (strlen(trimmed) > 0 && SendMessage(hComboHistory, CB_FINDSTRINGEXACT, -1, (LPARAM)trimmed) == CB_ERR) {
        SendMessage(hComboHistory, CB_ADDSTRING, 0, (LPARAM)trimmed);
    }
    free(buf);
}

/* ============================================================================
 * INI PERSISTENCE
 * ============================================================================ */
void SaveTablesToINI(void) {
    char ini_path[MAX_PATH];
    char* ext;
    char* buf = (char*)calloc(2048, 1);
    int i;
    if (!buf) return;
    GetModuleFileName(hInstGlobal, ini_path, MAX_PATH);
    ext = strrchr(ini_path, '.');
    if (ext) strcpy(ext, ".INI"); else strcat(ini_path, ".INI");
    for (i = 0; i < num_tables; i++) {
        if (strcmp(tables[i].name, "_results_") == 0) continue;
        if (strlen(buf) > 0) strcat(buf, ", ");
        strcat(buf, tables[i].name);
    }
    WritePrivateProfileString("Client", "Tables", buf, ini_path);
    free(buf);
}

void RefreshTablesList(void) {
    int i, count = num_tables, cur_idx = current_table_idx;
    SendMessage(hListTables, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < count; i++) {
        char buf[256];
        sprintf(buf, "[%s] %d cols / %d rows", tables[i].name, tables[i].num_columns, tables[i].num_rows);
        SendMessage(hListTables, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    if (cur_idx >= 0 && cur_idx < count)
        SendMessage(hListTables, LB_SETCURSEL, cur_idx, 0);
}

/* ============================================================================
 * CLIPBOARD
 * ============================================================================ */
void CopyToClipboard(const char* text) {
    size_t len;
    HGLOBAL hMem;
    if (!OpenClipboard(hMainWnd)) return;
    EmptyClipboard();
    len = strlen(text) + 1;
    hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMem) {
        memcpy(GlobalLock(hMem), text, len);
        GlobalUnlock(hMem);
        SetClipboardData(CF_TEXT, hMem);
    }
    CloseClipboard();
}

void PasteToEdit(HWND hEdit) {
    HGLOBAL hMem;
    char* text;
    if (!IsClipboardFormatAvailable(CF_TEXT)) return;
    if (!OpenClipboard(hMainWnd)) return;
    hMem = GetClipboardData(CF_TEXT);
    if (hMem) {
        text = (char*)GlobalLock(hMem);
        if (text) {
            SetWindowText(hEdit, "");
            SendMessage(hEdit, EM_REPLACESEL, TRUE, (LPARAM)text);
            GlobalUnlock(hMem);
        }
    }
    CloseClipboard();
}

void UpdateStatusBar(const char* fmt, ...) {
    char f[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(f, fmt, args);
    va_end(args);
    SetWindowText(hStatusBar, f);
}

/* ============================================================================
 * SYNC UI
 * ============================================================================ */
void SyncUI(const char* status, const char* result) {
    if (status) {
        strncpy(ui_status_msg, status, sizeof(ui_status_msg) - 1);
        ui_status_msg[sizeof(ui_status_msg) - 1] = '\0';
        UpdateStatusBar("%s", ui_status_msg);
    }
    if (result) SetWindowText(hEditOutput, result);
    RefreshTablesList();
}

/* ============================================================================
 * LEXER & PARSER
 * ============================================================================ */
void NextToken(void) {
    char quote;
    int i = 0, j;
    char up[512];
    while (*lex_ptr && isspace((unsigned char)*lex_ptr)) lex_ptr++;
    curr_tok.value[0] = '\0'; curr_tok.kind = TK_EOF;
    if (!*lex_ptr) return;

    if (*lex_ptr == ';') { curr_tok.kind = TK_SEMI; curr_tok.value[0] = ';'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == ',') { curr_tok.kind = TK_COMMA; curr_tok.value[0] = ','; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '(') { curr_tok.kind = TK_LPAREN; curr_tok.value[0] = '('; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == ')') { curr_tok.kind = TK_RPAREN; curr_tok.value[0] = ')'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '*') { curr_tok.kind = TK_STAR; curr_tok.value[0] = '*'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '=') { curr_tok.kind = TK_EQ; curr_tok.value[0] = '='; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '!' && *(lex_ptr+1) == '=') { curr_tok.kind = TK_NEQ; strcpy(curr_tok.value, "!="); lex_ptr += 2; return; }
    if (*lex_ptr == '<') { curr_tok.kind = TK_LT; curr_tok.value[0] = '<'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '>') { curr_tok.kind = TK_GT; curr_tok.value[0] = '>'; curr_tok.value[1] = '\0'; lex_ptr++; return; }

    if (*lex_ptr == '\'' || *lex_ptr == '"') {
        quote = *lex_ptr++;
        i = 0;
        while (*lex_ptr && *lex_ptr != quote && i < 510) curr_tok.value[i++] = *lex_ptr++;
        if (*lex_ptr == quote) lex_ptr++;
        curr_tok.value[i] = '\0'; curr_tok.kind = TK_STR;
        return;
    }

    if (isalpha((unsigned char)*lex_ptr) || *lex_ptr == '_') {
        i = 0;
        while (isalnum((unsigned char)*lex_ptr) || *lex_ptr == '_') { if (i < 510) curr_tok.value[i++] = *lex_ptr; lex_ptr++; }
        curr_tok.value[i] = '\0';
        for (j = 0; j <= i; j++) up[j] = (char)toupper((unsigned char)curr_tok.value[j]);

        if (!strcmp(up, "SELECT")) curr_tok.kind = TK_SELECT;
        else if (!strcmp(up, "FROM")) curr_tok.kind = TK_FROM;
        else if (!strcmp(up, "WHERE")) curr_tok.kind = TK_WHERE;
        else if (!strcmp(up, "INSERT")) curr_tok.kind = TK_INSERT;
        else if (!strcmp(up, "INTO")) curr_tok.kind = TK_INTO;
        else if (!strcmp(up, "VALUES")) curr_tok.kind = TK_VALUES;
        else if (!strcmp(up, "UPDATE")) curr_tok.kind = TK_UPDATE;
        else if (!strcmp(up, "SET")) curr_tok.kind = TK_SET;
        else if (!strcmp(up, "DELETE")) curr_tok.kind = TK_DELETE;
        else if (!strcmp(up, "CREATE")) curr_tok.kind = TK_CREATE;
        else if (!strcmp(up, "TABLE")) curr_tok.kind = TK_TABLE;
        else if (!strcmp(up, "DROP")) curr_tok.kind = TK_DROP;
        else if (!strcmp(up, "JOIN") || !strcmp(up, "INNER") || !strcmp(up, "LEFT")) curr_tok.kind = TK_JOIN;
        else if (!strcmp(up, "ON")) curr_tok.kind = TK_ON;
        else if (!strcmp(up, "AND")) curr_tok.kind = TK_AND;
        else if (!strcmp(up, "OR")) curr_tok.kind = TK_OR;
        else if (!strcmp(up, "DEFAULT")) curr_tok.kind = TK_DEFAULT;
        else if (!strcmp(up, "PRIMARY")) curr_tok.kind = TK_PRIMARY;
        else if (!strcmp(up, "KEY")) curr_tok.kind = TK_KEY;
        else if (!strcmp(up, "FOREIGN")) curr_tok.kind = TK_FOREIGN;
        else if (!strcmp(up, "REFERENCES")) curr_tok.kind = TK_REFERENCES;
        else curr_tok.kind = TK_IDENT;
        return;
    }

    if (isdigit((unsigned char)*lex_ptr) || (*lex_ptr == '-' && isdigit((unsigned char)*(lex_ptr+1)))) {
        i = 0;
        if (*lex_ptr == '-') { curr_tok.value[i++] = *lex_ptr++; }
        while (isdigit((unsigned char)*lex_ptr) || *lex_ptr == '.') { if (i < 510) curr_tok.value[i++] = *lex_ptr; lex_ptr++; }
        curr_tok.value[i] = '\0'; curr_tok.kind = TK_NUM; return;
    }

    curr_tok.kind = TK_IDENT; curr_tok.value[0] = *lex_ptr++; curr_tok.value[1] = '\0';
}

int Match(TokenKind kind) { if (curr_tok.kind == kind) { NextToken(); return 1; } return 0; }

int MatchIdent(char* out_name) {
    if (curr_tok.kind == TK_IDENT || curr_tok.kind == TK_STR ||
        (curr_tok.kind >= TK_SELECT && curr_tok.kind <= TK_REFERENCES)) {
        strncpy(out_name, curr_tok.value, MAX_COLUMN_NAME - 1);
        out_name[MAX_COLUMN_NAME - 1] = '\0';
        NextToken();
        return 1;
    }
    return 0;
}

/* ============================================================================
 * AST HELPERS
 * ============================================================================ */
ExprNode* MakeNode(ExprKind k, ExprNode* l, ExprNode* r, const char* v) {
    ExprNode* n = (ExprNode*)malloc(sizeof(ExprNode));
    n->kind = k; n->left = l; n->right = r;
    n->value = v ? strdup_safe(v) : NULL;
    return n;
}

void FreeAST(ExprNode* n) {
    if (!n) return;
    FreeAST(n->left); FreeAST(n->right);
    free(n->value); free(n);
}

/* ============================================================================
 * EXPRESSION PARSER (AND/OR)
 * ============================================================================ */
ExprNode* ParsePrimary(void) {
    ExprNode* n = NULL; char ident[512];
    if (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM) {
        n = MakeNode(VAL_STR, NULL, NULL, curr_tok.value); NextToken();
    } else if (Match(TK_LPAREN)) {
        n = ParseOr(); Match(TK_RPAREN);
    } else if (MatchIdent(ident)) {
        n = MakeNode(VAL_IDENT, NULL, NULL, ident);
    }
    return n;
}

ExprNode* ParseCmp(void) {
    ExprNode* n = ParsePrimary();
    while (curr_tok.kind == TK_EQ || curr_tok.kind == TK_NEQ ||
           curr_tok.kind == TK_LT || curr_tok.kind == TK_GT) {
        ExprKind op = OP_EQ;
        if (curr_tok.kind == TK_NEQ) op = OP_NEQ;
        if (curr_tok.kind == TK_LT) op = OP_LT;
        if (curr_tok.kind == TK_GT) op = OP_GT;
        NextToken();
        n = MakeNode(op, n, ParsePrimary(), NULL);
    }
    return n;
}

ExprNode* ParseAnd(void) {
    ExprNode* n = ParseCmp();
    while (Match(TK_AND)) n = MakeNode(OP_AND, n, ParseCmp(), NULL);
    return n;
}

ExprNode* ParseOr(void) {
    ExprNode* n = ParseAnd();
    while (Match(TK_OR)) n = MakeNode(OP_OR, n, ParseAnd(), NULL);
    return n;
}

/* ============================================================================
 * STATEMENT PARSER
 * ============================================================================ */
SQLStmt* ParseStmt(void) {
    SQLStmt* stmt;
    int matched = 0;
    char col[512];

    while (curr_tok.kind == TK_SEMI) NextToken();
    if (curr_tok.kind == TK_EOF) return NULL;

    stmt = (SQLStmt*)calloc(1, sizeof(SQLStmt));
    if (Match(TK_SELECT)) {
        stmt->kind = STMT_SELECT;
        stmt->cols = (char**)calloc(MAX_COLUMNS, sizeof(char*));
        if (Match(TK_STAR)) {
            stmt->cols[stmt->num_cols++] = strdup_safe("*");
        } else {
            while (MatchIdent(col)) {
                if (stmt->num_cols < MAX_COLUMNS) stmt->cols[stmt->num_cols++] = strdup_safe(col);
                if (!Match(TK_COMMA)) break;
            }
        }
        if (Match(TK_FROM) && MatchIdent(stmt->table)) {
            if (Match(TK_JOIN) && MatchIdent(stmt->join_table)) {
                if (Match(TK_ON)) stmt->join_cond = ParseOr();
            }
            if (Match(TK_WHERE)) stmt->where = ParseOr();
            matched = 1;
        }
    }
    else if (Match(TK_INSERT) && Match(TK_INTO) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_INSERT;
        if (Match(TK_VALUES) && Match(TK_LPAREN)) {
            stmt->vals = (char**)calloc(MAX_COLUMNS, sizeof(char*));
            while (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM) {
                if (stmt->num_vals < MAX_COLUMNS) stmt->vals[stmt->num_vals++] = strdup_safe(curr_tok.value);
                NextToken();
                if (!Match(TK_COMMA)) break;
            }
            if (Match(TK_RPAREN)) matched = 1;
        }
    }
    else if (Match(TK_UPDATE) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_UPDATE;
        if (Match(TK_SET)) {
            stmt->cols = (char**)calloc(2, sizeof(char*));
            stmt->vals = (char**)calloc(2, sizeof(char*));
            if (MatchIdent(col)) {
                stmt->cols[0] = strdup_safe(col);
                if (Match(TK_EQ) && (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM)) {
                    stmt->vals[0] = strdup_safe(curr_tok.value); NextToken();
                    stmt->num_cols = 1; stmt->num_vals = 1; matched = 1;
                }
                if (Match(TK_WHERE)) stmt->where = ParseOr();
            }
        }
    }
    else if (Match(TK_DELETE) && Match(TK_FROM) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_DELETE; matched = 1;
        if (Match(TK_WHERE)) stmt->where = ParseOr();
    }
    else if (Match(TK_CREATE) && Match(TK_TABLE) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_CREATE;
        if (Match(TK_LPAREN)) {
            stmt->cols = (char**)calloc(MAX_COLUMNS, sizeof(char*));
            stmt->defs = (char**)calloc(MAX_COLUMNS, sizeof(char*));
            stmt->pks = (int*)calloc(MAX_COLUMNS, sizeof(int));
            stmt->fk_tbls = (char**)calloc(MAX_COLUMNS, sizeof(char*));
            stmt->fk_cols = (char**)calloc(MAX_COLUMNS, sizeof(char*));
            while (curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) {
                if (curr_tok.kind == TK_IDENT &&
                    (_stricmp(curr_tok.value, "PRIMARY") == 0 ||
                     _stricmp(curr_tok.value, "FOREIGN") == 0 ||
                     _stricmp(curr_tok.value, "UNIQUE") == 0 ||
                     _stricmp(curr_tok.value, "CONSTRAINT") == 0 ||
                     _stricmp(curr_tok.value, "CHECK") == 0)) {
                    while (curr_tok.kind != TK_COMMA && curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) NextToken();
                } else if (MatchIdent(col)) {
                    if (stmt->num_cols < MAX_COLUMNS) {
                        int cidx = stmt->num_cols++;
                        stmt->cols[cidx] = strdup_safe(col);
                        while (curr_tok.kind != TK_COMMA && curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) {
                            if (Match(TK_DEFAULT) && (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM)) {
                                stmt->defs[cidx] = strdup_safe(curr_tok.value); NextToken();
                            } else if (Match(TK_PRIMARY) && Match(TK_KEY)) {
                                stmt->pks[cidx] = 1;
                            } else if (Match(TK_REFERENCES) && MatchIdent(col)) {
                                stmt->fk_tbls[cidx] = strdup_safe(col);
                                if (Match(TK_LPAREN) && MatchIdent(col)) { stmt->fk_cols[cidx] = strdup_safe(col); Match(TK_RPAREN); }
                            } else NextToken();
                        }
                    }
                } else NextToken();
                if (Match(TK_COMMA)) continue; else break;
            }
            if (Match(TK_RPAREN)) matched = 1;
        }
    }
    else if (Match(TK_DROP) && Match(TK_TABLE) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_DROP; matched = 1;
    }

    if (!matched) {
        free(stmt);
        while (curr_tok.kind != TK_EOF && curr_tok.kind != TK_SEMI) NextToken();
        if (curr_tok.kind == TK_SEMI) NextToken();
        return NULL;
    }
    while (curr_tok.kind != TK_EOF && curr_tok.kind != TK_SEMI) NextToken();
    if (curr_tok.kind == TK_SEMI) NextToken();
    return stmt;
}

void FreeStmt(SQLStmt* s) {
    int i;
    if (!s) return;
    if (s->cols) { for (i = 0; i < s->num_cols; i++) free(s->cols[i]); free(s->cols); }
    if (s->vals) { for (i = 0; i < s->num_vals; i++) free(s->vals[i]); free(s->vals); }
    if (s->defs) { for (i = 0; i < MAX_COLUMNS; i++) free(s->defs[i]); free(s->defs); }
    if (s->pks) free(s->pks);
    if (s->fk_tbls) { for (i = 0; i < MAX_COLUMNS; i++) free(s->fk_tbls[i]); free(s->fk_tbls); }
    if (s->fk_cols) { for (i = 0; i < MAX_COLUMNS; i++) free(s->fk_cols[i]); free(s->fk_cols); }
    FreeAST(s->where); FreeAST(s->join_cond);
    free(s);
}

/* ============================================================================
 * EXPRESSION EVALUATOR (supports 2-table JOIN)
 * ============================================================================ */
char* GetASTValJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2) {
    char* dot;
    char tname[256];
    int tlen;
    char* cname;
    int c;

    if (!n) return NULL;
    if (n->kind == VAL_STR) return n->value;
    if (n->kind == VAL_IDENT) {
        dot = strchr(n->value, '.');
        if (dot) {
            tlen = (int)(dot - n->value);
            if (tlen >= 256) tlen = 255;
            memcpy(tname, n->value, tlen); tname[tlen] = '\0';
            cname = dot + 1;
            if (_stricmp(tname, t1->name) == 0) {
                c = GetColIndex(t1, cname);
                if (c >= 0 && r1 < t1->num_rows) return t1->rows[r1][c];
            } else if (t2 && _stricmp(tname, t2->name) == 0) {
                c = GetColIndex(t2, cname);
                if (c >= 0 && r2 < t2->num_rows) return t2->rows[r2][c];
            }
        } else {
            c = GetColIndex(t1, n->value);
            if (c >= 0 && r1 < t1->num_rows) return t1->rows[r1][c];
            if (t2) {
                c = GetColIndex(t2, n->value);
                if (c >= 0 && r2 < t2->num_rows) return t2->rows[r2][c];
            }
        }
    }
    return NULL;
}

int EvalExprJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2) {
    char *vL, *vR;
    int isNum;
    double numL, numR;

    if (!n) return 1;
    if (n->kind == OP_AND) return EvalExprJoin(n->left, t1, r1, t2, r2) && EvalExprJoin(n->right, t1, r1, t2, r2);
    if (n->kind == OP_OR)  return EvalExprJoin(n->left, t1, r1, t2, r2) || EvalExprJoin(n->right, t1, r1, t2, r2);

    vL = GetASTValJoin(n->left, t1, r1, t2, r2);
    vR = GetASTValJoin(n->right, t1, r1, t2, r2);
    if (!vL) vL = ""; if (!vR) vR = "";

    isNum = IsStrictNumeric(vL) && IsStrictNumeric(vR);
    numL = isNum ? atof(vL) : 0;
    numR = isNum ? atof(vR) : 0;

    if (n->kind == OP_EQ)  return _stricmp(vL, vR) == 0;
    if (n->kind == OP_NEQ) return _stricmp(vL, vR) != 0;
    if (n->kind == OP_LT)  return isNum ? (numL < numR) : (_stricmp(vL, vR) < 0);
    if (n->kind == OP_GT)  return isNum ? (numL > numR) : (_stricmp(vL, vR) > 0);
    return 0;
}

/* ============================================================================
 * STATEMENT EXECUTOR
 * ============================================================================ */
void ExecuteAST(SQLStmt* s, char* out_buf, size_t out_max) {
    int i, r, c, k, idx, affected;
    size_t pos;
    Table* tbl;
    char tmp_buf[2048];

    if (!s || !out_buf || out_max == 0) return;
    pos = strlen(out_buf);

    #define EMIT(...) do { \
        int emit_len; \
        sprintf(tmp_buf, __VA_ARGS__); \
        emit_len = (int)strlen(tmp_buf); \
        if (pos + (size_t)emit_len < out_max - 1) { \
            strcpy(out_buf + pos, tmp_buf); \
            pos += (size_t)emit_len; \
        } \
    } while(0)

    if (s->kind == STMT_CREATE) {
        if (num_tables < MAX_TABLES) {
            char fn[256];
            tbl = &tables[num_tables++];
            memset(tbl, 0, sizeof(Table));
            strncpy(tbl->name, s->table, sizeof(tbl->name) - 1);
            sprintf(fn, "%s.csv", s->table);
            tbl->filename = strdup_safe(fn);
            tbl->delim = ',';
            tbl->num_columns = s->num_cols;
            tbl->columns = (char**)calloc(MAX_COLUMNS, sizeof(char*));
            for (i = 0; i < s->num_cols && i < MAX_COLUMNS; i++) {
                tbl->columns[i] = strdup_safe(s->cols[i]);
                tbl->column_widths[i] = (int)strlen(s->cols[i]);
                if (s->defs && s->defs[i]) tbl->def_vals[i] = strdup_safe(s->defs[i]);
                if (s->pks && s->pks[i]) tbl->is_pk[i] = 1;
                if (s->fk_tbls && s->fk_tbls[i]) {
                    tbl->fk_table[i] = strdup_safe(s->fk_tbls[i]);
                    if (s->fk_cols && s->fk_cols[i]) tbl->fk_col[i] = strdup_safe(s->fk_cols[i]);
                }
            }
            tbl->capacity_rows = 100;
            tbl->rows = (char***)malloc(tbl->capacity_rows * sizeof(char**));
            current_table_idx = num_tables - 1;
            SaveTablesToINI();
            EMIT("Created table '%s'.\r\n", s->table);
        } else EMIT("Error: Table limit reached.\r\n");
    }

    else if (s->kind == STMT_DROP) {
        idx = FindTable(s->table);
        if (idx >= 0) {
            dropped_files[num_dropped_files++] = strdup_safe(tables[idx].filename);
            ResetTableData(&tables[idx]);
            if (tables[idx].filename) { free(tables[idx].filename); tables[idx].filename = NULL; }
            for (i = idx; i < num_tables - 1; i++) tables[i] = tables[i + 1];
            num_tables--;
            current_table_idx = num_tables > 0 ? 0 : -1;
            SaveTablesToINI();
            EMIT("Dropped table '%s'.\r\n", s->table);
        } else EMIT("Error: Table '%s' not found.\r\n", s->table);
    }

    else if (s->kind == STMT_INSERT) {
        idx = FindTable(s->table);
        if (idx < 0 && num_tables < MAX_TABLES) {
            char fn[256];
            tbl = &tables[num_tables];
            memset(tbl, 0, sizeof(Table));
            strncpy(tbl->name, s->table, sizeof(tbl->name) - 1);
            sprintf(fn, "%s.csv", s->table);
            tbl->filename = strdup_safe(fn);
            tbl->delim = ',';
            tbl->num_columns = s->num_vals;
            tbl->columns = (char**)calloc(MAX_COLUMNS, sizeof(char*));
            for (i = 0; i < s->num_vals && i < MAX_COLUMNS; i++) {
                char col[32];
                sprintf(col, "Col%d", i + 1);
                tbl->columns[i] = strdup_safe(col);
                tbl->column_widths[i] = (int)strlen(col);
            }
            tbl->capacity_rows = 100;
            tbl->rows = (char***)malloc(tbl->capacity_rows * sizeof(char**));
            idx = num_tables++;
            SaveTablesToINI();
        }
        if (idx >= 0) {
            int pk_violation = 0;
            tbl = &tables[idx];
            for (i = 0; i < tbl->num_columns && i < s->num_vals; i++) {
                if (tbl->is_pk[i]) {
                    for (r = 0; r < tbl->num_rows; r++) {
                        if (tbl->rows[r][i] && _stricmp(tbl->rows[r][i], s->vals[i]) == 0) {
                            pk_violation = 1; break;
                        }
                    }
                }
                if (pk_violation) break;
            }
            if (pk_violation) {
                EMIT("Error: Primary Key violation on column '%s'.\r\n", tbl->columns[i]);
                return;
            }
            if (tbl->num_rows >= tbl->capacity_rows) {
                tbl->capacity_rows *= 2;
                tbl->rows = (char***)realloc(tbl->rows, tbl->capacity_rows * sizeof(char**));
            }
            tbl->rows[tbl->num_rows] = (char**)calloc(tbl->num_columns, sizeof(char*));
            for (i = 0; i < tbl->num_columns; i++) {
                if (i < s->num_vals) {
                    int vlen;
                    tbl->rows[tbl->num_rows][i] = strdup_safe(s->vals[i]);
                    vlen = (int)strlen(s->vals[i]); 
                    if (vlen > tbl->column_widths[i]) tbl->column_widths[i] = vlen;
                } else if (tbl->def_vals[i]) {
                    tbl->rows[tbl->num_rows][i] = strdup_safe(tbl->def_vals[i]);
                } else {
                    tbl->rows[tbl->num_rows][i] = strdup_safe("");
                }
            }
            tbl->num_rows++;
            current_table_idx = idx;
            EMIT("Inserted 1 row into '%s'.\r\n", s->table);
        } else EMIT("Error: Table limit reached.\r\n");
    }

    else if (s->kind == STMT_UPDATE) {
        idx = FindTable(s->table);
        if (idx >= 0 && s->num_cols == 1) {
            int set_col;
            tbl = &tables[idx];
            set_col = GetColIndex(tbl, s->cols[0]);
            if (set_col >= 0) {
                affected = 0;
                for (r = 0; r < tbl->num_rows; r++) {
                    if (EvalExprJoin(s->where, tbl, r, NULL, -1)) {
                        free(tbl->rows[r][set_col]);
                        tbl->rows[r][set_col] = strdup_safe(s->vals[0]);
                        affected++;
                    }
                }
                current_table_idx = idx;
                EMIT("Updated %d rows in '%s'.\r\n", affected, s->table);
            } else EMIT("Error: Column '%s' not found.\r\n", s->cols[0]);
        } else EMIT("Error: Table not found or syntax invalid.\r\n");
    }

    else if (s->kind == STMT_DELETE) {
        idx = FindTable(s->table);
        if (idx >= 0) {
            tbl = &tables[idx];
            if (!s->where) {
                EMIT("Error: Bare deletes prevented. Provide a WHERE clause.\r\n");
            } else {
                affected = 0;
                for (r = 0; r < tbl->num_rows; r++) {
                    if (EvalExprJoin(s->where, tbl, r, NULL, -1)) {
                        int restrict_del = 0;
                        for (c = 0; c < tbl->num_columns; c++) {
                            if (tbl->is_pk[c] && !restrict_del) {
                                int t_idx, fk_c, cr;
                                for (t_idx = 0; t_idx < num_tables; t_idx++) {
                                    for (fk_c = 0; fk_c < tables[t_idx].num_columns; fk_c++) {
                                        if (tables[t_idx].fk_table[fk_c] && _stricmp(tables[t_idx].fk_table[fk_c], tbl->name) == 0) {
                                            for (cr = 0; cr < tables[t_idx].num_rows; cr++) {
                                                if (tables[t_idx].rows[cr][fk_c] &&
                                                    _stricmp(tables[t_idx].rows[cr][fk_c], tbl->rows[r][c]) == 0) {
                                                    restrict_del = 1; break;
                                                }
                                            }
                                        }
                                        if (restrict_del) break;
                                    }
                                    if (restrict_del) break;
                                }
                            }
                            if (restrict_del) break;
                        }
                        if (restrict_del) {
                            EMIT("Error: Foreign Key constraint prevents deletion.\r\n");
                            return;
                        }
                        for (c = 0; c < tbl->num_columns; c++) free(tbl->rows[r][c]);
                        free(tbl->rows[r]);
                        for (k = r; k < tbl->num_rows - 1; k++) tbl->rows[k] = tbl->rows[k + 1];
                        tbl->num_rows--; r--; affected++;
                    }
                }
                current_table_idx = idx;
                EMIT("Deleted %d rows from '%s'.\r\n", affected, s->table);
            }
        } else EMIT("Error: Table '%s' not found.\r\n", s->table);
    }

    else if (s->kind == STMT_SELECT) {
        idx = FindTable(s->table);
        if (idx >= 0) {
            Table* src1 = &tables[idx];
            Table* src2 = NULL;
            int join_ok = 1;
            if (s->join_table[0] != '\0') {
                int j_idx = FindTable(s->join_table);
                if (j_idx >= 0) src2 = &tables[j_idx];
                else { EMIT("Error: Joined table '%s' not found.\r\n", s->join_table); join_ok = 0; }
            }

            if (join_ok) {
                int res_idx = FindTable("_results_");
                if (res_idx < 0 && num_tables < MAX_TABLES) {
                    res_idx = num_tables++;
                    strcpy(tables[res_idx].name, "_results_");
                    tables[res_idx].filename = NULL;
                }
                if (res_idx >= 0) {
                    Table* dst = &tables[res_idx];
                    ResetTableData(dst);
                    dst->columns = (char**)calloc(MAX_COLUMNS, sizeof(char*));
                    dst->num_columns = 0;

                    if (s->cols[0] && strcmp(s->cols[0], "*") == 0) {
                        for (c = 0; c < src1->num_columns && dst->num_columns < MAX_COLUMNS; c++) {
                            dst->columns[dst->num_columns] = strdup_safe(src1->columns[c]);
                            dst->column_widths[dst->num_columns] = src1->column_widths[c];
                            dst->num_columns++;
                        }
                        if (src2) {
                            for (c = 0; c < src2->num_columns && dst->num_columns < MAX_COLUMNS; c++) {
                                char cname[256];
                                sprintf(cname, "%s.%s", src2->name, src2->columns[c]);
                                dst->columns[dst->num_columns] = strdup_safe(cname);
                                dst->column_widths[dst->num_columns] = (int)strlen(cname);
                                dst->num_columns++;
                            }
                        }
                    } else {
                        for (i = 0; i < s->num_cols && dst->num_columns < MAX_COLUMNS; i++) {
                            dst->columns[dst->num_columns] = strdup_safe(s->cols[i]);
                            dst->column_widths[dst->num_columns] = (int)strlen(s->cols[i]);
                            dst->num_columns++;
                        }
                    }

                    dst->capacity_rows = 100;
                    dst->rows = (char***)calloc(dst->capacity_rows, sizeof(char**));
                    dst->num_rows = 0;

                    for (r = 0; r < src1->num_rows; r++) {
                        if (src2) {
                            for (k = 0; k < src2->num_rows; k++) {
                                if (!s->join_cond || EvalExprJoin(s->join_cond, src1, r, src2, k)) {
                                    if (!s->where || EvalExprJoin(s->where, src1, r, src2, k)) {
                                        if (dst->num_rows >= dst->capacity_rows) {
                                            dst->capacity_rows *= 2;
                                            dst->rows = (char***)realloc(dst->rows, dst->capacity_rows * sizeof(char**));
                                        }
                                        dst->rows[dst->num_rows] = (char**)calloc(dst->num_columns, sizeof(char*));
                                        if (s->cols[0] && strcmp(s->cols[0], "*") == 0) {
                                            int dc = 0;
                                            for (c = 0; c < src1->num_columns && dc < dst->num_columns; c++)
                                                dst->rows[dst->num_rows][dc++] = strdup_safe(src1->rows[r][c]);
                                            for (c = 0; c < src2->num_columns && dc < dst->num_columns; c++)
                                                dst->rows[dst->num_rows][dc++] = strdup_safe(src2->rows[k][c]);
                                        } else {
                                            for (i = 0; i < s->num_cols && i < dst->num_columns; i++) {
                                                ExprNode fake_node;
                                                char* val;
                                                fake_node.kind = VAL_IDENT;
                                                fake_node.value = (char*)s->cols[i];
                                                fake_node.left = NULL; fake_node.right = NULL;
                                                val = GetASTValJoin(&fake_node, src1, r, src2, k);
                                                dst->rows[dst->num_rows][i] = strdup_safe(val ? val : "");
                                            }
                                        }
                                        dst->num_rows++;
                                    }
                                }
                            }
                        } else {
                            if (!s->where || EvalExprJoin(s->where, src1, r, NULL, -1)) {
                                if (dst->num_rows >= dst->capacity_rows) {
                                    dst->capacity_rows *= 2;
                                    dst->rows = (char***)realloc(dst->rows, dst->capacity_rows * sizeof(char**));
                                }
                                dst->rows[dst->num_rows] = (char**)calloc(dst->num_columns, sizeof(char*));
                                if (s->cols[0] && strcmp(s->cols[0], "*") == 0) {
                                    for (c = 0; c < src1->num_columns && c < dst->num_columns; c++)
                                        dst->rows[dst->num_rows][c] = strdup_safe(src1->rows[r][c]);
                                } else {
                                    for (i = 0; i < s->num_cols && i < dst->num_columns; i++) {
                                        ExprNode fake_node;
                                        char* val;
                                        fake_node.kind = VAL_IDENT;
                                        fake_node.value = (char*)s->cols[i];
                                        fake_node.left = NULL; fake_node.right = NULL;
                                        val = GetASTValJoin(&fake_node, src1, r, NULL, -1);
                                        dst->rows[dst->num_rows][i] = strdup_safe(val ? val : "");
                                    }
                                }
                                dst->num_rows++;
                            }
                        }
                    }

                    {
                        int widths[MAX_COLUMNS];
                        memset(widths, 0, sizeof(widths));
                        for (c = 0; c < dst->num_columns && c < MAX_COLUMNS; c++) {
                            widths[c] = dst->columns[c] ? (int)strlen(dst->columns[c]) : 0;
                            for (r = 0; r < dst->num_rows; r++) {
                                if (dst->rows[r][c]) {
                                    int len = (int)strlen(dst->rows[r][c]);
                                    if (len > widths[c]) widths[c] = len;
                                }
                            }
                        }
                        for (c = 0; c < dst->num_columns && c < MAX_COLUMNS; c++) {
                            const char* col_name = dst->columns[c] ? dst->columns[c] : "";
                            int slen = (int)strlen(col_name);
                            EMIT("%s", col_name);
                            for (i = slen; i < widths[c]; i++) EMIT(" ");
                            EMIT("  ");
                        }
                        EMIT("\r\n");
                        for (c = 0; c < dst->num_columns && c < MAX_COLUMNS; c++) {
                            for (i = 0; i < widths[c]; i++) EMIT("-");
                            EMIT("  ");
                        }
                        EMIT("\r\n");
                        for (r = 0; r < dst->num_rows; r++) {
                            for (c = 0; c < dst->num_columns && c < MAX_COLUMNS; c++) {
                                const char* cell = dst->rows[r][c] ? dst->rows[r][c] : "";
                                int slen = (int)strlen(cell);
                                EMIT("%s", cell);
                                for (i = slen; i < widths[c]; i++) EMIT(" ");
                                EMIT("  ");
                            }
                            EMIT("\r\n");
                        }
                        if (dst->num_rows == 0) EMIT("(0 rows)\r\n");
                        EMIT("\r\n");
                    }
                    current_table_idx = res_idx;
                }
            }
        } else EMIT("Error: Table '%s' not found.\r\n", s->table);
    }

    #undef EMIT
}

/* ============================================================================
 * QUERY EXECUTOR
 * ============================================================================ */
void ExecuteQueryEx(const char* query, char* out_buf, size_t out_max) {
    int is_local = (out_buf == NULL);
    char* exec_buf;
    size_t exec_max;
    int syntax_ok = 1;
    int has_success = 0, has_error = 0;

    if (!query || !*query) return;

    if (is_connected && !out_buf) {
        lex_ptr = query; NextToken();
        while (curr_tok.kind != TK_EOF) {
            SQLStmt* ast = ParseStmt();
            if (ast) { FreeStmt(ast); }
            else { if (curr_tok.kind != TK_EOF) { syntax_ok = 0; break; } }
        }
        if (!syntax_ok) {
            SyncUI("Syntax Error before sending.", "Syntax Error or Unsupported Command.");
            return;
        }
        SendToRemote(query);
        AddToHistory(query);
        return;
    }

    if (is_local) { 
        exec_max = MAX_OUTPUT_SIZE; 
        exec_buf = (char*)calloc(exec_max, 1);
    } else {
        exec_buf = out_buf;
        exec_max = out_max;
    }

    if (!exec_buf) return;

    lex_ptr = query; NextToken();

    while (curr_tok.kind != TK_EOF) {
        SQLStmt* ast = ParseStmt();
        if (ast) {
            ExecuteAST(ast, exec_buf, exec_max);
            FreeStmt(ast);
            has_success = 1;
        } else if (curr_tok.kind != TK_EOF) {
            size_t p = strlen(exec_buf);
            if (p < exec_max - 1) {
                char err_msg[] = "Syntax Error or Unsupported Command.\r\n";
                size_t elen = strlen(err_msg);
                if (p + elen < exec_max - 1) {
                    strcpy(exec_buf + p, err_msg);
                }
            }
            has_error = 1; break;
        }
    }

    if (is_local) {
        if (has_success && !has_error) AddToHistory(query);
        SyncUI(has_error ? "Error" : "Success", exec_buf);
        free(exec_buf);
        SetFocus(hEditQuery);
    }
}

/* ============================================================================
 * RFC 4180 CSV ENGINE
 * ============================================================================ */
char DetectDelimiter(const char* data) {
    int counts[256] = {0};
    int i = 0;
    int max = 0;
    int c_idx;
    char delim = ',';
    char candidates[] = {',', ';', '\t', '|', '\xA6'};

    while (data[i] && data[i] != '\n') {
        if (data[i] == '"') { i++; while (data[i] && data[i] != '"') i++; if (!data[i]) break; }
        else counts[(unsigned char)data[i]]++;
        i++;
    }
    for (c_idx = 0; c_idx < 5; c_idx++) {
        if (counts[(unsigned char)candidates[c_idx]] > max) {
            max = counts[(unsigned char)candidates[c_idx]];
            delim = candidates[c_idx];
        }
    }
    return delim;
}

static void emit_rfc4180(FILE* fp, const char* field, int is_last, char delim) {
    int needs_quotes;
    const char* c;
    if (!field) { fputc(is_last ? '\n' : delim, fp); return; }
    needs_quotes = (strchr(field, delim) || strchr(field, '\n') || strchr(field, '"')) != NULL;
    if (needs_quotes) {
        fputc('"', fp);
        for (c = field; *c; c++) {
            if (*c == '"') fputs("\"\"", fp);
            else fputc(*c, fp);
        }
        fputc('"', fp);
    } else {
        fputs(field, fp);
    }
    fputc(is_last ? '\n' : delim, fp);
}

BOOL LoadCSV(const char* filename, const char* tablename) {
    FILE* fp;
    long len;
    char* buf;
    Table* tbl;
    char* p;
    int is_header = 1;
    char msg[256];

    fp = fopen(filename, "rb");
    if (!fp) return FALSE;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char*)malloc(len + 1);
    if (!buf) { fclose(fp); return FALSE; }
    fread(buf, 1, len, fp);
    buf[len] = '\0';
    fclose(fp);

    if (num_tables >= MAX_TABLES) { free(buf); return FALSE; }
    tbl = &tables[num_tables];
    memset(tbl, 0, sizeof(Table));
    strncpy(tbl->name, tablename, sizeof(tbl->name) - 1);
    tbl->filename = strdup_safe(filename);
    tbl->delim = DetectDelimiter(buf);
    tbl->capacity_rows = 100;
    tbl->rows = (char***)calloc(tbl->capacity_rows, sizeof(char**));
    tbl->columns = (char**)calloc(MAX_COLUMNS, sizeof(char*));

    p = buf;
    while (*p) {
        char** fields = (char**)calloc(MAX_COLUMNS, sizeof(char*));
        int count = 0, in_row = 1;
        int ci;
        while (in_row && *p) {
            char fbuf[MAX_CELL_SIZE];
            int fi = 0, in_quotes = 0;
            if (*p == '"') {
                in_quotes = 1; p++;
                while (*p) {
                    if (*p == '"') {
                        if (*(p+1) == '"') { if (fi < MAX_CELL_SIZE - 1) fbuf[fi++] = '"'; p += 2; }
                        else { in_quotes = 0; p++; break; }
                    } else { if (fi < MAX_CELL_SIZE - 1) fbuf[fi++] = *p; p++; }
                }
            } else {
                while (*p && *p != tbl->delim && *p != '\n' && *p != '\r') {
                    if (fi < MAX_CELL_SIZE - 1) fbuf[fi++] = *p;
                    p++;
                }
            }
            fbuf[fi] = '\0';
            if (count < MAX_COLUMNS) fields[count++] = strdup_safe(fbuf);
            if (*p == tbl->delim) p++;
            else if (*p == '\r' || *p == '\n') {
                in_row = 0;
                if (*p == '\r' && *(p+1) == '\n') p += 2; else p++;
            } else if (!*p) in_row = 0;
        }
        if (count > 0 || (count == 0 && *p)) {
            if (is_header) {
                tbl->num_columns = count;
                for (ci = 0; ci < count; ci++) {
                    tbl->columns[ci] = fields[ci];
                    tbl->column_widths[ci] = (int)strlen(fields[ci]);
                }
                free(fields);
                is_header = 0;
            } else {
                if (tbl->num_rows >= tbl->capacity_rows) {
                    tbl->capacity_rows *= 2;
                    tbl->rows = (char***)realloc(tbl->rows, tbl->capacity_rows * sizeof(char**));
                }
                tbl->rows[tbl->num_rows] = (char**)calloc(tbl->num_columns, sizeof(char*));
                for (ci = 0; ci < tbl->num_columns && ci < count; ci++) {
                    int field_len;
                    tbl->rows[tbl->num_rows][ci] = fields[ci];
                    field_len = (int)strlen(fields[ci]);
                    if (field_len > tbl->column_widths[ci]) tbl->column_widths[ci] = field_len;
                }
                for (ci = count; ci < tbl->num_columns; ci++) {
                    tbl->rows[tbl->num_rows][ci] = strdup_safe("");
                }
                tbl->num_rows++;
                for (ci = count; ci < MAX_COLUMNS; ci++) { if (fields[ci]) { free(fields[ci]); } }
                free(fields);
            }
        } else {
            for (ci = 0; ci < MAX_COLUMNS; ci++) { if (fields[ci]) free(fields[ci]); }
            free(fields);
        }
    }
    free(buf);
    current_table_idx = num_tables++;
    SaveTablesToINI();
    sprintf(msg, "Loaded: %s (%d cols, %d rows, delim '%c')",
            tablename, tbl->num_columns, tbl->num_rows, tbl->delim);
    SyncUI(msg, "");
    return TRUE;
}

void SaveCSV(Table* tbl) {
    FILE* fp;
    int c, r;
    if (!tbl->filename) return;
    fp = fopen(tbl->filename, "wb");
    if (!fp) return;
    for (c = 0; c < tbl->num_columns; c++)
        emit_rfc4180(fp, tbl->columns[c], c == tbl->num_columns - 1, tbl->delim);
    for (r = 0; r < tbl->num_rows; r++) {
        for (c = 0; c < tbl->num_columns; c++)
            emit_rfc4180(fp, tbl->rows[r][c], c == tbl->num_columns - 1, tbl->delim);
    }
    fclose(fp);
}

/* ============================================================================
 * TELNET CLIENT (connect to remote)
 * ============================================================================ */
void ToggleConnection(void) {
    char server_str[256];
    char ip[256];
    int port = 23;
    char* colon;
    unsigned long ip_addr;
    SOCKET tmp_sock;
    struct sockaddr_in addr;
    int timeout = 5000;
    int success;
    char msg[256];
    int si, count;
    char all_servers[512] = {0};
    char ini_path[MAX_PATH];
    char* ext;

    if (is_connected) {
        closesocket(client_sock);
        client_sock = INVALID_SOCKET;
        is_connected = 0;
        SyncUI("Disconnected from remote server.", NULL);
        return;
    }

    GetWindowText(hComboServer, server_str, sizeof(server_str));
    if (strlen(server_str) == 0) return;

    strcpy(ip, server_str);
    colon = strchr(ip, ':');
    if (colon) { *colon = '\0'; port = atoi(colon + 1); }

    ip_addr = inet_addr(ip);
    if (ip_addr == INADDR_NONE) {
        struct hostent* he = gethostbyname(ip);
        if (he) ip_addr = *(unsigned long*)he->h_addr_list[0];
    }

    tmp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = ip_addr;

    setsockopt(tmp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    success = (connect(tmp_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    if (success) {
        client_sock = tmp_sock;
        is_connected = 1;
        sprintf(msg, "Connected to %s:%d", ip, port);
        SyncUI(msg, NULL);
        if (SendMessage(hComboServer, CB_FINDSTRINGEXACT, -1, (LPARAM)server_str) == CB_ERR) {
            SendMessage(hComboServer, CB_ADDSTRING, 0, (LPARAM)server_str);
            count = SendMessage(hComboServer, CB_GETCOUNT, 0, 0);
            for (si = 0; si < count; si++) {
                char temp[128];
                SendMessage(hComboServer, CB_GETLBTEXT, si, (LPARAM)temp);
                if (strlen(all_servers) > 0) strcat(all_servers, ",");
                strcat(all_servers, temp);
            }
            GetModuleFileName(hInstGlobal, ini_path, MAX_PATH);
            ext = strrchr(ini_path, '.');
            if (ext) strcpy(ext, ".INI"); else strcat(ini_path, ".INI");
            WritePrivateProfileString("Client", "Servers", all_servers, ini_path);
        }
    } else {
        closesocket(tmp_sock);
        MessageBox(hMainWnd, "Failed to connect to server.", "Connection Error", MB_ICONERROR);
    }
}

void SendToRemote(const char* query) {
    char* temp = (char*)malloc(QUERY_BUFFER_SIZE);
    unsigned long sum = 0;
    char* p; char* uncomp; char* packed_buf; int packed_size;
    char* comp_buf; int comp_size; int magic_len; char* send_buf;
    char* recv_buf; int recv_len; char* rle_payload; int rle_len;
    char* unpacked_rle; int unpacked_rle_size; char* decomp_buf; int decomp_size;

    if (!temp) { SyncUI("Error", "Memory allocation failed."); return; }
    sprintf(temp, "%d\xA6%s\xA6%s", client_userid, client_password, query);
    for (p = temp; *p; p++) sum += (unsigned char)*p;

    uncomp = (char*)malloc(QUERY_BUFFER_SIZE * 2);
    if (!uncomp) { free(temp); return; }
    sprintf(uncomp, "%s\xA6%lu", temp, sum);
    free(temp);

    packed_buf = (char*)malloc(QUERY_BUFFER_SIZE * 2);
    if (!packed_buf) { free(uncomp); return; }
    packed_size = Pack6Bit(uncomp, (int)strlen(uncomp) + 1, packed_buf, QUERY_BUFFER_SIZE * 2);
    if (packed_size <= 0) { free(uncomp); free(packed_buf); SyncUI("Error", "Error packing payload."); return; }

    comp_buf = (char*)malloc(QUERY_BUFFER_SIZE * 2);
    if (!comp_buf) { free(uncomp); free(packed_buf); return; }
    comp_size = CompressRLE(packed_buf, packed_size, comp_buf, QUERY_BUFFER_SIZE * 2);
    if (comp_size <= 0) { free(uncomp); free(packed_buf); free(comp_buf); SyncUI("Error", "Error compressing payload."); return; }

    magic_len = (int)strlen(telnet_magic);
    send_buf = (char*)malloc(QUERY_BUFFER_SIZE * 2 + 256);
    if (!send_buf) { free(uncomp); free(packed_buf); free(comp_buf); return; }
    memcpy(send_buf, telnet_magic, magic_len);
    memcpy(send_buf + magic_len, comp_buf, comp_size);

    if (send(client_sock, send_buf, magic_len + comp_size, 0) <= 0) {
        closesocket(client_sock); client_sock = INVALID_SOCKET; is_connected = 0;
        SyncUI("Error: Connection lost.", "Error: Connection lost.");
        free(uncomp); free(packed_buf); free(comp_buf); free(send_buf);
        return;
    }

    recv_buf = (char*)malloc(MAX_OUTPUT_SIZE);
    if (!recv_buf) { free(uncomp); free(packed_buf); free(comp_buf); free(send_buf); return; }
    recv_len = recv(client_sock, recv_buf, MAX_OUTPUT_SIZE - 1, 0);
    if (recv_len > 0) {
        recv_buf[recv_len] = '\0';
        if (recv_len >= magic_len && strncmp(recv_buf, telnet_magic, magic_len) == 0) {
            rle_payload = recv_buf + magic_len;
            rle_len = recv_len - magic_len;
            unpacked_rle = (char*)malloc(MAX_OUTPUT_SIZE);
            if (unpacked_rle) {
                unpacked_rle_size = DecompressRLE(rle_payload, rle_len, unpacked_rle, MAX_OUTPUT_SIZE);
                decomp_buf = (char*)malloc(MAX_OUTPUT_SIZE);
                if (decomp_buf) {
                    decomp_size = Unpack6Bit(unpacked_rle, unpacked_rle_size, decomp_buf, MAX_OUTPUT_SIZE);
                    if (decomp_size > 0) {
                        SyncUI("Received remote response.", decomp_buf);
                    } else {
                        SyncUI("Error", "Error decompressing response.");
                    }
                    free(decomp_buf);
                }
                free(unpacked_rle);
            }
        } else {
            SyncUI("Error", "Error: Invalid response magic string.");
        }
    } else {
        closesocket(client_sock); client_sock = INVALID_SOCKET; is_connected = 0;
        SyncUI("Error: Connection dropped.", "Error: No response or connection dropped.");
    }
    free(uncomp); free(packed_buf); free(comp_buf); free(send_buf); free(recv_buf);
}

/* ============================================================================
 * TELNET SERVER (multi-client via WSAAsyncSelect)
 * ============================================================================ */
void HandleServerRead(SOCKET sock, char* in_buf, int in_len) {
    char* out_buf;
    int magic_len;
    char* rle_payload;
    int rle_len;
    char* unpacked_rle;
    int unpacked_rle_size;
    char* decomp_buf;
    int decomp_size;
    char* payload_start;
    char* last_pipe;
    unsigned long provided_chk;
    unsigned long sum;
    char* p;
    char* pipe1;
    char* pipe2;
    char* rcv_pass;
    char* rcv_query;
    int pass_len, rcv_len, mismatch, pi;
    char* packed_buf;
    int packed_size;
    char* comp_buf;
    int comp_size;
    char* send_buf;

    if (telnet_plaintext) {
        in_buf[in_len] = '\0';
        if (_strnicmp(in_buf, "bye", 3) == 0) {
            closesocket(sock);
            return;
        }
        out_buf = (char*)calloc(MAX_OUTPUT_SIZE, 1);
        ExecuteQueryEx(in_buf, out_buf, MAX_OUTPUT_SIZE);
        send(sock, out_buf, (int)strlen(out_buf), 0);
        free(out_buf);
    } else {
        magic_len = (int)strlen(telnet_magic);
        if (in_len < magic_len || strncmp(in_buf, telnet_magic, magic_len) != 0) {
            closesocket(sock);
            return;
        }
        rle_payload = in_buf + magic_len;
        rle_len = in_len - magic_len;

        unpacked_rle = (char*)malloc(QUERY_BUFFER_SIZE * 2);
        unpacked_rle_size = DecompressRLE(rle_payload, rle_len, unpacked_rle, QUERY_BUFFER_SIZE * 2);

        decomp_buf = (char*)malloc(QUERY_BUFFER_SIZE * 2);
        decomp_size = Unpack6Bit(unpacked_rle, unpacked_rle_size, decomp_buf, QUERY_BUFFER_SIZE * 2);

        if (decomp_size > 0) {
            out_buf = (char*)calloc(MAX_OUTPUT_SIZE, 1);
            payload_start = decomp_buf;
            last_pipe = strrchr(payload_start, '\xA6');

            if (last_pipe) {
                *last_pipe = '\0';
                provided_chk = (unsigned long)atol(last_pipe + 1);
                sum = 0;
                for (p = decomp_buf; *p; p++) sum += (unsigned char)*p;

                if (sum == provided_chk) {
                    pipe1 = strchr(decomp_buf, '\xA6');
                    if (pipe1) {
                        *pipe1 = '\0';
                        pipe2 = strchr(pipe1 + 1, '\xA6');
                        if (pipe2) {
                            *pipe2 = '\0';
                            rcv_pass = pipe1 + 1;
                            rcv_query = pipe2 + 1;
                            pass_len = (int)strlen(telnet_password);
                            rcv_len = (int)strlen(rcv_pass);
                            mismatch = (pass_len ^ rcv_len);
                            for (pi = 0; pi < pass_len && pi < rcv_len; pi++)
                                mismatch |= (telnet_password[pi] ^ rcv_pass[pi]);

                            if (mismatch == 0) {
                                if (_strnicmp(rcv_query, "bye", 3) == 0) {
                                    free(out_buf); free(unpacked_rle); free(decomp_buf);
                                    closesocket(sock);
                                    return;
                                }
                                ExecuteQueryEx(rcv_query, out_buf, MAX_OUTPUT_SIZE);
                            } else {
                                strcpy(out_buf, "Error: Access Denied (Invalid Password)\r\n");
                            }
                        } else strcpy(out_buf, "Error: Invalid Format\r\n");
                    } else strcpy(out_buf, "Error: Invalid Format\r\n");
                } else strcpy(out_buf, "Error: Checksum mismatch\r\n");
            } else strcpy(out_buf, "Error: Invalid Format\r\n");

            packed_buf = (char*)malloc(MAX_OUTPUT_SIZE);
            packed_size = Pack6Bit(out_buf, (int)strlen(out_buf) + 1, packed_buf, MAX_OUTPUT_SIZE);
            comp_buf = (char*)malloc(MAX_OUTPUT_SIZE);
            comp_size = CompressRLE(packed_buf, packed_size, comp_buf, MAX_OUTPUT_SIZE);
            send_buf = (char*)malloc(MAX_OUTPUT_SIZE + 256);
            memcpy(send_buf, telnet_magic, magic_len);
            memcpy(send_buf + magic_len, comp_buf, comp_size);
            send(sock, send_buf, magic_len + comp_size, 0);

            free(out_buf); free(packed_buf); free(comp_buf); free(send_buf);
        }
        free(unpacked_rle); free(decomp_buf);
    }
}

void ProcessSocketEvent(SOCKET sock, WORD event, WORD error) {
    int i;
    SOCKET c_sock;
    int timeout;
    char* reqBuf;
    int bytes;

    if (error) {
        closesocket(sock);
        for (i = 0; i < server_client_count; i++) {
            if (server_clients[i] == sock) {
                server_clients[i] = server_clients[server_client_count - 1];
                server_client_count--;
                break;
            }
        }
        return;
    }
    switch (event) {
        case FD_ACCEPT: {
            c_sock = accept(sock, NULL, NULL);
            if (c_sock != INVALID_SOCKET) {
                if (server_client_count < MAX_CONCURRENT_CLIENTS) {
                    server_clients[server_client_count++] = c_sock;
                    timeout = telnet_timeout * 1000;
                    setsockopt(c_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
                    WSAAsyncSelect(c_sock, hMainWnd, WM_SOCKET, FD_READ | FD_CLOSE);
                } else {
                    closesocket(c_sock);
                }
            }
            break;
        }
        case FD_READ: {
            reqBuf = (char*)malloc(QUERY_BUFFER_SIZE * 2);
            if (!reqBuf) return;
            bytes = recv(sock, reqBuf, QUERY_BUFFER_SIZE * 2 - 1, 0);
            if (bytes > 0) {
                HandleServerRead(sock, reqBuf, bytes);
            } else {
                closesocket(sock);
                for (i = 0; i < server_client_count; i++) {
                    if (server_clients[i] == sock) {
                        server_clients[i] = server_clients[server_client_count - 1];
                        server_client_count--;
                        break;
                    }
                }
            }
            free(reqBuf);
            break;
        }
        case FD_CLOSE:
            closesocket(sock);
            for (i = 0; i < server_client_count; i++) {
                if (server_clients[i] == sock) {
                    server_clients[i] = server_clients[server_client_count - 1];
                    server_client_count--;
                    break;
                }
            }
            break;
    }
}

/* ============================================================================
 * EDIT SUBCLASS (Ctrl+A, F5)
 * ============================================================================ */
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    char* query;
    if (uMsg == WM_KEYDOWN) {
        if (wParam == 'A' && GetKeyState(VK_CONTROL) < 0) {
            SendMessage(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        if (wParam == VK_F5 && hwnd == hEditQuery) {
            query = (char*)malloc(QUERY_BUFFER_SIZE);
            if (query) {
                GetWindowText(hwnd, query, QUERY_BUFFER_SIZE);
                ExecuteQueryEx(query, NULL, 0);
                free(query);
            }
            return 0;
        }
    }
    if (uMsg == WM_CHAR && wParam == 1) return 0;
    return CallWindowProc((FARPROC)OldEditProc, hwnd, uMsg, wParam, lParam);
}

/* ============================================================================
 * MAIN WINDOW PROC
 * ============================================================================ */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_USER + 1:
            ProcessStartup();
            return 0;

        case WM_SOCKET:
            ProcessSocketEvent((SOCKET)wParam, WSAGETSELECTEVENT(lParam), WSAGETSELECTERROR(lParam));
            return 0;

        case WM_COMMAND:
            switch (wParam) {
                case ID_BTN_RUN:
                    if (HIWORD(lParam) == BN_CLICKED) {
                        char* q = (char*)malloc(QUERY_BUFFER_SIZE);
                        if (q) {
                            GetWindowText(hEditQuery, q, QUERY_BUFFER_SIZE);
                            ExecuteQueryEx(q, NULL, 0);
                            free(q);
                        }
                    }
                    break;
                case ID_BTN_CLEAR:
                    if (HIWORD(lParam) == BN_CLICKED) {
                        SetWindowText(hEditQuery, "");
                        SetFocus(hEditQuery);
                    }
                    break;
                case ID_BTN_CONNECT:
                    if (HIWORD(lParam) == BN_CLICKED) ToggleConnection();
                    break;
                case ID_BTN_PASTE:
                    if (HIWORD(lParam) == BN_CLICKED) {
                        PasteToEdit(hEditQuery);
                        SetFocus(hEditQuery);
                    }
                    break;
                case ID_BTN_COPY:
                    if (HIWORD(lParam) == BN_CLICKED) {
                        char* out = (char*)malloc(MAX_OUTPUT_SIZE);
                        if (out) {
                            GetWindowText(hEditOutput, out, MAX_OUTPUT_SIZE);
                            CopyToClipboard(out);
                            free(out);
                        }
                    }
                    break;
                case ID_COMBO_HISTORY:
                    if (HIWORD(lParam) == CBN_SELCHANGE) {
                        int idx = SendMessage(hComboHistory, CB_GETCURSEL, 0, 0);
                        if (idx != CB_ERR) {
                            char* wb = (char*)malloc(QUERY_BUFFER_SIZE);
                            if (wb) {
                                SendMessage(hComboHistory, CB_GETLBTEXT, idx, (LPARAM)wb);
                                SetWindowText(hEditQuery, wb);
                                free(wb);
                            }
                        }
                    }
                    break;
                case ID_LIST_TABLES:
                    if (HIWORD(lParam) == LBN_SELCHANGE) {
                        current_table_idx = SendMessage(hListTables, LB_GETCURSEL, 0, 0);
                        SyncUI(NULL, "");
                    }
                    break;
                case ID_BTN_OPEN:
                    if (HIWORD(lParam) == BN_CLICKED) {
                        OPENFILENAME ofn;
                        char szFile[MAX_PATH] = "";
                        char* fn;
                        char tb[MAX_COLUMN_NAME];
                        char* dot;
                        
                        memset(&ofn, 0, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files\0*.*\0";
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
                        if (GetOpenFileName(&ofn)) {
                            fn = strrchr(szFile, '\\');
                            fn = fn ? fn + 1 : szFile;
                            dot = strrchr(fn, '.');
                            if (dot) { strncpy(tb, fn, dot - fn); tb[dot - fn] = '\0'; }
                            else strcpy(tb, fn);
                            LoadCSV(szFile, tb);
                        }
                    }
                    break;
            }
            break;

        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (w == 0 || h == 0) return 0;
            MoveWindow(hBtnOpen, 10, 10, 100, 28, TRUE);
            MoveWindow(hComboServer, 120, 10, 200, 150, TRUE);
            MoveWindow(hBtnConnect, 330, 10, 90, 28, TRUE);
            MoveWindow(hBtnCopy, 430, 10, 80, 28, TRUE);
            MoveWindow(hListTables, 10, 45, w - 20, 70, TRUE);
            MoveWindow(hComboHistory, 10, 120, w - 20, 150, TRUE);
            MoveWindow(hBtnRun, 10, 165, 70, 50, TRUE);
            MoveWindow(hBtnPaste, 85, 165, 70, 50, TRUE);
            MoveWindow(hEditQuery, 160, 165, w - 250, 50, TRUE);
            MoveWindow(hBtnClear, w - 70, 165, 60, 50, TRUE);
            if (h > 245) MoveWindow(hEditOutput, 10, 225, w - 20, h - 245, TRUE);
            MoveWindow(hStatusBar, 0, h - 20, w, 20, TRUE);
            break;
        }

        case WM_DESTROY: {
            int i;
            for (i = 0; i < num_tables; i++) {
                SaveCSV(&tables[i]);
                ClearTable(&tables[i]);
            }
            for (i = 0; i < num_dropped_files; i++) {
                if (dropped_files[i]) remove(dropped_files[i]);
                free(dropped_files[i]);
            }
            if (client_sock != INVALID_SOCKET) { closesocket(client_sock); client_sock = INVALID_SOCKET; }
            for (i = 0; i < server_client_count; i++) closesocket(server_clients[i]);
            if (hListenSock != INVALID_SOCKET) { closesocket(hListenSock); hListenSock = INVALID_SOCKET; }
            SaveHistory();
            DeleteObject(hFontFixed);
            DeleteObject(hFontNormal);
            DeleteObject(hBrushBg);
            WSACleanup();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ============================================================================
 * APPLICATION INIT & WINDOW CREATION
 * ============================================================================ */
void InitApplication(void) {
    hBrushBg = CreateSolidBrush(COLOR_BG_LIGHT);
}

ATOM RegisterAppClass(HINSTANCE hInstance) {
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "CSV_SQL_Main";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = hBrushBg;
    return RegisterClass(&wc);
}

BOOL CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    hMainWnd = CreateWindow("CSV_SQL_Main",
        "CSV SQL - Query Your CSV Files",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);
    if (!hMainWnd) return FALSE;

    hFontFixed = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        FIXED_PITCH | FF_MODERN, "Courier New");
    hFontNormal = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        VARIABLE_PITCH | FF_SWISS, "MS Sans Serif");

    hBtnOpen = CreateWindow("BUTTON", "Open Table...", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_OPEN, hInstance, NULL);
    SendMessage(hBtnOpen, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hComboServer = CreateWindow("COMBOBOX", "", CBS_DROPDOWN | WS_VISIBLE | WS_CHILD | WS_VSCROLL,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_COMBO_SERVER, hInstance, NULL);
    SendMessage(hComboServer, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hBtnConnect = CreateWindow("BUTTON", "Connect", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_CONNECT, hInstance, NULL);
    SendMessage(hBtnConnect, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hBtnCopy = CreateWindow("BUTTON", "Copy Output", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_COPY, hInstance, NULL);
    SendMessage(hBtnCopy, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hListTables = CreateWindow("LISTBOX", "", LBS_NOTIFY | WS_BORDER | WS_VISIBLE | WS_CHILD | WS_VSCROLL,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_LIST_TABLES, hInstance, NULL);
    SendMessage(hListTables, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hComboHistory = CreateWindow("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VISIBLE | WS_CHILD | WS_VSCROLL,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_COMBO_HISTORY, hInstance, NULL);
    SendMessage(hComboHistory, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hBtnRun = CreateWindow("BUTTON", "Run", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_RUN, hInstance, NULL);
    SendMessage(hBtnRun, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hBtnPaste = CreateWindow("BUTTON", "Paste", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_PASTE, hInstance, NULL);
    SendMessage(hBtnPaste, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hEditQuery = CreateWindow("EDIT", "",
        WS_BORDER | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_EDIT_QUERY, hInstance, NULL);
    SendMessage(hEditQuery, WM_SETFONT, (WPARAM)hFontFixed, TRUE);
    OldEditProc = (WNDPROC)SetWindowLong(hEditQuery, GWL_WNDPROC, (LONG)(FARPROC)EditSubclassProc);

    hBtnClear = CreateWindow("BUTTON", "Clear", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_CLEAR, hInstance, NULL);
    SendMessage(hBtnClear, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    hEditOutput = CreateWindow("EDIT", "",
        WS_BORDER | WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL | ES_AUTOVSCROLL,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_EDIT_OUTPUT, hInstance, NULL);
    SendMessage(hEditOutput, WM_SETFONT, (WPARAM)hFontFixed, TRUE);

    hStatusBar = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | 0x0000L,
        0, 0, 0, 0, hMainWnd, (HMENU)ID_STATUSBAR, hInstance, NULL);
    SendMessage(hStatusBar, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);
    PostMessage(hMainWnd, WM_USER + 1, 0, 0);
    return TRUE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    char* p;
    char opt;
    char temp_tbl[MAX_PATH];
    int ti;
    int qi;
    char* tq;
    char q_char;
    char* end_q;
    MSG msg;

    (void)hPrevInstance;
    hInstGlobal = hInstance;
    
    if (lpCmdLine && *lpCmdLine) {
        p = lpCmdLine;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            if (*p == '-' || *p == '/') {
                p++;
                opt = *p++;
                while (*p && isspace((unsigned char)*p)) p++;
                if (opt == 't' || opt == 'T') {
                    while (*p && *p != '-' && *p != '/') {
                        memset(temp_tbl, 0, sizeof(temp_tbl));
                        ti = 0;
                        while (*p && !isspace((unsigned char)*p) && *p != '-' && *p != '/') {
                            if (ti < MAX_PATH - 1) temp_tbl[ti++] = *p;
                            p++;
                        }
                        temp_tbl[ti] = '\0';
                        if (ti > 0) {
                            if (strlen(startup_tables) > 0) strcat(startup_tables, " ");
                            strcat(startup_tables, temp_tbl);
                        }
                        while (*p && isspace((unsigned char)*p)) p++;
                    }
                } else if (opt == 'q' || opt == 'Q') {
                    qi = 0;
                    while (*p) {
                        if (qi < (int)sizeof(startup_query) - 1) startup_query[qi++] = *p;
                        p++;
                    }
                    startup_query[qi] = '\0';
                    tq = trim(startup_query);
                    if (*tq == '"' || *tq == '\'') {
                        q_char = *tq++;
                        end_q = strrchr(tq, q_char);
                        if (end_q) *end_q = '\0';
                    }
                    memmove(startup_query, tq, strlen(tq) + 1);
                    break;
                }
            } else {
                p++;
            }
        }
    }

    InitApplication();
    if (!RegisterAppClass(hInstance)) return 1;
    if (!CreateMainWindow(hInstance, nCmdShow)) return 1;

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

/* ============================================================================
 * STARTUP & CONFIGURATION LOADING
 * ============================================================================ */
void ProcessStartup(void) {
    WSADATA wsa;
    char ini_path[MAX_PATH];
    char* ext;
    FILE* fp;
    char* s;
    char* trimmed;
    char* tbl;
    char tablename[MAX_COLUMN_NAME];
    char filename[MAX_PATH];
    char* dot;
    struct sockaddr_in addr;

    char* servers_buf = (char*)calloc(512, 1);
    char* ini_tables = (char*)calloc(2048, 1);
    if (!servers_buf || !ini_tables) {
        if (servers_buf) free(servers_buf);
        if (ini_tables) free(ini_tables);
        return;
    }

    WSAStartup(MAKEWORD(1, 1), &wsa);
    LoadHistory();

    GetModuleFileName(hInstGlobal, ini_path, MAX_PATH);
    ext = strrchr(ini_path, '.');
    if (ext) strcpy(ext, ".INI"); else strcat(ini_path, ".INI");

    fp = fopen(ini_path, "r");
    if (fp) {
        fclose(fp);
    } else {
        fp = fopen(ini_path, "w");
        if (fp) {
            fprintf(fp, "[Telnet]\nEnabled=1\nPort=23\nTimeout=20\nPlaintext=0\nMagic=\xA6\nPassword=admin\n\n[Client]\nUserId=1\nPassword=admin\nServers=127.0.0.1:23\nTables=table\n");
            fclose(fp);
        } else {
            UpdateStatusBar("Error: Cannot create INI file.");
        }
    }

    telnet_enabled = GetPrivateProfileInt("Telnet", "Enabled", 0, ini_path);
    telnet_plaintext = GetPrivateProfileInt("Telnet", "Plaintext", 0, ini_path);
    telnet_port = GetPrivateProfileInt("Telnet", "Port", TELNET_PORT_DEFAULT, ini_path);
    telnet_timeout = GetPrivateProfileInt("Telnet", "Timeout", 20, ini_path);
    GetPrivateProfileString("Telnet", "Magic", "\xA6", telnet_magic, sizeof(telnet_magic), ini_path);
    GetPrivateProfileString("Telnet", "Password", "admin", telnet_password, sizeof(telnet_password), ini_path);

    client_userid = GetPrivateProfileInt("Client", "UserId", 1, ini_path);
    GetPrivateProfileString("Client", "Password", "admin", client_password, sizeof(client_password), ini_path);

    GetPrivateProfileString("Client", "Servers", "", servers_buf, 512, ini_path);
    if (strlen(servers_buf) > 0) {
        s = strtok(servers_buf, ",");
        while (s) {
            trimmed = trim(s);
            if (strlen(trimmed) > 0) SendMessage(hComboServer, CB_ADDSTRING, 0, (LPARAM)trimmed);
            s = strtok(NULL, ",");
        }
        SendMessage(hComboServer, CB_SETCURSEL, 0, 0);
    }

    if (telnet_enabled) {
        hListenSock = socket(AF_INET, SOCK_STREAM, 0);
        if (hListenSock != INVALID_SOCKET) {
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons((unsigned short)telnet_port);
            if (bind(hListenSock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                listen(hListenSock, SOMAXCONN);
                WSAAsyncSelect(hListenSock, hMainWnd, WM_SOCKET, FD_ACCEPT);
                UpdateStatusBar("Telnet server listening on port %d", telnet_port);
            } else {
                closesocket(hListenSock);
                hListenSock = INVALID_SOCKET;
                UpdateStatusBar("Warning: Could not bind telnet server.");
            }
        }
    }

    if (strlen(startup_tables) == 0) {
        GetPrivateProfileString("Client", "Tables", "", ini_tables, 2048, ini_path);
        strcpy(startup_tables, ini_tables);
    }

    if (strlen(startup_tables) > 0) {
        tbl = strtok(startup_tables, ",");
        while (tbl) {
            trimmed = trim(tbl);
            if (strlen(trimmed) > 0) {
                dot = strrchr(trimmed, '.');
                if (dot && _stricmp(dot, ".csv") == 0) {
                    strcpy(filename, trimmed);
                    strncpy(tablename, trimmed, dot - trimmed);
                    tablename[dot - trimmed] = '\0';
                } else {
                    sprintf(filename, "%s.csv", trimmed);
                    strcpy(tablename, trimmed);
                }
                LoadCSV(filename, tablename);
            }
            tbl = strtok(NULL, ",");
        }
    } else {
        LoadCSV("table.csv", "table");
    }

    if (strlen(startup_query) > 0) {
        SetWindowText(hEditQuery, startup_query);
        ExecuteQueryEx(startup_query, NULL, 0);
    }

    free(servers_buf);
    free(ini_tables);
}