/* ============================================================================
 * CSV SQL - 16-Bit In-Memory Mutator (Open Watcom Win16)
 *
 * COMPILATION INSTRUCTIONS (Open Watcom C/C++):
 *   wcl -ml -bcl=windows -Os -s -fe=csvsql.exe csvsql16.c winsock.lib
 * ============================================================================ *//* ============================================================================
 * CSV SQL - 16-Bit In-Memory Mutator (Open Watcom Win16 - C89 Compliant)
 * ============================================================================ */
#include <windows.h>
#include <winsock.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

#define WM_USER_SOCKET      (WM_USER + 100)

#define MAX_TABLES          16
#define MAX_COLUMNS         64
#define MAX_COLUMN_NAME     64
#define QUERY_BUFFER_SIZE   8192
#define MAX_CELL_SIZE       1024
#define MAX_OUTPUT_SIZE     30000 

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
    char fk_table[MAX_COLUMNS][MAX_COLUMN_NAME];
    char fk_col[MAX_COLUMNS][MAX_COLUMN_NAME];
} Table;

typedef enum {
    TK_EOF, TK_IDENT, TK_STR, TK_NUM, 
    TK_SELECT, TK_FROM, TK_WHERE, TK_INSERT, TK_INTO, TK_VALUES, 
    TK_UPDATE, TK_SET, TK_DELETE, TK_CREATE, TK_TABLE, TK_DROP,
    TK_JOIN, TK_ON, TK_AND, TK_OR, TK_DEFAULT, TK_PRIMARY, TK_KEY, TK_FOREIGN, TK_REFERENCES,
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_COMMA, TK_LPAREN, TK_RPAREN, TK_STAR, TK_SEMI
} TokenKind;

typedef struct { TokenKind kind; char value[256]; } Token;
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

static Table tables[MAX_TABLES];
static int num_tables = 0;

static HWND hMainWnd, hEditQuery, hEditOutput, hComboHistory, hComboServer, hBtnConnect;
static HWND hBtnRun, hBtnClear, hBtnCopy, hBtnPaste, hBtnOpen, hListTables;

static const char* lex_ptr;
static Token curr_tok;
static SOCKET listen_sock = INVALID_SOCKET;
static SOCKET client_sock = INVALID_SOCKET;

void ExecuteQueryEx(const char* query, char* out_buf, size_t out_max);

/* ============================================================================
 * WIN16 UTILITIES & STRICT NUMERIC PARSER
 * ============================================================================ */
char* strdup_safe(const char* s) {
    size_t len;
    char* dup;
    if (!s) return NULL; 
    len = strlen(s) + 1; 
    dup = malloc(len); 
    return dup ? memcpy(dup, s, len) : NULL;
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

void SyncUI(const char* status, const char* result) {
    int i;
    if (result) SetWindowText(hEditOutput, result);
    SendMessage(hListTables, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < num_tables; i++) {
        char buf[256]; 
        sprintf(buf, "[%s] %d cols / %d rows", tables[i].name, tables[i].num_columns, tables[i].num_rows);
        SendMessage(hListTables, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

int FindTable(const char* name) { 
    int i;
    for (i = 0; i < num_tables; i++) 
        if (_stricmp(tables[i].name, name) == 0) return i; 
    return -1; 
}

int GetColIndex(Table* tbl, const char* colname) { 
    int i;
    for(i=0; i<tbl->num_columns; i++) 
        if(_stricmp(tbl->columns[i], colname) == 0) return i; 
    return -1; 
}

/* ============================================================================
 * AST PARSER
 * ============================================================================ */
void NextToken(void) {
    while (*lex_ptr && isspace((unsigned char)*lex_ptr)) lex_ptr++;
    curr_tok.value[0] = '\0'; curr_tok.kind = TK_EOF;
    if (!*lex_ptr) return;

    if (*lex_ptr == ';') { curr_tok.kind = TK_SEMI; lex_ptr++; return; }
    if (*lex_ptr == ',') { curr_tok.kind = TK_COMMA; lex_ptr++; return; }
    if (*lex_ptr == '(') { curr_tok.kind = TK_LPAREN; lex_ptr++; return; }
    if (*lex_ptr == ')') { curr_tok.kind = TK_RPAREN; lex_ptr++; return; }
    if (*lex_ptr == '*') { curr_tok.kind = TK_STAR; lex_ptr++; return; }
    if (*lex_ptr == '=') { curr_tok.kind = TK_EQ; lex_ptr++; return; }
    if (*lex_ptr == '<') { curr_tok.kind = TK_LT; lex_ptr++; return; }
    if (*lex_ptr == '>') { curr_tok.kind = TK_GT; lex_ptr++; return; }

    if (*lex_ptr == '\'' || *lex_ptr == '"') {
        char quote = *lex_ptr++; 
        int i = 0;
        while (*lex_ptr && *lex_ptr != quote && i < 254) curr_tok.value[i++] = *lex_ptr++;
        if (*lex_ptr == quote) lex_ptr++;
        curr_tok.value[i] = '\0'; curr_tok.kind = TK_STR; return;
    }

    if (isalpha((unsigned char)*lex_ptr) || *lex_ptr == '_') {
        int i = 0; 
        int j;
        char up[256]; 
        while (isalnum((unsigned char)*lex_ptr) || *lex_ptr == '_') { if (i < 254) curr_tok.value[i++] = *lex_ptr; lex_ptr++; }
        curr_tok.value[i] = '\0'; 
        for(j=0; j<=i; j++) up[j] = toupper((unsigned char)curr_tok.value[j]);
        
        if (!strcmp(up, "SELECT")) curr_tok.kind = TK_SELECT; else if (!strcmp(up, "FROM")) curr_tok.kind = TK_FROM;
        else if (!strcmp(up, "WHERE")) curr_tok.kind = TK_WHERE; else if (!strcmp(up, "INSERT")) curr_tok.kind = TK_INSERT;
        else if (!strcmp(up, "INTO")) curr_tok.kind = TK_INTO; else if (!strcmp(up, "VALUES")) curr_tok.kind = TK_VALUES;
        else if (!strcmp(up, "DELETE")) curr_tok.kind = TK_DELETE; else if (!strcmp(up, "CREATE")) curr_tok.kind = TK_CREATE;
        else if (!strcmp(up, "TABLE")) curr_tok.kind = TK_TABLE; else if (!strcmp(up, "DEFAULT")) curr_tok.kind = TK_DEFAULT;
        else if (!strcmp(up, "PRIMARY")) curr_tok.kind = TK_PRIMARY; else if (!strcmp(up, "KEY")) curr_tok.kind = TK_KEY;
        else if (!strcmp(up, "FOREIGN")) curr_tok.kind = TK_FOREIGN; else if (!strcmp(up, "REFERENCES")) curr_tok.kind = TK_REFERENCES;
        else curr_tok.kind = TK_IDENT; return;
    }
    
    if (isdigit((unsigned char)*lex_ptr) || (*lex_ptr == '-' && isdigit((unsigned char)*(lex_ptr+1)))) {
        int i = 0; if (*lex_ptr == '-') curr_tok.value[i++] = *lex_ptr++;
        while (isdigit((unsigned char)*lex_ptr) || *lex_ptr == '.') { if (i < 254) curr_tok.value[i++] = *lex_ptr; lex_ptr++; }
        curr_tok.value[i] = '\0'; curr_tok.kind = TK_NUM; return;
    }
    curr_tok.kind = TK_IDENT; curr_tok.value[0] = *lex_ptr++; curr_tok.value[1] = '\0';
}

int Match(TokenKind kind) { if (curr_tok.kind == kind) { NextToken(); return 1; } return 0; }
int MatchIdent(char* out_name) {
    if (curr_tok.kind == TK_IDENT || curr_tok.kind == TK_STR) {
        strncpy(out_name, curr_tok.value, MAX_COLUMN_NAME - 1); out_name[MAX_COLUMN_NAME - 1] = '\0'; NextToken(); return 1;
    }
    return 0;
}

ExprNode* MakeNode(ExprKind k, ExprNode* l, ExprNode* r, const char* v) { ExprNode* n = malloc(sizeof(ExprNode)); n->kind = k; n->left = l; n->right = r; n->value = v ? strdup_safe(v) : NULL; return n; }
void FreeAST(ExprNode* n) { if(!n) return; FreeAST(n->left); FreeAST(n->right); free(n->value); free(n); }
ExprNode* ParsePrimary(void) {
    ExprNode* n = NULL; char ident[256];
    if (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM) { n = MakeNode(VAL_STR, NULL, NULL, curr_tok.value); NextToken(); }
    else if (MatchIdent(ident)) n = MakeNode(VAL_IDENT, NULL, NULL, ident);
    return n;
}
ExprNode* ParseCmp(void) {
    ExprNode* n = ParsePrimary();
    ExprKind op;
    while (curr_tok.kind == TK_EQ || curr_tok.kind == TK_NEQ || curr_tok.kind == TK_LT || curr_tok.kind == TK_GT) {
        op = OP_EQ; 
        if(curr_tok.kind == TK_NEQ) op = OP_NEQ; 
        if(curr_tok.kind == TK_LT) op = OP_LT; 
        if(curr_tok.kind == TK_GT) op = OP_GT;
        NextToken(); n = MakeNode(op, n, ParsePrimary(), NULL);
    }
    return n;
}

SQLStmt* ParseStmt(void) {
    SQLStmt* stmt;
    int matched;
    char col[256];
    int cidx;

    while (curr_tok.kind == TK_SEMI) NextToken(); 
    if (curr_tok.kind == TK_EOF) return NULL;
    
    stmt = calloc(1, sizeof(SQLStmt)); 
    matched = 0;
    
    if (Match(TK_SELECT)) {
        stmt->kind = STMT_SELECT; stmt->cols = calloc(MAX_COLUMNS, sizeof(char*));
        if (Match(TK_STAR)) stmt->cols[stmt->num_cols++] = strdup_safe("*");
        else { while (MatchIdent(col)) { stmt->cols[stmt->num_cols++] = strdup_safe(col); if (!Match(TK_COMMA)) break; } }
        if (Match(TK_FROM) && MatchIdent(stmt->table)) { if (Match(TK_WHERE)) stmt->where = ParseCmp(); matched = 1; }
    } 
    else if (Match(TK_INSERT) && Match(TK_INTO) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_INSERT; 
        if (Match(TK_VALUES) && Match(TK_LPAREN)) {
            stmt->vals = calloc(MAX_COLUMNS, sizeof(char*));
            while (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM) { stmt->vals[stmt->num_vals++] = strdup_safe(curr_tok.value); NextToken(); if (!Match(TK_COMMA)) break; }
            if (Match(TK_RPAREN)) matched = 1;
        }
    }
    else if (Match(TK_DELETE) && Match(TK_FROM) && MatchIdent(stmt->table)) { 
        stmt->kind = STMT_DELETE; matched = 1; 
        if (Match(TK_WHERE)) stmt->where = ParseCmp(); 
    }
    else if (Match(TK_CREATE) && Match(TK_TABLE) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_CREATE; 
        if (Match(TK_LPAREN)) {
            stmt->cols = calloc(MAX_COLUMNS, sizeof(char*)); stmt->defs = calloc(MAX_COLUMNS, sizeof(char*)); 
            stmt->pks = calloc(MAX_COLUMNS, sizeof(int));
            stmt->fk_tbls = calloc(MAX_COLUMNS, sizeof(char*)); stmt->fk_cols = calloc(MAX_COLUMNS, sizeof(char*));
            
            while (curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) {
                if (MatchIdent(col)) {
                    cidx = stmt->num_cols++; stmt->cols[cidx] = strdup_safe(col);
                    while (curr_tok.kind != TK_COMMA && curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) {
                        if (Match(TK_DEFAULT) && (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM)) { stmt->defs[cidx] = strdup_safe(curr_tok.value); NextToken(); }
                        else if (Match(TK_PRIMARY) && Match(TK_KEY)) { stmt->pks[cidx] = 1; }
                        else if (Match(TK_REFERENCES) && MatchIdent(col)) {
                            stmt->fk_tbls[cidx] = strdup_safe(col);
                            if (Match(TK_LPAREN) && MatchIdent(col)) { stmt->fk_cols[cidx] = strdup_safe(col); Match(TK_RPAREN); }
                        }
                        else NextToken();
                    }
                } else NextToken();
                Match(TK_COMMA);
            }
            if (Match(TK_RPAREN)) matched = 1;
        }
    }
    if (!matched) { free(stmt); while(curr_tok.kind != TK_EOF && curr_tok.kind != TK_SEMI) NextToken(); return NULL; }
    while(curr_tok.kind != TK_EOF && curr_tok.kind != TK_SEMI) NextToken(); return stmt;
}

char* GetASTVal(ExprNode* n, Table* t1, int r1) {
    int c;
    if (!n) return NULL;
    if (n->kind == VAL_STR) return n->value;
    if (n->kind == VAL_IDENT) { c = GetColIndex(t1, n->value); if (c >= 0 && r1 < t1->num_rows) return t1->rows[r1][c]; }
    return NULL;
}

int EvalExprJoin(ExprNode* n, Table* t1, int r1) {
    char *vL, *vR;
    int isNum;
    double numL, numR;

    if (!n) return 1; 
    vL = GetASTVal(n->left, t1, r1); vR = GetASTVal(n->right, t1, r1);
    if (!vL) vL = ""; if (!vR) vR = "";
    
    isNum = IsStrictNumeric(vL) && IsStrictNumeric(vR);
    numL = isNum ? atof(vL) : 0; numR = isNum ? atof(vR) : 0;
    
    if (n->kind == OP_EQ) return _stricmp(vL, vR) == 0;
    if (n->kind == OP_NEQ) return _stricmp(vL, vR) != 0;
    if (n->kind == OP_LT) return isNum ? (numL < numR) : (_stricmp(vL, vR) < 0);
    if (n->kind == OP_GT) return isNum ? (numL > numR) : (_stricmp(vL, vR) > 0);
    return 0;
}

/* ============================================================================
 * EXECUTOR ENGINE
 * ============================================================================ */
void ExecuteAST(SQLStmt* s, char* out_buf, size_t out_max) {
    int i, r, c, k, idx, affected, restrict_del, pk_c, t_idx, fk_c, cr;
    size_t pos;
    Table* tbl;

    if (!s) return;
    
    if (s->kind == STMT_CREATE) {
        if (num_tables < MAX_TABLES) {
            tbl = &tables[num_tables++]; memset(tbl, 0, sizeof(Table)); strncpy(tbl->name, s->table, sizeof(tbl->name)-1);
            tbl->num_columns = s->num_cols; tbl->columns = calloc(MAX_COLUMNS, sizeof(char*));
            for(i=0; i<s->num_cols; i++) { 
                tbl->columns[i] = strdup_safe(s->cols[i]); 
                if (s->defs && s->defs[i]) tbl->def_vals[i] = strdup_safe(s->defs[i]);
                if (s->pks && s->pks[i]) tbl->is_pk[i] = 1;
                if (s->fk_tbls && s->fk_tbls[i]) { strcpy(tbl->fk_table[i], s->fk_tbls[i]); strcpy(tbl->fk_col[i], s->fk_cols[i]); }
            }
            tbl->capacity_rows = 100; tbl->rows = malloc(tbl->capacity_rows * sizeof(char**));
            pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Created table '%s'.\r\n", s->table);
        }
    }
    else if (s->kind == STMT_INSERT) {
        idx = FindTable(s->table);
        if (idx >= 0) {
            tbl = &tables[idx];
            for (i=0; i<tbl->num_columns && i<s->num_vals; i++) {
                if (tbl->is_pk[i]) {
                    for (r=0; r<tbl->num_rows; r++) {
                        if (_stricmp(tbl->rows[r][i], s->vals[i]) == 0) {
                            pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Primary Key violation on column '%s'.\r\n", tbl->columns[i]);
                            return;
                        }
                    }
                }
            }

            if (tbl->num_rows >= tbl->capacity_rows) { tbl->capacity_rows *= 2; tbl->rows = realloc(tbl->rows, tbl->capacity_rows * sizeof(char**)); }
            tbl->rows[tbl->num_rows] = calloc(tbl->num_columns, sizeof(char*));
            for (i=0; i<tbl->num_columns; i++) {
                if (i < s->num_vals) tbl->rows[tbl->num_rows][i] = strdup_safe(s->vals[i]);
                else if (tbl->def_vals[i]) tbl->rows[tbl->num_rows][i] = strdup_safe(tbl->def_vals[i]);
                else tbl->rows[tbl->num_rows][i] = strdup_safe("");
            }
            tbl->num_rows++; pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Inserted 1 row into '%s'.\r\n", s->table);
        }
    }
    else if (s->kind == STMT_DELETE) {
        idx = FindTable(s->table);
        if (idx >= 0) {
            tbl = &tables[idx]; affected = 0;
            for (r=0; r<tbl->num_rows; r++) { 
                if (EvalExprJoin(s->where, tbl, r)) { 
                    restrict_del = 0;
                    for (pk_c=0; pk_c<tbl->num_columns; pk_c++) {
                        if (tbl->is_pk[pk_c]) {
                            for (t_idx=0; t_idx<num_tables; t_idx++) {
                                for (fk_c=0; fk_c<tables[t_idx].num_columns; fk_c++) {
                                    if (_stricmp(tables[t_idx].fk_table[fk_c], tbl->name) == 0) {
                                        for (cr=0; cr<tables[t_idx].num_rows; cr++) {
                                            if (_stricmp(tables[t_idx].rows[cr][fk_c], tbl->rows[r][pk_c]) == 0) restrict_del = 1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (restrict_del) {
                        pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Foreign Key constraint prevents deletion.\r\n"); return;
                    }

                    for (c=0; c<tbl->num_columns; c++) free(tbl->rows[r][c]); free(tbl->rows[r]); 
                    for (k=r; k<tbl->num_rows - 1; k++) tbl->rows[k] = tbl->rows[k+1]; 
                    tbl->num_rows--; r--; affected++; 
                } 
            }
            pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Deleted %d rows from '%s'.\r\n", affected, s->table);
        }
    }
}

void ExecuteQueryEx(const char* query, char* out_buf, size_t out_max) {
    int is_local, has_success, has_error;
    char* exec_buf;
    size_t exec_max, pos;
    SQLStmt* ast;

    if(!query || !*query) return;
    
    is_local = (out_buf == NULL); 
    exec_buf = out_buf; 
    exec_max = out_max;
    if (is_local) { exec_max = MAX_OUTPUT_SIZE; exec_buf = calloc(exec_max, 1); }
    
    lex_ptr = query; NextToken(); has_success = 0; has_error = 0;
    while (curr_tok.kind != TK_EOF) {
        ast = ParseStmt();
        if (ast) { ExecuteAST(ast, exec_buf, exec_max); has_success = 1; } 
        else if (curr_tok.kind != TK_EOF) { pos = strlen(exec_buf); snprintf(exec_buf + pos, exec_max - pos, "Syntax Error.\r\n"); has_error = 1; break; }
    }
    if (is_local) { SyncUI(has_error ? "Error" : "Success", exec_buf); free(exec_buf); SetFocus(hEditQuery); }
}

/* ============================================================================
 * WIN16 UI & ASYNC NETWORKING
 * ============================================================================ */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    char q[QUERY_BUFFER_SIZE];
    char in_buf[QUERY_BUFFER_SIZE];
    int len;
    char* out_buf;
    int w, h;

    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_RUN) { GetWindowText(hEditQuery, q, QUERY_BUFFER_SIZE); ExecuteQueryEx(q, NULL, 0); }
            break;
        case WM_USER_SOCKET:
            if (WSAGETSELECTERROR(lParam)) { closesocket((SOCKET)wParam); break; }
            switch (WSAGETSELECTEVENT(lParam)) {
                case FD_ACCEPT:
                    client_sock = accept(listen_sock, NULL, NULL);
                    WSAAsyncSelect(client_sock, hwnd, WM_USER_SOCKET, FD_READ | FD_CLOSE);
                    break;
                case FD_READ:
                    len = recv((SOCKET)wParam, in_buf, sizeof(in_buf)-1, 0);
                    if (len > 0) {
                        in_buf[len] = '\0'; out_buf = calloc(MAX_OUTPUT_SIZE, 1);
                        ExecuteQueryEx(in_buf, out_buf, MAX_OUTPUT_SIZE);
                        send((SOCKET)wParam, out_buf, strlen(out_buf), 0);
                        free(out_buf);
                    }
                    break;
                case FD_CLOSE: closesocket((SOCKET)wParam); break;
            }
            break;
        case WM_SIZE:
            w = LOWORD(lParam); h = HIWORD(lParam);
            MoveWindow(hListTables, 10, 10, w - 20, 80, TRUE);
            MoveWindow(hEditQuery, 10, 100, w - 100, 60, TRUE); 
            MoveWindow(hBtnRun, w - 80, 100, 70, 60, TRUE);
            MoveWindow(hEditOutput, 10, 170, w - 20, h - 180, TRUE);
            break;
        case WM_DESTROY: WSACleanup(); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0}; 
    WSADATA wsa; 
    struct sockaddr_in addr; 
    MSG msg;

    wc.lpfnWndProc = WndProc; wc.hInstance = hInstance; wc.lpszClassName = "CSV_SQL_Main"; 
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClass(&wc)) return 1;

    hMainWnd = CreateWindow("CSV_SQL_Main", "CSV SQL (Win16)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);
    hListTables = CreateWindow("LISTBOX", "", WS_BORDER | WS_VISIBLE | WS_CHILD, 0,0,0,0, hMainWnd, (HMENU)ID_LIST_TABLES, hInstance, NULL);
    hEditQuery = CreateWindow("EDIT", "", WS_BORDER | ES_MULTILINE | WS_VISIBLE | WS_CHILD, 0,0,0,0, hMainWnd, (HMENU)ID_EDIT_QUERY, hInstance, NULL);
    hBtnRun = CreateWindow("BUTTON", "Run", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, 0,0,0,0, hMainWnd, (HMENU)ID_BTN_RUN, hInstance, NULL);
    hEditOutput = CreateWindow("EDIT", "", WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | WS_VISIBLE | WS_CHILD, 0,0,0,0, hMainWnd, (HMENU)ID_EDIT_OUTPUT, hInstance, NULL);
    
    ShowWindow(hMainWnd, nCmdShow); UpdateWindow(hMainWnd);

    WSAStartup(0x0101, &wsa);
    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(23);
    bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)); listen(listen_sock, 5);
    WSAAsyncSelect(listen_sock, hMainWnd, WM_USER_SOCKET, FD_ACCEPT);

    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return msg.wParam;
}