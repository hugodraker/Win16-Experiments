/* ============================================================================
 * Vector Icon Editor for Win16 - Saves icons as GDI Polygons
 * ============================================================================
 * OPENWATCOM WIN16 C PORT (Windows 3.1x / 16-bit Target)
 * wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s icoedtv.c commdlg.lib
 *
 * PUBLIC DOMAIN NOTICE
 * Free and unencumbered software released into the public domain.
 *
 * ============================================================================ */

#pragma library("commdlg.lib")

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

#ifndef WM_APP
#define WM_APP 0x8000
#endif

#define GRID_SIZE 32
#define MAX_POINTS 64
#define MAX_SHAPES 40
#define MAX_UNDO 10
#define MAX_ICONS 50
#define PANEL_WIDTH 340

/* C89 Math & Color Macros */
#define fmax(a,b) (((a)>(b))?(a):(b))
#define fmin(a,b) (((a)<(b))?(a):(b))
#define round(x) ((double)((long)((x) + ((x)>=0 ? 0.5 : -0.5))))

/* --- Function Prototypes --- */
double Snap(double val);
double CLAMP(double v, double minv, double maxv);
void PtToSegProj(double px, double py, double x1, double y1, double x2, double y2, double* prX, double* prY, double* dist);
void MatMul(double* A, double* B, double* out);
int GetNextSVGFloat(char** pp, double* val);
void ParseTransform(char* str, double* mat, char limitChar);
char* Attr(char* tagStr, char* tagEnd, const char* attrName);
double AttrF(char* tagStr, char* tagEnd, const char* attrName, double defVal);
void GetSVGColors(char* p, char* tagEnd, COLORREF* fill, COLORREF* stroke, int* uF, int* uS, int defFill);
void LoadSVG(const char* path, HWND hwnd, double customScale);
void DrawDimensions(HDC dc);

/* --- Data Structures --- */
typedef struct { 
    int type; /* 0=Polygon, 1=Line, 2=Polyline */
    double ptsX[MAX_POINTS]; 
    double ptsY[MAX_POINTS]; 
    int ptCount; 
    COLORREF fill; 
    COLORREF stroke; 
    int useFill; 
    int useStroke; 
} Shape;

typedef struct { double x1, y1, x2, y2; } Edge;

typedef struct { 
    int caseId; 
    char name[64]; 
    Shape* shapes; 
    int shapeCount; 
} IconDef;

typedef struct {
    int s1, p1, s2, p2; /* Stores references to shape and point indices */
    double offset; 
    double textPos; 
    int mode; /* 0=Aligned, 1=Horizontal, 2=Vertical */
} Dimension;

/* --- Far Heap-Allocated Globals --- */
Shape* shapes = NULL; 
Shape* dragStartSnapshot = NULL;
Shape* history[MAX_UNDO]; 

int shapeCount = 0;
int historyShapeCount[MAX_UNDO]; 
int undoIndex = -1;

IconDef parsedIcons[MAX_ICONS];
int parsedCount = 0, currentIconIdx = -1, currentCaseId = 1;
char loadedCFile[260] = "";

#define MAX_DIMS 32
Dimension dims[MAX_DIMS];
int dimCount = 0;
int dragDimIdx = -1;

Shape currentShape;
int isDrawing = 0, currentMode = 3; 
int selectedShape = -1;

int hoverShape = -1, hoverPt = -1, hoverSegShape = -1, hoverSegPt = -1;
double hoverProjX = 0, hoverProjY = 0;
int isDraggingNodes = 0;
double dragStartX = 0, dragStartY = 0;

int ptSelected[MAX_SHAPES][MAX_POINTS];
int selOrderS[MAX_POINTS], selOrderP[MAX_POINTS], selOrderCount = 0;

int startX = 0, startY = 0;
double currentEndX = 0, currentEndY = 0;
int snapToGrid = 1;

double textCursorX = 0, textCursorY = 0; int textCursorActive = 0;
long font5x3[128] = {0};

COLORREF palette[16] = {
    RGB(0,0,0), RGB(255,255,255), RGB(128,128,128), RGB(192,192,192),
    RGB(255,0,0), RGB(128,0,0), RGB(255,255,0), RGB(128,128,0),
    RGB(0,255,0), RGB(0,128,0), RGB(0,255,255), RGB(0,128,128),
    RGB(0,0,255), RGB(0,0,128), RGB(255,0,255), RGB(128,0,128)
};
COLORREF currentFill = RGB(128, 128, 128); int useFill = 1;
COLORREF currentStroke = RGB(0, 0, 0); int useStroke = 1;

HINSTANCE hInst = NULL;
HWND hMain, hBtn[28], hStatus;
HWND hScrlSides, hScrlDepth, hScrlIcon;
HWND hBtnAddIcon, hBtnDelIcon;
HWND hDistEdit = NULL; 
FARPROC oldEditProc = NULL;
FARPROC subclassThunk = NULL;
int distEditMode = 0;

int paramSides = 4, paramStar = 100;
int canvasSize = 320, scaleFactor = 10, clientW = 0, clientH = 0;

const char* const bT[28] = {
    "Select/Edit", "Rotate", "Scale", "Polygon", "Line",
    "Polyline", "Shapes", "Text", "Flood Fill", "Undo",
    "Clear", "Delete", "Import SVG", "Open Ref", "Open .C",
    "Save .C", "Export Code", "Merge", "Move Up", "Move Down",
    "Align Vert", "Align Horz", "Set Dist", "Set Width", "Set Height", "Duplicate", "Set Angle", "Dimension"
};

char pendingSvgFile[260] = "";

/* --- Utilities & Initialization --- */
double CLAMP(double v, double minv, double maxv) {
    if (v < minv) return minv;
    if (v > maxv) return maxv;
    return v;
}

double Snap(double val) { 
    return snapToGrid ? round(val) : val; 
}

void InitFont(void) {
    font5x3['A']=0x25755L; font5x3['B']=0x65656L; font5x3['C']=0x34443L; font5x3['D']=0x65556L; 
    font5x3['E']=0x74747L; font5x3['F']=0x74744L; font5x3['G']=0x34553L; font5x3['H']=0x55755L;
    font5x3['I']=0x72227L; font5x3['J']=0x31152L; font5x3['K']=0x55655L; font5x3['L']=0x44447L; 
    font5x3['M']=0x57755L; font5x3['N']=0x75555L; font5x3['O']=0x25552L; font5x3['P']=0x75744L;
    font5x3['Q']=0x25531L; font5x3['R']=0x75755L; font5x3['S']=0x34216L; font5x3['T']=0x72222L; 
    font5x3['U']=0x55557L; font5x3['V']=0x55552L; font5x3['W']=0x55775L; font5x3['X']=0x55255L;
    font5x3['Y']=0x55222L; font5x3['Z']=0x71247L; font5x3['0']=0x25552L; font5x3['1']=0x26227L; 
    font5x3['2']=0x71747L; font5x3['3']=0x71717L; font5x3['4']=0x55711L; font5x3['5']=0x74717L;
    font5x3['6']=0x74757L; font5x3['7']=0x71111L; font5x3['8']=0x75757L; font5x3['9']=0x75717L; 
    font5x3['.']=0x00002L; font5x3['-']=0x00700L;
}

void ShowStatus(const char* msg) {
    if (hStatus) SetWindowText(hStatus, msg);
}

void UpdateStatusBar(void) {
    char sb[128]; 
    sprintf(sb, " [ID: %d Mode: %s] Shapes: %d | Nodes: %d | Depth: %d%%", currentCaseId, bT[currentMode], shapeCount, selOrderCount, paramStar);
    ShowStatus(sb);
}

void RedrawCanvas(HWND hwnd) {
    RECT r;
    r.left = 0; r.top = 0; r.right = canvasSize + 10; r.bottom = canvasSize + 10;
    InvalidateRect(hwnd, &r, TRUE); 
}

void SaveState(void) { 
    int i; Shape* temp;
    if (!shapes || !history[0]) return;
    
    if (undoIndex >= MAX_UNDO - 1) {
        temp = history[0];
        for(i = 0; i < MAX_UNDO - 1; i++) {
            historyShapeCount[i] = historyShapeCount[i+1];
            history[i] = history[i+1];
        }
        history[MAX_UNDO - 1] = temp;
        undoIndex = MAX_UNDO - 2;
    }
    undoIndex++; 
    historyShapeCount[undoIndex] = shapeCount; 
    memcpy(history[undoIndex], shapes, sizeof(Shape) * shapeCount); 
    UpdateStatusBar(); 
}

void Undo(HWND hwnd) { 
    if (undoIndex >= 0) { 
        shapeCount = historyShapeCount[undoIndex]; 
        memcpy(shapes, history[undoIndex], sizeof(Shape) * shapeCount); 
        undoIndex--; 
    } 
    if (shapeCount == 0) currentMode = 3;
    UpdateStatusBar(); RedrawCanvas(hwnd); 
}

void ClearSelection(void) {
    memset(ptSelected, 0, sizeof(ptSelected));
    selOrderCount = 0;
    UpdateStatusBar();
}

void ToggleSelection(int s, int p) {
    int i, j;
    if (s < 0 || s >= MAX_SHAPES || p < 0 || p >= MAX_POINTS) return;
    if (!ptSelected[s][p]) { 
        ptSelected[s][p] = 1; 
        selOrderS[selOrderCount] = s; selOrderP[selOrderCount] = p; selOrderCount++; 
    } else {
        ptSelected[s][p] = 0;
        for(i = 0; i < selOrderCount; i++) {
            if (selOrderS[i] == s && selOrderP[i] == p) {
                for(j = i; j < selOrderCount - 1; j++) { selOrderS[j] = selOrderS[j+1]; selOrderP[j] = selOrderP[j+1]; }
                selOrderCount--; break;
            }
        }
    }
    UpdateStatusBar();
}

/* --- File I/O & Icon Management --- */
void CommitCurrentIcon(void) {
    if (currentIconIdx >= 0 && currentIconIdx < parsedCount) {
        if (parsedIcons[currentIconIdx].shapes) { 
            GlobalFreePtr(parsedIcons[currentIconIdx].shapes); 
            parsedIcons[currentIconIdx].shapes = NULL; 
        }
        if (shapeCount > 0) {
            parsedIcons[currentIconIdx].shapes = (Shape*)GlobalAllocPtr(GHND, sizeof(Shape) * shapeCount);
            if (parsedIcons[currentIconIdx].shapes) memcpy(parsedIcons[currentIconIdx].shapes, shapes, sizeof(Shape) * shapeCount);
        }
        parsedIcons[currentIconIdx].shapeCount = shapeCount;
    }
}

void SwitchToIcon(int idx) {
    if (currentIconIdx == idx) return;
    CommitCurrentIcon();
    
    currentIconIdx = idx;
    dimCount = 0; dragDimIdx = -1;
    
    if (idx >= 0 && idx < parsedCount) {
        currentCaseId = parsedIcons[idx].caseId; shapeCount = parsedIcons[idx].shapeCount;
        if (parsedIcons[idx].shapes) memcpy(shapes, parsedIcons[idx].shapes, sizeof(Shape) * shapeCount); else shapeCount = 0;
    } else shapeCount = 0;
    undoIndex = -1; ClearSelection(); selectedShape = -1;
    if (shapeCount == 0) currentMode = 3; else currentMode = 0;
    if (hScrlIcon) SetScrollPos(hScrlIcon, SB_CTL, currentIconIdx >= 0 ? currentIconIdx : 0, TRUE);
    UpdateStatusBar(); RedrawCanvas(hMain);
}

void LoadCFile(const char* path, HWND hwnd) {
    FILE* f; long sz; char *d, *cur, *endBlock, *st, *pt, *lStr, *next, *pC, *bracket;
    int i, cId, tmpCount; double px, py;
    Shape* tmpShapes; Shape* exact; 
    static Shape s; 

    f = fopen(path, "rb"); 
    if (!f) return;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    
    if (sz > 60000L) { ShowStatus(" Error: File exceeds 60KB limit."); fclose(f); return; }
    
    d = (char*)GlobalAllocPtr(GHND, (size_t)sz + 1);
    if (d) {
        fread(d, 1, (size_t)sz, f); d[sz] = '\0'; fclose(f);
        for(i=0; i<MAX_ICONS; i++) { if (parsedIcons[i].shapes) { GlobalFreePtr(parsedIcons[i].shapes); parsedIcons[i].shapes = NULL; } }
        parsedCount = 0; cur = d;
        
        while ((cur = strstr(cur, "case ")) != NULL && parsedCount < MAX_ICONS) {
            cId = atoi(cur + 5); endBlock = strstr(cur, "break;"); 
            tmpShapes = (Shape*)GlobalAllocPtr(GHND, MAX_SHAPES * sizeof(Shape));
            if (!tmpShapes) break;
            
            tmpCount = 0;
            st = cur; if (!endBlock) endBlock = cur + strlen(cur);
            
            while (st < endBlock) {
                pt = strstr(st, "POINT "); lStr = strstr(st, "L(");
                next = pt ? pt : lStr;
                if (lStr && (!next || lStr < next)) next = lStr;
                if (!next || next >= endBlock) break;
                
                memset(&s, 0, sizeof(Shape));
                s.useFill = 1; s.useStroke = 1; s.fill = RGB(128,128,128); s.stroke = RGB(0,0,0);
                
                {
                    char* colorSearch = st;
                    while(colorSearch < next) {
                        int cr, cg, cb;
                        if (sscanf(colorSearch, "CreateSolidBrush(RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) { s.fill = RGB(cr, cg, cb); s.useFill = 1; }
                        if (sscanf(colorSearch, "CreatePen(PS_SOLID, 1, RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) { s.stroke = RGB(cr, cg, cb); s.useStroke = 1; }
                        colorSearch++;
                    }
                }

                if (next == pt) {
                    s.type = strstr(next, "Polyline(") ? 2 : 0;
                    pC = next; bracket = strchr(next, '}');
                    while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) { 
                        if (sscanf(pC, "PT(%lf,%lf)", &px, &py) == 2 && s.ptCount < MAX_POINTS) { s.ptsX[s.ptCount]=px; s.ptsY[s.ptCount]=py; s.ptCount++; } 
                        pC += 3; 
                    }
                    if (s.ptCount > 0 && tmpCount < MAX_SHAPES) tmpShapes[tmpCount++] = s;
                } else if (next == lStr) {
                    s.type = 1; s.ptCount = 2;
                    if (sscanf(next, "L(%lf,%lf,%lf,%lf)", &s.ptsX[0], &s.ptsY[0], &s.ptsX[1], &s.ptsY[1]) == 4) {
                        if (tmpCount < MAX_SHAPES) tmpShapes[tmpCount++] = s;
                    }
                }
                
                st = strchr(next, ';');
                if (!st) st = next + 1;
            }
            
            parsedIcons[parsedCount].caseId = cId; 
            if (tmpCount > 0) {
                exact = (Shape*)GlobalAllocPtr(GHND, tmpCount * sizeof(Shape));
                if (exact) { memcpy(exact, tmpShapes, tmpCount * sizeof(Shape)); parsedIcons[parsedCount].shapes = exact; }
                else { parsedIcons[parsedCount].shapes = NULL; }
            } else { parsedIcons[parsedCount].shapes = NULL; }
            parsedIcons[parsedCount].shapeCount = tmpCount; 
            
            parsedCount++; cur = endBlock;
            GlobalFreePtr(tmpShapes);
        }
        GlobalFreePtr(d);
        if (parsedCount > 0) { 
            strcpy(loadedCFile, path); 
            SetScrollRange(hScrlIcon, SB_CTL, 0, parsedCount - 1, TRUE); 
            currentIconIdx = -1; 
            SwitchToIcon(0); 
            ShowStatus(" C Data File Loaded.");
        } else {
            parsedCount = 1; currentIconIdx = -1; currentCaseId = 1;
            parsedIcons[0].caseId = 1; strcpy(parsedIcons[0].name, "New");
            parsedIcons[0].shapes = NULL; parsedIcons[0].shapeCount = 0;
            SetScrollRange(hScrlIcon, SB_CTL, 0, 0, TRUE); 
            SwitchToIcon(0);
            ShowStatus(" No valid icons found. Reset to default.");
        }
    } else {
        fclose(f); ShowStatus(" Error: Could not allocate buffer for file.");
    }
}

void DoSaveFile(HWND hwnd) {
    static OPENFILENAME ofn; static char szFile[260]; 
    FILE* f; int i, j, k;
    
    memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "C Data Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "c";
    
    if (GetSaveFileName(&ofn)) {
        CommitCurrentIcon();
        f = fopen(szFile, "w");
        if (f) {
            for (i = 0; i < parsedCount; i++) {
                fprintf(f, "case %d: {\n", parsedIcons[i].caseId);
                for (j = 0; j < parsedIcons[i].shapeCount; j++) {
                    Shape* s;
                    if (!parsedIcons[i].shapes) continue;
                    s = &parsedIcons[i].shapes[j];
                    
                    if (s->useFill || s->useStroke) {
                        fprintf(f, "    {\n");
                        if (s->useFill) fprintf(f, "        HBRUSH hBr = CreateSolidBrush(RGB(%d,%d,%d)); HGDIOBJ oBr = SelectObject(hdc, hBr);\n", (int)(s->fill & 0xFF), (int)((s->fill >> 8) & 0xFF), (int)((s->fill >> 16) & 0xFF));
                        if (s->useStroke) fprintf(f, "        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(%d,%d,%d)); HGDIOBJ oPen = SelectObject(hdc, hPen);\n", (int)(s->stroke & 0xFF), (int)((s->stroke >> 8) & 0xFF), (int)((s->stroke >> 16) & 0xFF));
                    }

                    if (s->type == 0 && s->ptCount > 2) {
                        fprintf(f, "        POINT p%d[] = { ", j);
                        for (k = 0; k < s->ptCount; k++) fprintf(f, "PT(%g,%g)%s", s->ptsX[k], s->ptsY[k], k == s->ptCount-1 ? "" : ", ");
                        fprintf(f, " }; POLY(p%d);\n", j);
                    } else if (s->type == 1 && s->ptCount == 2) {
                        fprintf(f, "        L(%g,%g,%g,%g);\n", s->ptsX[0], s->ptsY[0], s->ptsX[1], s->ptsY[1]);
                    } else if (s->type == 2 && s->ptCount >= 2) {
                        fprintf(f, "        POINT p%d[] = { ", j);
                        for (k = 0; k < s->ptCount; k++) fprintf(f, "PT(%g,%g)%s", s->ptsX[k], s->ptsY[k], k == s->ptCount-1 ? "" : ", ");
                        fprintf(f, " }; Polyline(hdc, p%d, %d);\n", j, s->ptCount);
                    }

                    if (s->useFill || s->useStroke) {
                        if (s->useFill) fprintf(f, "        SelectObject(hdc, oBr); DeleteObject(hBr);\n");
                        if (s->useStroke) fprintf(f, "        SelectObject(hdc, oPen); DeleteObject(hPen);\n");
                        fprintf(f, "    }\n");
                    }
                }
                fprintf(f, "    break;\n}\n");
            }
            fclose(f); ShowStatus(" File saved successfully.");
        }
    }
}

/* --- SVG Logic --- */
void MatMul(double* A, double* B, double* out) {
    out[0] = A[0]*B[0] + A[2]*B[1];
    out[1] = A[1]*B[0] + A[3]*B[1];
    out[2] = A[0]*B[2] + A[2]*B[3];
    out[3] = A[1]*B[2] + A[3]*B[3];
    out[4] = A[0]*B[4] + A[2]*B[5] + A[4];
    out[5] = A[1]*B[4] + A[3]*B[5] + A[5];
}

int GetNextSVGFloat(char** pp, double* val) {
    char* p = *pp;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) p++;
    if (!*p || *p == '"' || *p == '\'' || *p == ')' || *p == '<' || *p == '>') return 0;
    if (isalpha(*p) && *p != 'e' && *p != 'E') return 0; 
    *val = strtod(p, &p);
    *pp = p;
    return 1;
}

void ParseTransform(char* str, double* mat, char limitChar) {
    char* p = str; double t[6], temp[6]; int i;
    while (p && *p && *p != limitChar) {
        if (strncmp(p, "matrix", 6) == 0) {
            p += 6; while(*p && *p != '(' && *p != limitChar) p++; if(*p=='(') p++;
            for(i=0; i<6; i++) { if (!GetNextSVGFloat(&p, &t[i])) t[i] = (i==0||i==3)?1:0; }
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else if (strncmp(p, "translate", 9) == 0) {
            p += 9; while(*p && *p != '(' && *p != limitChar) p++; if(*p=='(') p++;
            for(i=0; i<6; i++) t[i] = (i==0||i==3)?1:0;
            GetNextSVGFloat(&p, &t[4]);
            if (!GetNextSVGFloat(&p, &t[5])) t[5] = 0;
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5; while(*p && *p != '(' && *p != limitChar) p++; if(*p=='(') p++;
            for(i=0; i<6; i++) t[i] = (i==0||i==3)?1:0;
            GetNextSVGFloat(&p, &t[0]);
            if (!GetNextSVGFloat(&p, &t[3])) t[3] = t[0];
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else {
            p++;
        }
    }
}

char* Attr(char* tagStr, char* tagEnd, const char* attrName) {
    char search[32]; char* a; int i;
    const char* quotes = "\"'";
    for (i = 0; i < 2; i++) {
        sprintf(search, " %s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "\n%s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "\t%s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "<%s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
    }
    return NULL;
}

double AttrF(char* tagStr, char* tagEnd, const char* attrName, double defVal) {
    char* valStr = Attr(tagStr, tagEnd, attrName);
    if (valStr) return atof(valStr);
    return defVal;
}

void GetSVGColors(char* p, char* tagEnd, COLORREF* fill, COLORREF* stroke, int* uF, int* uS, int defFill) {
    char* a = strstr(p, "fill:#"); int r, g, b;
    *uF = defFill; *uS = 1; *fill = RGB(128,128,128); *stroke = RGB(0,0,0);
    
    if (a && a < tagEnd && sscanf(a+6, "%02x%02x%02x", &r, &g, &b) == 3) { *fill = RGB(r, g, b); *uF = 1; }
    else if (strstr(p, "fill:none") && strstr(p, "fill:none") < tagEnd) { *uF = 0; }
    else {
        a = Attr(p, tagEnd, "fill");
        if (a && *a == '#' && sscanf(a+1, "%02x%02x%02x", &r, &g, &b) == 3) { *fill = RGB(r, g, b); *uF = 1; }
        else if (a && strncmp(a, "none", 4) == 0) *uF = 0;
    }
    
    a = strstr(p, "stroke:#");
    if (a && a < tagEnd && sscanf(a+8, "%02x%02x%02x", &r, &g, &b) == 3) { *stroke = RGB(r, g, b); *uS = 1; }
    else if (strstr(p, "stroke:none") && strstr(p, "stroke:none") < tagEnd) { *uS = 0; }
    else {
        a = Attr(p, tagEnd, "stroke");
        if (a && *a == '#' && sscanf(a+1, "%02x%02x%02x", &r, &g, &b) == 3) { *stroke = RGB(r, g, b); *uS = 1; }
        else if (a && strncmp(a, "none", 4) == 0) *uS = 0;
    }
}

void LoadSVG(const char* path, HWND hwnd, double customScale) {
    FILE* f; long sz; char *d, *p, *tagEnd, *transStr, *dStr, *ptsStr; 
    int i, isPolyline, sp = 0, oldShapeCount; double matStack[10][6], cMat[6]; 
    static Shape s;
    double rx, ry, rw, rh, px, py;
    
    f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > 60000L || sz <= 0) { fclose(f); return; }
    d = (char*)GlobalAllocPtr(GHND, (size_t)sz + 1);
    if (!d) { fclose(f); return; }
    fread(d, 1, (size_t)sz, f); d[sz] = '\0'; fclose(f);
    
    for(i=0;i<6;i++) matStack[0][i] = (i==0||i==3)?1:0;
    SaveState(); p = d; oldShapeCount = shapeCount;
    
    while ((p = strchr(p, '<')) != NULL && shapeCount < MAX_SHAPES) {
        p++;
        if (strncmp(p, "/g", 2) == 0) { if (sp > 0) sp--; continue; }
        tagEnd = strchr(p, '>'); if (!tagEnd) break;
        
        for(i=0;i<6;i++) cMat[i] = matStack[sp][i];
        transStr = Attr(p, tagEnd, "transform");
        if (transStr) ParseTransform(transStr, cMat, '"');
        
        if (p[0] == 'g' && (isspace(p[1]) || p[1] == '>')) {
            if (sp < 9) { sp++; for(i=0;i<6;i++) matStack[sp][i] = cMat[i]; }
        } else if (strncmp(p, "rect", 4) == 0) {
            rx = AttrF(p, tagEnd, "x", 0.0); ry = AttrF(p, tagEnd, "y", 0.0);
            rw = AttrF(p, tagEnd, "width", 0.0); rh = AttrF(p, tagEnd, "height", 0.0);
            if (rw > 0 && rh > 0) {
                memset(&s, 0, sizeof(Shape)); s.type = 0; s.ptCount = 4;
                GetSVGColors(p, tagEnd, &s.fill, &s.stroke, &s.useFill, &s.useStroke, 1);
                double px0=rx, py0=ry, px1=rx+rw, py1=ry, px2=rx+rw, py2=ry+rh, px3=rx, py3=ry+rh;
                s.ptsX[0] = (cMat[0]*px0 + cMat[2]*py0 + cMat[4]); s.ptsY[0] = (cMat[1]*px0 + cMat[3]*py0 + cMat[5]);
                s.ptsX[1] = (cMat[0]*px1 + cMat[2]*py1 + cMat[4]); s.ptsY[1] = (cMat[1]*px1 + cMat[3]*py1 + cMat[5]);
                s.ptsX[2] = (cMat[0]*px2 + cMat[2]*py2 + cMat[4]); s.ptsY[2] = (cMat[1]*px2 + cMat[3]*py2 + cMat[5]);
                s.ptsX[3] = (cMat[0]*px3 + cMat[2]*py3 + cMat[4]); s.ptsY[3] = (cMat[1]*px3 + cMat[3]*py3 + cMat[5]);
                shapes[shapeCount++] = s;
            }
        } else if (strncmp(p, "poly", 4) == 0) {
            isPolyline = (strncmp(p, "polyline", 8) == 0);
            ptsStr = Attr(p, tagEnd, "points");
            if (ptsStr) {
                memset(&s, 0, sizeof(Shape)); s.type = isPolyline ? 2 : 0;
                GetSVGColors(p, tagEnd, &s.fill, &s.stroke, &s.useFill, &s.useStroke, !isPolyline);
                while (s.ptCount < MAX_POINTS) {
                    if (!GetNextSVGFloat(&ptsStr, &px)) break;
                    if (!GetNextSVGFloat(&ptsStr, &py)) break;
                    s.ptsX[s.ptCount] = (cMat[0]*px + cMat[2]*py + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*px + cMat[3]*py + cMat[5]); s.ptCount++;
                }
                if (s.ptCount >= 2) shapes[shapeCount++] = s;
            }
        } else if (strncmp(p, "path", 4) == 0) {
            dStr = Attr(p, tagEnd, "d");
            if (dStr) {
                memset(&s, 0, sizeof(Shape)); s.type = 0;
                GetSVGColors(p, tagEnd, &s.fill, &s.stroke, &s.useFill, &s.useStroke, 1);
                double curX = 0, curY = 0; char cmd = 'M';
                while (*dStr && *dStr != '"' && s.ptCount < MAX_POINTS) {
                    while (*dStr && isspace(*dStr)) dStr++;
                    if (isalpha(*dStr)) { cmd = *dStr; dStr++; }
                    if (toupper(cmd) == 'Z') break;
                    
                    double argX, argY, ctrlX, ctrlY;
                    if (toupper(cmd) == 'M' || toupper(cmd) == 'L') {
                        if (!GetNextSVGFloat(&dStr, &argX) || !GetNextSVGFloat(&dStr, &argY)) break;
                        if (cmd == 'm' || cmd == 'l') { argX += curX; argY += curY; }
                        curX = argX; curY = argY;
                        s.ptsX[s.ptCount] = (cMat[0]*curX + cMat[2]*curY + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*curX + cMat[3]*curY + cMat[5]); s.ptCount++;
                        if (toupper(cmd) == 'M') cmd = (cmd == 'M') ? 'L' : 'l';
                    } else if (toupper(cmd) == 'H') {
                        if (!GetNextSVGFloat(&dStr, &argX)) break;
                        if (cmd == 'h') argX += curX; curX = argX;
                        s.ptsX[s.ptCount] = (cMat[0]*curX + cMat[2]*curY + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*curX + cMat[3]*curY + cMat[5]); s.ptCount++;
                    } else if (toupper(cmd) == 'V') {
                        if (!GetNextSVGFloat(&dStr, &argY)) break;
                        if (cmd == 'v') argY += curY; curY = argY;
                        s.ptsX[s.ptCount] = (cMat[0]*curX + cMat[2]*curY + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*curX + cMat[3]*curY + cMat[5]); s.ptCount++;
                    } else if (toupper(cmd) == 'Q') {
                        if (!GetNextSVGFloat(&dStr, &ctrlX) || !GetNextSVGFloat(&dStr, &ctrlY) || !GetNextSVGFloat(&dStr, &argX) || !GetNextSVGFloat(&dStr, &argY)) break;
                        if (cmd == 'q') { ctrlX += curX; ctrlY += curY; argX += curX; argY += curY; }
                        double t, px_curve, py_curve; int step;
                        for (step = 1; step <= 4 && s.ptCount < MAX_POINTS; step++) {
                            t = step / 4.0; px_curve = (1-t)*(1-t)*curX + 2*(1-t)*t*ctrlX + t*t*argX; py_curve = (1-t)*(1-t)*curY + 2*(1-t)*t*ctrlY + t*t*argY;
                            s.ptsX[s.ptCount] = (cMat[0]*px_curve + cMat[2]*py_curve + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*px_curve + cMat[3]*py_curve + cMat[5]); s.ptCount++;
                        }
                        curX = argX; curY = argY;
                    } else if (toupper(cmd) == 'C') {
                        double cx1, cy1, cx2, cy2;
                        if (!GetNextSVGFloat(&dStr, &cx1) || !GetNextSVGFloat(&dStr, &cy1) || !GetNextSVGFloat(&dStr, &cx2) || !GetNextSVGFloat(&dStr, &cy2) || !GetNextSVGFloat(&dStr, &argX) || !GetNextSVGFloat(&dStr, &argY)) break;
                        if (cmd == 'c') { cx1+=curX; cy1+=curY; cx2+=curX; cy2+=curY; argX+=curX; argY+=curY; }
                        double t, px_curve, py_curve; int step;
                        for (step = 1; step <= 6 && s.ptCount < MAX_POINTS; step++) {
                            t = step / 6.0; px_curve = (1-t)*(1-t)*(1-t)*curX + 3*(1-t)*(1-t)*t*cx1 + 3*(1-t)*t*t*cx2 + t*t*t*argX; py_curve = (1-t)*(1-t)*(1-t)*curY + 3*(1-t)*(1-t)*t*cy1 + 3*(1-t)*t*t*cy2 + t*t*t*argY;
                            s.ptsX[s.ptCount] = (cMat[0]*px_curve + cMat[2]*py_curve + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*px_curve + cMat[3]*py_curve + cMat[5]); s.ptCount++;
                        }
                        curX = argX; curY = argY;
                    } else {
                        GetNextSVGFloat(&dStr, &argX);
                    }
                }
                if (s.ptCount >= 2) shapes[shapeCount++] = s;
            }
        }
    }
    GlobalFreePtr(d);
    
    if (shapeCount > oldShapeCount) {
        double minX = 99999.0, maxX = -99999.0, minY = 99999.0, maxY = -99999.0;
        int si, pi;
        double w, h, scale, offsetX, offsetY;
        for(si = oldShapeCount; si < shapeCount; si++) {
            for(pi = 0; pi < shapes[si].ptCount; pi++) {
                minX = fmin(minX, shapes[si].ptsX[pi]); maxX = fmax(maxX, shapes[si].ptsX[pi]);
                minY = fmin(minY, shapes[si].ptsY[pi]); maxY = fmax(maxY, shapes[si].ptsY[pi]);
            }
        }
        w = maxX - minX; h = maxY - minY;
        
        if (customScale < 0.0) { 
            if (w > 0 && h > 0) {
                scale = fmin((GRID_SIZE * 0.9) / w, (GRID_SIZE * 0.9) / h);
                offsetX = (GRID_SIZE - w * scale) / 2.0 - minX * scale;
                offsetY = (GRID_SIZE - h * scale) / 2.0 - minY * scale;
            } else { scale = 1.0; offsetX = 0; offsetY = 0; }
        } else {
            scale = customScale; offsetX = 0; offsetY = 0;
        }
        
        for(si = oldShapeCount; si < shapeCount; si++) {
            for(pi = 0; pi < shapes[si].ptCount; pi++) {
                shapes[si].ptsX[pi] = shapes[si].ptsX[pi] * scale + offsetX;
                shapes[si].ptsY[pi] = shapes[si].ptsY[pi] * scale + offsetY;
            }
        }
    }
    UpdateStatusBar(); RedrawCanvas(hwnd);
}

/* --- Math & Geometry --- */
void PtToSegProj(double px, double py, double x1, double y1, double x2, double y2, double* prX, double* prY, double* dist) {
    double l2 = (x2-x1)*(x2-x1) + (y2-y1)*(y2-y1); 
    double t;
    if (l2 == 0) { *prX = x1; *prY = y1; *dist = sqrt((px-x1)*(px-x1) + (py-y1)*(py-y1)); return; }
    t = fmax(0.0, fmin(1.0, ((px-x1)*(x2-x1) + (py-y1)*(y2-y1)) / l2)); 
    *prX = x1 + t*(x2-x1); *prY = y1 + t*(y2-y1); 
    *dist = sqrt((px-*prX)*(px-*prX) + (py-*prY)*(py-*prY));
}

int PointInPolyShape(double px, double py, Shape* s) {
    int c = 0, n = s->ptCount, i, j;
    if (n < 3) return 0;
    for (i = 0, j = n - 1; i < n; j = i++) {
        if (((s->ptsY[i] > py) != (s->ptsY[j] > py))) {
            double dy = s->ptsY[j] - s->ptsY[i];
            if (dy != 0) {
                if (px < (s->ptsX[j] - s->ptsX[i]) * (py - s->ptsY[i]) / dy + s->ptsX[i]) {
                    c = !c;
                }
            }
        }
    }
    return c;
}

void AddEdgesFromShape(Shape* s1, Shape* s2, Edge* pool, int* edgeCount) {
    int i, j, k, n1 = s1->ptCount, n2 = s2->ptCount;
    double x1, y1, x2, y2, x3, y3, x4, y4;
    double den, t, u, ix, iy;
    
    double* cutsX = (double*)GlobalAllocPtr(GHND, MAX_POINTS * sizeof(double));
    double* cutsY = (double*)GlobalAllocPtr(GHND, MAX_POINTS * sizeof(double));
    int cutCount = 0;

    if (!cutsX || !cutsY) {
        if (cutsX) GlobalFreePtr(cutsX);
        if (cutsY) GlobalFreePtr(cutsY);
        return;
    }

    for (i = 0; i < n1; i++) {
        int next1 = (i + 1) % n1;
        x1 = s1->ptsX[i]; y1 = s1->ptsY[i];
        x2 = s1->ptsX[next1]; y2 = s1->ptsY[next1];
        
        cutCount = 0;
        cutsX[cutCount] = x1; cutsY[cutCount] = y1; cutCount++;
        cutsX[cutCount] = x2; cutsY[cutCount] = y2; cutCount++;

        for (j = 0; j < n2; j++) {
            int next2 = (j + 1) % n2;
            x3 = s2->ptsX[j]; y3 = s2->ptsY[j];
            x4 = s2->ptsX[next2]; y4 = s2->ptsY[next2];

            den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
            if (fabs(den) > 1e-8) {
                t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / den;
                u = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / den;
                if (t > 1e-5 && t < 1 - 1e-5 && u > -1e-5 && u < 1 + 1e-5) {
                    ix = x1 + t * (x2 - x1);
                    iy = y1 + t * (y2 - y1);
                    if (cutCount < MAX_POINTS) {
                        cutsX[cutCount] = ix; cutsY[cutCount] = iy; cutCount++;
                    }
                }
            }
        }

        for (j = 0; j < n2; j++) {
            double ppx = s2->ptsX[j], ppy = s2->ptsY[j];
            double prX, prY, dist;
            PtToSegProj(ppx, ppy, x1, y1, x2, y2, &prX, &prY, &dist);
            if (dist < 1e-4) {
                double d_total = sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
                double d1 = sqrt((ppx-x1)*(ppx-x1) + (ppy-y1)*(ppy-y1));
                double d2 = sqrt((ppx-x2)*(ppx-x2) + (ppy-y2)*(ppy-y2));
                if (d1 > 1e-4 && d2 > 1e-4 && d1 + d2 < d_total + 1e-4) {
                    if (cutCount < MAX_POINTS) {
                        cutsX[cutCount] = ppx; cutsY[cutCount] = ppy; cutCount++;
                    }
                }
            }
        }

        for (j = 0; j < cutCount - 1; j++) {
            for (k = j + 1; k < cutCount; k++) {
                double d_j = (cutsX[j]-x1)*(cutsX[j]-x1) + (cutsY[j]-y1)*(cutsY[j]-y1);
                double d_k = (cutsX[k]-x1)*(cutsX[k]-x1) + (cutsY[k]-y1)*(cutsY[k]-y1);
                if (d_j > d_k) {
                    double tx_c = cutsX[j]; cutsX[j] = cutsX[k]; cutsX[k] = tx_c;
                    double ty_c = cutsY[j]; cutsY[j] = cutsY[k]; cutsY[k] = ty_c;
                }
            }
        }

        for (j = 0; j < cutCount - 1; j++) {
            double sx1 = cutsX[j], sy1 = cutsY[j];
            double sx2 = cutsX[j+1], sy2 = cutsY[j+1];
            if ((sx1 - sx2)*(sx1 - sx2) + (sy1 - sy2)*(sy1 - sy2) > 1e-8) {
                double midX = (sx1 + sx2) / 2.0;
                double midY = (sy1 + sy2) / 2.0;
                if (!PointInPolyShape(midX, midY, s2)) {
                    if (*edgeCount < 1024) {
                        pool[*edgeCount].x1 = sx1; pool[*edgeCount].y1 = sy1;
                        pool[*edgeCount].x2 = sx2; pool[*edgeCount].y2 = sy2;
                        (*edgeCount)++;
                    }
                }
            }
        }
    }

    GlobalFreePtr(cutsX);
    GlobalFreePtr(cutsY);
}

void GenShape(double eX, double eY) {
    int i; double cx, cy, rx, ry;
    if (currentMode == 4) {
        currentShape.ptCount = 2; currentShape.ptsX[0] = startX; currentShape.ptsY[0] = startY;
        currentShape.ptsX[1] = eX; currentShape.ptsY[1] = eY; return;
    }
    cx = (startX + eX) / 2.0; cy = (startY + eY) / 2.0; 
    rx = fabs(eX - startX) / 2.0; ry = fabs(eY - startY) / 2.0;
    currentShape.ptCount = paramSides; 
    
    if (paramSides == 4 && paramStar == 100) { 
        currentShape.ptsX[0]=fmin(startX, eX); currentShape.ptsY[0]=fmin(startY, eY); 
        currentShape.ptsX[1]=fmax(startX, eX); currentShape.ptsY[1]=fmin(startY, eY); 
        currentShape.ptsX[2]=fmax(startX, eX); currentShape.ptsY[2]=fmax(startY, eY); 
        currentShape.ptsX[3]=fmin(startX, eX); currentShape.ptsY[3]=fmax(startY, eY); 
        return; 
    }
    for (i = 0; i < paramSides; i++) { 
        double ang = i * (2.0 * PI / paramSides) - (PI / 2.0);
        double rF = (paramSides>4 && paramStar<100 && i%2!=0) ? fmax(0.2, paramStar/100.0) : 1.0; 
        currentShape.ptsX[i] = cx + cos(ang) * rx * rF; currentShape.ptsY[i] = cy + sin(ang) * ry * rF; 
    }
}

/* --- Subclassed Edit Control --- */
LRESULT FAR PASCAL _export DistEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(GetParent(hwnd), WM_APP + 1, 0, 0L);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        ShowWindow(hwnd, SW_HIDE); SetFocus(GetParent(hwnd)); return 0;
    }
    return CallWindowProc((FARPROC)oldEditProc, hwnd, msg, wParam, lParam);
}

/* --- GDI Rendering Functions --- */
void DrawGrid(HDC dc) {
    int i; HPEN hPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
    HPEN hOldPen = SelectObject(dc, hPen);
    for (i = 0; i <= GRID_SIZE; i++) {
        MoveTo(dc, 0, i * scaleFactor); LineTo(dc, canvasSize, i * scaleFactor);
        MoveTo(dc, i * scaleFactor, 0); LineTo(dc, i * scaleFactor, canvasSize);
    }
    SelectObject(dc, hOldPen); DeleteObject(hPen);
}

void DrawPalette(HDC dc) {
    int i, col, row, cx = canvasSize + 15;
    HBRUSH hBr, hOldBr; HPEN hPen = (HPEN)GetStockObject(BLACK_PEN), hOldPen = SelectObject(dc, hPen);
    
    for (i = 0; i < 16; i++) {
        col = i % 8; row = i / 8;
        hBr = CreateSolidBrush(palette[i]); hOldBr = SelectObject(dc, hBr);
        Rectangle(dc, cx + col * 32, 382 + row * 16, cx + col * 32 + 32, 382 + row * 16 + 16);
        SelectObject(dc, hOldBr); DeleteObject(hBr);
    }
    
    hBr = useFill ? CreateSolidBrush(currentFill) : (HBRUSH)GetStockObject(NULL_BRUSH);
    hOldBr = SelectObject(dc, hBr); Rectangle(dc, cx, 422, cx + 32, 454);
    SelectObject(dc, hOldBr); if (useFill) DeleteObject(hBr);
    
    hBr = useStroke ? CreateSolidBrush(currentStroke) : (HBRUSH)GetStockObject(NULL_BRUSH);
    hOldBr = SelectObject(dc, hBr); Rectangle(dc, cx + 80, 422, cx + 112, 454);
    SelectObject(dc, hOldBr); if (useStroke) DeleteObject(hBr);
    
    SelectObject(dc, hOldPen); SetBkMode(dc, TRANSPARENT);
    TextOut(dc, cx + 40, 430, "Fill", 4); TextOut(dc, cx + 120, 430, "Stroke", 6);
    TextOut(dc, cx, 466, "Sides:", 6); TextOut(dc, cx, 491, "Depth:", 6);
}

void RenderShapes(HDC dc, Shape* sArr, int sCnt, int sc, int offX, int offY, Shape* activeShape, int isActDrawing) {
    int i, j;
    for (i = 0; i <= sCnt; i++) {
        Shape* s = (i == sCnt) ? (isActDrawing ? activeShape : NULL) : &sArr[i]; 
        HBRUSH b; HPEN p; HGDIOBJ ob, op; POINT pA[MAX_POINTS];
        if (!s || s->ptCount == 0) continue;
        
        b = s->useFill ? CreateSolidBrush(s->fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
        p = s->useStroke ? CreatePen(PS_SOLID, 1, s->stroke) : (HPEN)GetStockObject(NULL_PEN);
        ob = SelectObject(dc, b); op = SelectObject(dc, p);
        
        for(j=0; j<s->ptCount; j++) { 
            pA[j].x = offX + (int)round(s->ptsX[j] * sc); 
            pA[j].y = offY + (int)round(s->ptsY[j] * sc); 
        }
        
        if (s->type == 0) { SetPolyFillMode(dc, ALTERNATE); Polygon(dc, pA, s->ptCount); } 
        else Polyline(dc, pA, s->ptCount);
        
        SelectObject(dc, ob); SelectObject(dc, op);
        if (s->useFill) DeleteObject(b); if (s->useStroke) DeleteObject(p);
    }
}

void DrawPreview(HDC dc) {
    int cx = canvasSize + 15;
    int px = cx + 220, py = 420; 
    HBRUSH hBr = (HBRUSH)GetStockObject(WHITE_BRUSH);
    HPEN hPen = (HPEN)GetStockObject(BLACK_PEN);
    HGDIOBJ hOldBr = SelectObject(dc, hBr);
    HGDIOBJ hOldPen = SelectObject(dc, hPen);
    
    Rectangle(dc, px, py, px + 34, py + 34);
    SelectObject(dc, hOldBr); SelectObject(dc, hOldPen);
    
    if (shapes) RenderShapes(dc, shapes, shapeCount, 1, px + 1, py + 1, NULL, 0);
    SetBkMode(dc, TRANSPARENT); TextOut(dc, px - 60, py + 8, "Preview:", 8);
}

void DrawNodes(HDC dc) {
    int j, i, px, py;
    HBRUSH hSelBr = CreateSolidBrush(RGB(255, 0, 0));
    HBRUSH hUnselBr = CreateSolidBrush(RGB(0, 0, 255));
    HBRUSH hHovBr = CreateSolidBrush(RGB(255, 255, 0));
    HBRUSH hEdgeBr = CreateSolidBrush(RGB(0, 255, 255));
    HBRUSH hOrigBr = (HBRUSH)SelectObject(dc, hUnselBr);
    HPEN hOrigPen = (HPEN)SelectObject(dc, GetStockObject(BLACK_PEN));

    if (shapes) {
        for (i = 0; i < shapeCount; i++) {
            int drawNodes = (i == selectedShape || i == hoverShape);
            for (j = 0; j < shapes[i].ptCount; j++) if (ptSelected[i][j]) drawNodes = 1;
            
            if (drawNodes) {
                for (j = 0; j < shapes[i].ptCount; j++) {
                    px = (int)round(shapes[i].ptsX[j] * scaleFactor);
                    py = (int)round(shapes[i].ptsY[j] * scaleFactor);
                    if (ptSelected[i][j]) SelectObject(dc, hSelBr);
                    else if (hoverShape == i && hoverPt == j) SelectObject(dc, hHovBr);
                    else SelectObject(dc, hUnselBr);
                    Rectangle(dc, px - 3, py - 3, px + 4, py + 4);
                }
            }
        }
        if (hoverSegShape != -1) {
            SelectObject(dc, hEdgeBr);
            Ellipse(dc, (int)hoverProjX - 4, (int)hoverProjY - 4, (int)hoverProjX + 5, (int)hoverProjY + 5);
        }
    }
    SelectObject(dc, hOrigBr); SelectObject(dc, hOrigPen);
    DeleteObject(hSelBr); DeleteObject(hUnselBr); DeleteObject(hHovBr); DeleteObject(hEdgeBr);
}

void DrawDimensions(HDC dc) {
    int i; 
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 128, 255));
    HBRUSH hBr = CreateSolidBrush(RGB(0, 128, 255));
    HPEN hHandlePen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100)); 
    HBRUSH hHandleBr = CreateSolidBrush(RGB(200, 200, 200)); 
    HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
    HGDIOBJ oldPen = SelectObject(dc, hPen);
    HGDIOBJ oldBr = SelectObject(dc, hBr);
    HGDIOBJ oldFont = SelectObject(dc, hFont);
    
    SetTextColor(dc, RGB(0, 128, 255));
    SetBkMode(dc, TRANSPARENT);

    for (i = 0; i < dimCount; i++) {
        double A1x, A1y, A2x, A2y;
        double D1x, D1y, D2x, D2y;
        double dx, dy, ang, nx, ny, val;
        char buf[32];
        
        /* Skip rendering if shapes/points were deleted to prevent crashes */
        if (dims[i].s1 >= shapeCount || dims[i].s2 >= shapeCount || 
            dims[i].p1 >= shapes[dims[i].s1].ptCount || dims[i].p2 >= shapes[dims[i].s2].ptCount) {
            continue;
        }

        /* Dynamically pull coordinates from the connected nodes */
        A1x = shapes[dims[i].s1].ptsX[dims[i].p1] * scaleFactor;
        A1y = shapes[dims[i].s1].ptsY[dims[i].p1] * scaleFactor;
        A2x = shapes[dims[i].s2].ptsX[dims[i].p2] * scaleFactor;
        A2y = shapes[dims[i].s2].ptsY[dims[i].p2] * scaleFactor;
        
        dx = A2x - A1x; dy = A2y - A1y;
        
        if (dims[i].mode == 0) { 
            ang = atan2(dy, dx);
            nx = -sin(ang); ny = cos(ang);
            D1x = A1x + nx * dims[i].offset; D1y = A1y + ny * dims[i].offset;
            D2x = A2x + nx * dims[i].offset; D2y = A2y + ny * dims[i].offset;
            val = sqrt(pow(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1], 2) + pow(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1], 2));
        } else if (dims[i].mode == 1) { 
            D1x = A1x; D1y = A1y + dims[i].offset;
            D2x = A2x; D2y = A1y + dims[i].offset;
            val = fabs(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1]);
        } else { 
            D1x = A1x + dims[i].offset; D1y = A1y;
            D2x = A1x + dims[i].offset; D2y = A2y;
            val = fabs(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1]);
        }
        
        sprintf(buf, "%.2f", val);

        /* Gap and Leader Lines */
        {
            double L1dx = D1x - A1x, L1dy = D1y - A1y;
            double L1len = sqrt(L1dx*L1dx + L1dy*L1dy);
            if (L1len > 5.0) {
                MoveTo(dc, (int)(A1x + L1dx/L1len*5.0), (int)(A1y + L1dy/L1len*5.0));
                LineTo(dc, (int)(D1x + L1dx/L1len*2.0), (int)(D1y + L1dy/L1len*2.0));
            }
            double L2dx = D2x - A2x, L2dy = D2y - A2y;
            double L2len = sqrt(L2dx*L2dx + L2dy*L2dy);
            if (L2len > 5.0) {
                MoveTo(dc, (int)(A2x + L2dx/L2len*5.0), (int)(A2y + L2dy/L2len*5.0));
                LineTo(dc, (int)(D2x + L2dx/L2len*2.0), (int)(D2y + L2dy/L2len*2.0));
            }
        }

        /* Dimension Main Line */
        MoveTo(dc, (int)D1x, (int)D1y); LineTo(dc, (int)D2x, (int)D2y);
        
        /* ANSI Arrows */
        {
            double dimDx = D2x - D1x, dimDy = D2y - D1y;
            double dimLen = sqrt(dimDx*dimDx + dimDy*dimDy);
            if (dimLen > 0) {
                double dirX = dimDx/dimLen, dirY = dimDy/dimLen;
                POINT pts[3];
                pts[0].x = (int)D1x; pts[0].y = (int)D1y;
                pts[1].x = (int)(D1x + dirX*10 - dirY*3); pts[1].y = (int)(D1y + dirY*10 + dirX*3);
                pts[2].x = (int)(D1x + dirX*10 + dirY*3); pts[2].y = (int)(D1y + dirY*10 - dirX*3);
                Polygon(dc, pts, 3);
                
                pts[0].x = (int)D2x; pts[0].y = (int)D2y;
                pts[1].x = (int)(D2x - dirX*10 - dirY*3); pts[1].y = (int)(D2y - dirY*10 + dirX*3);
                pts[2].x = (int)(D2x - dirX*10 + dirY*3); pts[2].y = (int)(D2y - dirY*10 - dirX*3);
                Polygon(dc, pts, 3);
            }
        }

        /* Text Label and Handle Box */
        {
            double midX = D1x + (D2x - D1x) * dims[i].textPos;
            double midY = D1y + (D2y - D1y) * dims[i].textPos;
            DWORD ext;
            int tW, tH, tx, ty, hx, hy;
            RECT tR;
            
            ext = GetTextExtent(dc, buf, strlen(buf));
            tW = LOWORD(ext);
            tH = HIWORD(ext);
            tx = (int)(midX - tW/2.0);
            ty = (int)(midY - tH/2.0);
            
            tR.left = tx - 2;
            tR.top = ty - 2;
            tR.right = tx + tW + 2;
            tR.bottom = ty + tH + 2;
            
            /* Text Box Background */
            FillRect(dc, &tR, (HBRUSH)GetStockObject(WHITE_BRUSH));
            TextOut(dc, tx, ty, buf, strlen(buf));
            
            /* Draggable handle separated to the right of the text */
            hx = tx + tW + 8;
            hy = (int)midY;
            
            SelectObject(dc, hHandlePen);
            SelectObject(dc, hHandleBr);
            Rectangle(dc, hx - 4, hy - 4, hx + 5, hy + 5);
            
            /* Restore Dimension Colors */
            SelectObject(dc, hPen);
            SelectObject(dc, hBr);
        }
    }
    
    SelectObject(dc, oldPen); DeleteObject(hPen); DeleteObject(hHandlePen);
    SelectObject(dc, oldBr); DeleteObject(hBr); DeleteObject(hHandleBr);
    SelectObject(dc, oldFont); DeleteObject(hFont);
}

/* --- Main Windows Procedure --- */
LRESULT FAR PASCAL _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static double dragStartX = 0, dragStartY = 0;
    static double shapeCx = 0, shapeCy = 0;
    static int dragType = 0;
    static int isDraggingPoint = 0;

    switch (msg) {
        case WM_CREATE: {
            int i; int allocFailed = 0;
            
            shapes = (Shape*)GlobalAllocPtr(GHND, MAX_SHAPES * sizeof(Shape));
            dragStartSnapshot = (Shape*)GlobalAllocPtr(GHND, MAX_SHAPES * sizeof(Shape));
            for(i=0; i<MAX_UNDO; i++) {
                history[i] = (Shape*)GlobalAllocPtr(GHND, MAX_SHAPES * sizeof(Shape));
                if (!history[i]) allocFailed = 1;
            }
            
            if (!shapes || !dragStartSnapshot || allocFailed) {
                MessageBox(hwnd, "Failed to allocate memory on Far Heap!", "Error", MB_ICONHAND);
                PostQuitMessage(0); return -1;
            }

            InitFont();

            parsedCount = 1;
            currentIconIdx = -1; 
            currentCaseId = 1;
            parsedIcons[0].caseId = 1;
            strcpy(parsedIcons[0].name, "New");
            parsedIcons[0].shapes = NULL;
            parsedIcons[0].shapeCount = 0;

            hStatus = CreateWindow("STATIC", " Ready", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)100, hInst, NULL);
            hScrlSides = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)153, hInst, NULL);
            hScrlDepth = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)154, hInst, NULL);
            
            hBtnAddIcon = CreateWindow("BUTTON", "+", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)151, hInst, NULL);
            hScrlIcon  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)155, hInst, NULL);
            hBtnDelIcon = CreateWindow("BUTTON", "-", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)152, hInst, NULL);

            SetScrollRange(hScrlSides, SB_CTL, -5, 5, FALSE); SetScrollPos(hScrlSides, SB_CTL, 0, TRUE);
            SetScrollRange(hScrlDepth, SB_CTL, -5, 5, FALSE); SetScrollPos(hScrlDepth, SB_CTL, 0, TRUE);
            SetScrollRange(hScrlIcon,  SB_CTL, 0, 0, FALSE); SetScrollPos(hScrlIcon, SB_CTL, 0, TRUE);
            
            SwitchToIcon(0); 
            
            for(i = 0; i < 28; i++) hBtn[i] = CreateWindow("BUTTON", bT[i], WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)(200+i), hInst, NULL);
            hDistEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 60, 20, hwnd, (HMENU)300, hInst, NULL);
            
            subclassThunk = MakeProcInstance((FARPROC)DistEditProc, hInst);
            oldEditProc = (FARPROC)SetWindowLong(hDistEdit, GWL_WNDPROC, (LONG)subclassThunk);
            break;
        }
        case WM_SIZE: {
            int cx, w, by, i, availW, availH;
            HDWP hdwp;
            
            if (!hBtn[27] || !hScrlIcon || !hStatus || wParam == SIZE_MINIMIZED) {
                return DefWindowProc(hwnd, msg, wParam, lParam);
            }
            
            clientW = LOWORD(lParam); clientH = HIWORD(lParam); 
            availW = clientW - PANEL_WIDTH; availH = clientH - 100;
            
            if (availW < 10) availW = 10;
            if (availH < 10) availH = 10;
            
            scaleFactor = (availW < availH) ? (availW / GRID_SIZE) : (availH / GRID_SIZE);
            if (scaleFactor < 1) scaleFactor = 1; 
            canvasSize = scaleFactor * GRID_SIZE;
            
            cx = canvasSize + 15; w = PANEL_WIDTH - 30; by = 10;
            
            hdwp = BeginDeferWindowPos(32);
            if (hdwp) {
                for(i = 0; i < 28; i++) {
                    if (hBtn[i]) {
                        hdwp = DeferWindowPos(hdwp, hBtn[i], NULL, cx + (i%2)*(w/2 + 2), by + (i/2)*26, (w/2)-4, 24, SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                }
                hdwp = DeferWindowPos(hdwp, hScrlSides, NULL, cx + 50, 466, w - 50, 18, SWP_NOZORDER | SWP_NOACTIVATE);
                hdwp = DeferWindowPos(hdwp, hScrlDepth, NULL, cx + 50, 491, w - 50, 18, SWP_NOZORDER | SWP_NOACTIVATE);
                hdwp = DeferWindowPos(hdwp, hBtnAddIcon, NULL, cx, 516, 24, 20, SWP_NOZORDER | SWP_NOACTIVATE);
                hdwp = DeferWindowPos(hdwp, hScrlIcon, NULL, cx + 26, 516, w - 54, 20, SWP_NOZORDER | SWP_NOACTIVATE);
                hdwp = DeferWindowPos(hdwp, hBtnDelIcon, NULL, cx + w - 26, 516, 24, 20, SWP_NOZORDER | SWP_NOACTIVATE);
                hdwp = DeferWindowPos(hdwp, hStatus, NULL, 0, clientH - 20, clientW, 20, SWP_NOZORDER | SWP_NOACTIVATE);
                
                EndDeferWindowPos(hdwp);
            }
            
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        case WM_HSCROLL: {
            HWND hTrk = (HWND)(UINT)HIWORD(lParam);
            UINT code = wParam;
            int pos;
            int p;
            double minX, maxX, minY, maxY, cx, cy, rx, ry;
            int sides;
            
            if (hTrk == hScrlIcon) {
                pos = GetScrollPos(hScrlIcon, SB_CTL);
                switch(code) { 
                    case SB_LINELEFT: pos--; break; 
                    case SB_LINERIGHT: pos++; break; 
                    case SB_PAGELEFT: pos-=10; break; 
                    case SB_PAGERIGHT: pos+=10; break; 
                    case SB_THUMBTRACK: 
                    case SB_THUMBPOSITION: pos = LOWORD(lParam); break; 
                }
                if (pos < 0) pos = 0; 
                if (parsedCount > 0 && pos >= parsedCount) pos = parsedCount - 1;
                SwitchToIcon(pos);
            }
            else if (hTrk == hScrlSides || hTrk == hScrlDepth) {
                pos = GetScrollPos(hTrk, SB_CTL);
                if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) {
                    pos = (int)(short)LOWORD(lParam);
                } else if (code == SB_LINELEFT) {
                    pos = -1;
                } else if (code == SB_LINERIGHT) {
                    pos = 1;
                } else if (code == SB_PAGELEFT) {
                    pos = -3;
                } else if (code == SB_PAGERIGHT) {
                    pos = 3;
                } else {
                    pos = 0;
                }

                if (pos != 0) {
                    if (hTrk == hScrlSides) {
                        paramSides += pos;
                        if (paramSides < 3) paramSides = 3;
                        if (paramSides > 32) paramSides = 32;

                        if (selectedShape != -1 && shapes[selectedShape].type == 0) {
                            SaveState();
                            minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                            for (p = 0; p < shapes[selectedShape].ptCount; p++) {
                                minX = fmin(minX, shapes[selectedShape].ptsX[p]);
                                maxX = fmax(maxX, shapes[selectedShape].ptsX[p]);
                                minY = fmin(minY, shapes[selectedShape].ptsY[p]);
                                maxY = fmax(maxY, shapes[selectedShape].ptsY[p]);
                            }
                            cx = (minX + maxX) / 2.0;
                            cy = (minY + maxY) / 2.0;
                            rx = (maxX - minX) / 2.0; if (rx < 1) rx = 1;
                            ry = (maxY - minY) / 2.0; if (ry < 1) ry = 1;

                            shapes[selectedShape].ptCount = paramSides;
                            for (p = 0; p < paramSides; p++) {
                                double ang = p * (2.0 * PI / paramSides) - (PI / 2.0);
                                double rF = (paramSides > 4 && paramStar < 100 && p % 2 != 0) ? fmax(0.2, paramStar / 100.0) : 1.0;
                                shapes[selectedShape].ptsX[p] = cx + cos(ang) * rx * rF;
                                shapes[selectedShape].ptsY[p] = cy + sin(ang) * ry * rF;
                            }
                            RedrawCanvas(hwnd);
                        }
                    } else if (hTrk == hScrlDepth) {
                        paramStar += pos * 5;
                        if (paramStar < 10) paramStar = 10;
                        if (paramStar > 100) paramStar = 100;

                        if (selectedShape != -1 && shapes[selectedShape].type == 0) {
                            SaveState();
                            minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                            for (p = 0; p < shapes[selectedShape].ptCount; p++) {
                                minX = fmin(minX, shapes[selectedShape].ptsX[p]);
                                maxX = fmax(maxX, shapes[selectedShape].ptsX[p]);
                                minY = fmin(minY, shapes[selectedShape].ptsY[p]);
                                maxY = fmax(maxY, shapes[selectedShape].ptsY[p]);
                            }
                            cx = (minX + maxX) / 2.0;
                            cy = (minY + maxY) / 2.0;
                            rx = (maxX - minX) / 2.0; if (rx < 1) rx = 1;
                            ry = (maxY - minY) / 2.0; if (ry < 1) ry = 1;
                            sides = shapes[selectedShape].ptCount;

                            for (p = 0; p < sides; p++) {
                                double ang = p * (2.0 * PI / sides) - (PI / 2.0);
                                double rF = (sides > 4 && paramStar < 100 && p % 2 != 0) ? fmax(0.2, paramStar / 100.0) : 1.0;
                                shapes[selectedShape].ptsX[p] = cx + cos(ang) * rx * rF;
                                shapes[selectedShape].ptsY[p] = cy + sin(ang) * ry * rF;
                            }
                            RedrawCanvas(hwnd);
                        }
                    }
                    SetScrollPos(hTrk, SB_CTL, 0, TRUE);
                    UpdateStatusBar();
                }
            }
            break;
        }
        case WM_CHAR: {
            if (currentMode == 7 && textCursorActive) {
                char c = toupper((char)wParam); 
                int r, c_idx, bit, start_c, glyph;
                static Shape s;
                
                if (c == 13) { textCursorY += 6; textCursorX = startX; } 
                else if (c == 8) { textCursorX = fmax(0, textCursorX - 4); } 
                else if (font5x3[c] || c == ' ') {
                    if (c != ' ') {
                        SaveState(); glyph = font5x3[c];
                        for(r=0; r<5; r++) { 
                            int rowVal = (glyph >> ((4-r)*4)) & 0xF; start_c = -1;
                            for(c_idx=0; c_idx<=3; c_idx++) {
                                bit = (c_idx < 3) ? ((rowVal >> (2 - c_idx)) & 1) : 0;
                                if (bit && start_c == -1) start_c = c_idx;
                                else if (!bit && start_c != -1) {
                                    memset(&s, 0, sizeof(Shape));
                                    s.type = 0; s.ptCount = 4;
                                    s.fill = currentFill; s.stroke = currentStroke; s.useFill = useFill; s.useStroke = useStroke;
                                    s.ptsX[0] = textCursorX + start_c; s.ptsY[0] = textCursorY + r; 
                                    s.ptsX[1] = textCursorX + c_idx; s.ptsY[1] = textCursorY + r;
                                    s.ptsX[2] = textCursorX + c_idx; s.ptsY[2] = textCursorY + r + 1; 
                                    s.ptsX[3] = textCursorX + start_c; s.ptsY[3] = textCursorY + r + 1;
                                    if(shapeCount < MAX_SHAPES) shapes[shapeCount++] = s; start_c = -1;
                                }
                            }
                        }
                    } 
                    textCursorX += 4; UpdateStatusBar(); RedrawCanvas(hwnd);
                }
            }
            break;
        }
        case WM_KEYDOWN: {
            int i, j, k;
            if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) { Undo(hwnd); return 0; }
            if (wParam == VK_ESCAPE) { 
                if (distEditMode != 0 && IsWindowVisible(hDistEdit)) { ShowWindow(hDistEdit, SW_HIDE); distEditMode = 0; RedrawCanvas(hwnd); return 0; }
                if (isDrawing) { isDrawing = 0; currentShape.ptCount = 0; }
                if (shapeCount > 0) currentMode = 0; else currentMode = 3; 
                ClearSelection(); RedrawCanvas(hwnd); 
                return 0;
            }
            if (wParam == VK_RETURN) {
                if (currentMode == 0 && selOrderCount == 2) { SendMessage(hwnd, WM_COMMAND, 222, 0); return 0; } 
                if (isDrawing && (currentMode >= 3 && currentMode <= 5) && currentShape.ptCount >= 2) { 
                    SaveState(); shapes[shapeCount++] = currentShape; isDrawing = 0; currentShape.ptCount = 0; selectedShape = shapeCount - 1; currentMode = (shapeCount == 0) ? 3 : 0; UpdateStatusBar(); RedrawCanvas(hwnd); 
                    return 0;
                }
            }
            if (wParam == VK_DELETE || wParam == VK_BACK) {
                if (isDrawing && currentShape.ptCount > 0) { 
                    currentShape.ptCount--; if (currentShape.ptCount == 0) isDrawing = 0; RedrawCanvas(hwnd); 
                }
                else if (currentMode == 0 || currentMode == 1 || currentMode == 2) {
                    if (selOrderCount > 0) {
                        SaveState();
                        for (i = shapeCount - 1; i >= 0; i--) {
                            for (k = shapes[i].ptCount - 1; k >= 0; k--) {
                                if (ptSelected[i][k]) {
                                    for(j = k; j < shapes[i].ptCount - 1; j++) { shapes[i].ptsX[j] = shapes[i].ptsX[j+1]; shapes[i].ptsY[j] = shapes[i].ptsY[j+1]; }
                                    shapes[i].ptCount--;
                                }
                            }
                            if (shapes[i].ptCount < (shapes[i].type==0?3:2)) { 
                                for(k = i; k < shapeCount - 1; k++) shapes[k] = shapes[k+1]; 
                                shapeCount--; 
                            }
                        }
                        ClearSelection(); selectedShape = -1; 
                        if (shapeCount == 0) currentMode = 3;
                        UpdateStatusBar(); RedrawCanvas(hwnd);
                    }
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            int shiftDown, ctrlDown, pSelCount, origCount, i, j, k, hitS, hitP, sHasSel, allSel, isPartialSelection;
            double gx, gy, minX, maxX, minY, maxY;
            int hitDimDrag = -1, hitDimCycle = -1;
            int x = (int)(short)LOWORD(lParam); 
            int y = (int)(short)HIWORD(lParam);
            int cx = canvasSize + 15;
            
            isPartialSelection = 0;

            if (x >= cx && x < cx + 256 && y >= 382 && y < 414) {
                int index = ((y - 382) / 16) * 8 + (x - cx) / 32;
                currentFill = palette[index]; useFill = 1;
                if (selectedShape != -1) { SaveState(); shapes[selectedShape].fill = currentFill; shapes[selectedShape].useFill = 1; }
                InvalidateRect(hwnd, NULL, TRUE); return 0;
            }

            if (dimCount > 0) {
                HDC hdc = GetDC(hwnd);
                HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
                HGDIOBJ oldFont = SelectObject(hdc, hFont);

                for (i = 0; i < dimCount; i++) {
                    double A1x, A1y, A2x, A2y, D1x, D1y, D2x, D2y;
                    double midX, midY, val;
                    char buf[32];
                    DWORD ext;
                    int tW, tH, tx, ty, hx, hy;
                    
                    if (dims[i].s1 >= shapeCount || dims[i].s2 >= shapeCount || 
                        dims[i].p1 >= shapes[dims[i].s1].ptCount || dims[i].p2 >= shapes[dims[i].s2].ptCount) continue;

                    A1x = shapes[dims[i].s1].ptsX[dims[i].p1] * scaleFactor;
                    A1y = shapes[dims[i].s1].ptsY[dims[i].p1] * scaleFactor;
                    A2x = shapes[dims[i].s2].ptsX[dims[i].p2] * scaleFactor;
                    A2y = shapes[dims[i].s2].ptsY[dims[i].p2] * scaleFactor;
                    
                    if (dims[i].mode == 0) {
                        double dX = A2x - A1x, dY = A2y - A1y;
                        double ang = atan2(dY, dX);
                        double nX = -sin(ang), nY = cos(ang);
                        D1x = A1x + nX * dims[i].offset; D1y = A1y + nY * dims[i].offset;
                        D2x = A2x + nX * dims[i].offset; D2y = A2y + nY * dims[i].offset;
                        val = sqrt(pow(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1], 2) + pow(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1], 2));
                    } else if (dims[i].mode == 1) {
                        D1x = A1x; D1y = A1y + dims[i].offset;
                        D2x = A2x; D2y = A1y + dims[i].offset;
                        val = fabs(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1]);
                    } else {
                        D1x = A1x + dims[i].offset; D1y = A1y;
                        D2x = A1x + dims[i].offset; D2y = A2y;
                        val = fabs(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1]);
                    }
                    
                    midX = D1x + (D2x - D1x) * dims[i].textPos;
                    midY = D1y + (D2y - D1y) * dims[i].textPos;
                    
                    sprintf(buf, "%.2f", val);
                    ext = GetTextExtent(hdc, buf, strlen(buf));
                    tW = LOWORD(ext);
                    tH = HIWORD(ext);
                    tx = (int)(midX - tW/2.0);
                    ty = (int)(midY - tH/2.0);
                    hx = tx + tW + 8;
                    hy = (int)midY;
                    
                    if (x >= tx - 2 && x <= tx + tW + 2 && y >= ty - 2 && y <= ty + tH + 2) { hitDimDrag = i; break; }
                    if (x >= hx - 4 && x <= hx + 5 && y >= hy - 4 && y <= hy + 5) { hitDimCycle = i; break; }
                }

                SelectObject(hdc, oldFont);
                DeleteObject(hFont);
                ReleaseDC(hwnd, hdc);
            }

            if (hitDimDrag != -1) {
                dragDimIdx = hitDimDrag;
                SetFocus(hwnd); SetCapture(hwnd);
                return 0;
            } else if (hitDimCycle != -1) {
                dims[hitDimCycle].mode = (dims[hitDimCycle].mode + 1) % 3;
                RedrawCanvas(hwnd);
                return 0;
            }

            SetFocus(hwnd); SetCapture(hwnd);
            
            startX = CLAMP(Snap(x / (double)scaleFactor), 0, GRID_SIZE); 
            startY = CLAMP(Snap(y / (double)scaleFactor), 0, GRID_SIZE);
            gx = startX; gy = startY;

            if (currentMode == 7) { 
                textCursorX = gx; textCursorY = gy; textCursorActive = 1; InvalidateRect(hwnd, NULL, FALSE); return 0;
            }
            
            if (currentMode == 8) { 
                int fillHit = -1;
                double exactX = x / (double)scaleFactor;
                double exactY = y / (double)scaleFactor;
                for (i = shapeCount - 1; i >= 0; i--) {
                    if (shapes[i].type == 0 && PointInPolyShape(exactX, exactY, &shapes[i])) {
                        fillHit = i;
                        break;
                    }
                }
                if (fillHit != -1) {
                    SaveState();
                    shapes[fillHit].fill = currentFill;
                    shapes[fillHit].useFill = 1;
                    selectedShape = fillHit;
                    UpdateStatusBar();
                    RedrawCanvas(hwnd);
                }
                ReleaseCapture();
                return 0;
            }

            if (currentMode == 0 || currentMode == 1 || currentMode == 2) { 
                shiftDown = (GetKeyState(VK_SHIFT) & 0x8000);
                ctrlDown  = (GetKeyState(VK_CONTROL) & 0x8000);
                hitS = -1; hitP = -1; sHasSel = 0; allSel = 1;
                
                for (i = shapeCount - 1; i >= 0; i--) {
                    for (j = 0; j < shapes[i].ptCount; j++) {
                        int px = (int)round(shapes[i].ptsX[j] * scaleFactor);
                        int py = (int)round(shapes[i].ptsY[j] * scaleFactor);
                        if (sqrt(pow(px - x, 2) + pow(py - y, 2)) <= 8.0) { hitS = i; hitP = j; break; }
                    }
                    if (hitS != -1) break;
                }

                if (hitS == -1) {
                    for (i = shapeCount - 1; i >= 0; i--) {
                        if (shapes[i].type == 0 && PointInPolyShape(gx, gy, &shapes[i])) { hitS = i; break; }
                    }
                }

                if (hitS == -1) {
                    for (i = shapeCount - 1; i >= 0; i--) {
                        minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                        for (j = 0; j < shapes[i].ptCount; j++) {
                            minX = fmin(minX, shapes[i].ptsX[j]); maxX = fmax(maxX, shapes[i].ptsX[j]);
                            minY = fmin(minY, shapes[i].ptsY[j]); maxY = fmax(maxY, shapes[i].ptsY[j]);
                        }
                        if (gx >= minX && gx <= maxX && gy >= minY && gy <= maxY) { hitS = i; break; }
                    }
                }

                if (hitS == -1 && shiftDown && (currentMode == 0 || currentMode == 1 || currentMode == 2)) {
                    double bestDist = 9999.0;
                    int insertShape = -1, insertPt = -1;
                    double insertX = 0, insertY = 0;
                    for (i = 0; i < shapeCount; i++) {
                        if (shapes[i].ptCount >= MAX_POINTS) continue;
                        for (j = 0; j < (shapes[i].type == 0 ? shapes[i].ptCount : shapes[i].ptCount - 1); j++) {
                            int nj = (j + 1) % shapes[i].ptCount;
                            double prX, prY, d;
                            PtToSegProj(gx, gy, shapes[i].ptsX[j], shapes[i].ptsY[j], shapes[i].ptsX[nj], shapes[i].ptsY[nj], &prX, &prY, &d);
                            if (d < (10.0 / scaleFactor) && d < bestDist) {
                                bestDist = d; insertShape = i; insertPt = j; insertX = prX; insertY = prY;
                            }
                        }
                    }
                    if (insertShape != -1) {
                        int idx;
                        SaveState();
                        idx = insertPt + 1;
                        for (k = shapes[insertShape].ptCount; k > idx; k--) {
                            shapes[insertShape].ptsX[k] = shapes[insertShape].ptsX[k-1];
                            shapes[insertShape].ptsY[k] = shapes[insertShape].ptsY[k-1];
                        }
                        shapes[insertShape].ptsX[idx] = insertX;
                        shapes[insertShape].ptsY[idx] = insertY;
                        shapes[insertShape].ptCount++;
                        ClearSelection();
                        ToggleSelection(insertShape, idx);
                        selectedShape = insertShape;
                        RedrawCanvas(hwnd);
                        return 0;
                    }
                }

                if (hitS != -1) {
                    for(j = 0; j < shapes[hitS].ptCount; j++) if(ptSelected[hitS][j]) sHasSel = 1;
                    
                    if (shiftDown) {
                        if (hitP != -1) ToggleSelection(hitS, hitP);
                        else {
                            for(j = 0; j < shapes[hitS].ptCount; j++) if(!ptSelected[hitS][j]) allSel = 0;
                            for(j = 0; j < shapes[hitS].ptCount; j++) {
                                if (allSel && ptSelected[hitS][j]) ToggleSelection(hitS, j);
                                else if (!allSel && !ptSelected[hitS][j]) ToggleSelection(hitS, j);
                            }
                        }
                    } else {
                        if (hitP != -1) {
                            if (!ptSelected[hitS][hitP]) { ClearSelection(); ToggleSelection(hitS, hitP); }
                        } else if (!sHasSel) {
                            ClearSelection();
                            for(j = 0; j < shapes[hitS].ptCount; j++) ToggleSelection(hitS, j);
                        }
                    }
                    selectedShape = hitS;
                    currentFill = shapes[selectedShape].fill; useFill = shapes[selectedShape].useFill; 
                    currentStroke = shapes[selectedShape].stroke; useStroke = shapes[selectedShape].useStroke;
                } else {
                    ClearSelection(); selectedShape = -1;
                }

                if (selectedShape != -1) {
                    pSelCount = 0;
                    for (i = 0; i < shapeCount; i++) {
                        int cnt = 0;
                        for(j = 0; j < shapes[i].ptCount; j++) if(ptSelected[i][j]) cnt++;
                        if (cnt > 0 && cnt < shapes[i].ptCount) isPartialSelection = 1;
                        pSelCount += cnt;
                    }
                    if (ctrlDown && pSelCount == shapes[selectedShape].ptCount && shapeCount < MAX_SHAPES) {
                        origCount = shapeCount; SaveState();
                        for (i = 0; i < origCount; i++) {
                            sHasSel = 0; for(j = 0; j < shapes[i].ptCount; j++) if(ptSelected[i][j]) sHasSel = 1;
                            if (sHasSel && shapeCount < MAX_SHAPES) { 
                                shapes[shapeCount] = shapes[i]; 
                                for(j = 0; j < shapes[i].ptCount; j++) if(ptSelected[i][j]) { ToggleSelection(i, j); ToggleSelection(shapeCount, j); } 
                                shapeCount++; 
                            }
                        }
                    }
                    memcpy(dragStartSnapshot, shapes, sizeof(Shape) * shapeCount);
                    dragType = (currentMode == 0) ? (shiftDown ? 3 : 1) : (currentMode == 1 ? 3 : 4); 
                    dragStartX = startX; dragStartY = startY; 
                    isDraggingPoint = isPartialSelection;
                    
                    minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                    for(i = 0; i < shapeCount; i++) {
                        for(j = 0; j < shapes[i].ptCount; j++) {
                            if (ptSelected[i][j]) { minX = fmin(minX, shapes[i].ptsX[j]); maxX = fmax(maxX, shapes[i].ptsX[j]); minY = fmin(minY, shapes[i].ptsY[j]); maxY = fmax(maxY, shapes[i].ptsY[j]); }
                        }
                    }
                    shapeCx = (minX + maxX)/2.0; shapeCy = (minY + maxY)/2.0;
                }
                RedrawCanvas(hwnd); return 0;
            }
            else if (currentMode == 6) { 
                currentEndX = gx; currentEndY = gy; isDrawing = 1;
                currentShape.type = 0; 
                currentShape.useFill = useFill; currentShape.fill = currentFill;
                currentShape.useStroke = useStroke; currentShape.stroke = currentStroke;
                GenShape(currentEndX, currentEndY); RedrawCanvas(hwnd);
            }
            else if (currentMode >= 3 && currentMode <= 5) { 
                if (!isDrawing) {
                    isDrawing = 1; 
                    currentShape.type = (currentMode == 4) ? 1 : (currentMode == 5 ? 2 : 0);
                    currentShape.useFill = (currentMode == 3) ? useFill : 0; 
                    currentShape.fill = currentFill;
                    currentShape.useStroke = 1; 
                    currentShape.stroke = currentStroke;
                    currentShape.ptsX[0] = gx; currentShape.ptsY[0] = gy;
                    currentShape.ptsX[1] = gx; currentShape.ptsY[1] = gy; 
                    currentShape.ptCount = 2;
                } else if (currentShape.ptCount < MAX_POINTS) {
                    currentShape.ptsX[currentShape.ptCount-1] = gx; currentShape.ptsY[currentShape.ptCount-1] = gy;
                    currentShape.ptCount++; 
                    currentShape.ptsX[currentShape.ptCount-1] = gx; currentShape.ptsY[currentShape.ptCount-1] = gy;
                }
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            double nx, ny, dx, dy, bestDist, prX, prY, d, newX, newY, minX, maxX, minY, maxY;
            int x, y, i, j, p, np, ctrlDown;
            double diff, ox, oy, d1, d2, scale;
            
            x = (int)(short)LOWORD(lParam); 
            y = (int)(short)HIWORD(lParam);
            
            nx = CLAMP(Snap(x / (double)scaleFactor), 0, GRID_SIZE);
            ny = CLAMP(Snap(y / (double)scaleFactor), 0, GRID_SIZE);
            
            if (dragDimIdx != -1) {
                Dimension* dptr = &dims[dragDimIdx];
                if (dptr->s1 < shapeCount && dptr->s2 < shapeCount && 
                    dptr->p1 < shapes[dptr->s1].ptCount && dptr->p2 < shapes[dptr->s2].ptCount) {
                    
                    double A1x = shapes[dptr->s1].ptsX[dptr->p1] * scaleFactor;
                    double A1y = shapes[dptr->s1].ptsY[dptr->p1] * scaleFactor;
                    double A2x = shapes[dptr->s2].ptsX[dptr->p2] * scaleFactor;
                    double A2y = shapes[dptr->s2].ptsY[dptr->p2] * scaleFactor;
                    
                    if (dptr->mode == 0) {
                        double dimDx = A2x - A1x, dimDy = A2y - A1y;
                        double ang = atan2(dimDy, dimDx);
                        double nX = -sin(ang), nY = cos(ang);
                        dptr->offset = ((x - A1x) * nX + (y - A1y) * nY);
                        double len2 = dimDx*dimDx + dimDy*dimDy;
                        if (len2 > 0) dptr->textPos = ((x - A1x) * dimDx + (y - A1y) * dimDy) / len2;
                    } else if (dptr->mode == 1) {
                        dptr->offset = y - A1y;
                        double dimDx = A2x - A1x;
                        if (dimDx != 0) dptr->textPos = (x - A1x) / dimDx;
                    } else if (dptr->mode == 2) {
                        dptr->offset = x - A1x;
                        double dimDy = A2y - A1y;
                        if (dimDy != 0) dptr->textPos = (y - A1y) / dimDy;
                    }
                    if (dptr->textPos < 0.0) dptr->textPos = 0.0;
                    if (dptr->textPos > 1.0) dptr->textPos = 1.0;
                    RedrawCanvas(hwnd);
                }
                return 0;
            }
            
            if (dragType > 0) {
                ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000);
                dx = nx - dragStartX; dy = ny - dragStartY;
                
                if (dragType == 1) {
                    minX = GRID_SIZE; maxX = 0; minY = GRID_SIZE; maxY = 0;
                    for(i = 0; i < shapeCount; i++) {
                        for(j = 0; j < shapes[i].ptCount; j++) {
                            if (ptSelected[i][j]) {
                                minX = fmin(minX, dragStartSnapshot[i].ptsX[j]);
                                maxX = fmax(maxX, dragStartSnapshot[i].ptsX[j]);
                                minY = fmin(minY, dragStartSnapshot[i].ptsY[j]);
                                maxY = fmax(maxY, dragStartSnapshot[i].ptsY[j]);
                            }
                        }
                    }
                    if (minX + dx < 0) dx = -minX; 
                    if (maxX + dx > GRID_SIZE) dx = GRID_SIZE - maxX; 
                    if (minY + dy < 0) dy = -minY; 
                    if (maxY + dy > GRID_SIZE) dy = GRID_SIZE - maxY;

                    if (ctrlDown && isDraggingPoint) { if (fabs(dx) > fabs(dy)) dy = 0; else dx = 0; }
                    
                    if (dx != 0 || dy != 0) {
                        for(i = 0; i < shapeCount; i++) {
                            for(j = 0; j < shapes[i].ptCount; j++) {
                                if (ptSelected[i][j]) {
                                    newX = dragStartSnapshot[i].ptsX[j] + dx; newY = dragStartSnapshot[i].ptsY[j] + dy;
                                    shapes[i].ptsX[j] = snapToGrid ? round(newX) : newX; shapes[i].ptsY[j] = snapToGrid ? round(newY) : newY;
                                }
                            }
                        }
                    }
                } else if (dragType == 3) {
                    diff = atan2(ny - shapeCy, nx - shapeCx) - atan2(dragStartY - shapeCy, dragStartX - shapeCx);
                    if (snapToGrid) diff = round(diff / (PI/12.0)) * (PI/12.0);
                    for(i = 0; i < shapeCount; i++) {
                        for(j = 0; j < shapes[i].ptCount; j++) {
                            if (ptSelected[i][j]) {
                                ox = dragStartSnapshot[i].ptsX[j] - shapeCx; 
                                oy = dragStartSnapshot[i].ptsY[j] - shapeCy;
                                shapes[i].ptsX[j] = shapeCx + (ox * cos(diff) - oy * sin(diff)); 
                                shapes[i].ptsY[j] = shapeCy + (ox * sin(diff) + oy * cos(diff));
                            }
                        }
                    }
                } else if (dragType == 4) {
                    d1 = sqrt(pow(dragStartX - shapeCx, 2) + pow(dragStartY - shapeCy, 2));
                    d2 = sqrt(pow(nx - shapeCx, 2) + pow(ny - shapeCy, 2));
                    if (d1 > 0.01) {
                        scale = d2 / d1;
                        for(i = 0; i < shapeCount; i++) {
                            for(j = 0; j < shapes[i].ptCount; j++) {
                                if (ptSelected[i][j]) {
                                    newX = shapeCx + ((dragStartSnapshot[i].ptsX[j] - shapeCx) * scale);
                                    newY = shapeCy + ((dragStartSnapshot[i].ptsY[j] - shapeCy) * scale);
                                    shapes[i].ptsX[j] = snapToGrid ? round(newX) : newX; shapes[i].ptsY[j] = snapToGrid ? round(newY) : newY;
                                }
                            }
                        }
                    }
                }
                RedrawCanvas(hwnd);
            } else if (currentMode == 0 || currentMode == 1 || currentMode == 2) {
                hoverShape = -1; hoverPt = -1; hoverSegShape = -1; hoverSegPt = -1; bestDist = 9999.0;
                
                if (selectedShape != -1 && selectedShape < shapeCount) {
                    for (p = 0; p < shapes[selectedShape].ptCount; p++) {
                        d = sqrt(pow(shapes[selectedShape].ptsX[p]*scaleFactor - x, 2) + pow(shapes[selectedShape].ptsY[p]*scaleFactor - y, 2));
                        if (d < 15.0 && d < bestDist) { hoverShape = selectedShape; hoverPt = p; bestDist = d; }
                    }
                }
                if (hoverPt == -1) {
                    for (i = 0; i < shapeCount; i++) {
                        for (p = 0; p < shapes[i].ptCount; p++) {
                            d = sqrt(pow(shapes[i].ptsX[p]*scaleFactor - x, 2) + pow(shapes[i].ptsY[p]*scaleFactor - y, 2));
                            if (d < 15.0 && d < bestDist) { hoverShape = i; hoverPt = p; bestDist = d; }
                        }
                    }
                }
                if (hoverPt == -1) {
                    bestDist = 9999.0;
                    for (i = 0; i < shapeCount; i++) {
                        if (shapes[i].ptCount >= MAX_POINTS) continue;
                        for (p = 0; p < (shapes[i].type == 0 ? shapes[i].ptCount : shapes[i].ptCount - 1); p++) {
                            np = (p + 1) % shapes[i].ptCount;
                            PtToSegProj((double)x, (double)y, shapes[i].ptsX[p]*scaleFactor, shapes[i].ptsY[p]*scaleFactor, shapes[i].ptsX[np]*scaleFactor, shapes[i].ptsY[np]*scaleFactor, &prX, &prY, &d);
                            if (d < 10.0 && d < bestDist) { hoverSegShape = i; hoverSegPt = p; hoverProjX = prX; hoverProjY = prY; bestDist = d; }
                        }
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (isDrawing) {
                if (currentMode == 6) { 
                    currentEndX = nx; currentEndY = ny; GenShape(currentEndX, currentEndY); 
                } 
                else if (currentMode >= 3 && currentMode <= 5) { 
                    currentShape.ptsX[currentShape.ptCount-1] = nx; 
                    currentShape.ptsY[currentShape.ptCount-1] = ny; 
                }
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (dragDimIdx != -1) dragDimIdx = -1;
            if (dragType > 0) { dragType = 0; SaveState(); }
            else if (isDrawing && currentMode == 6) {
                isDrawing = 0;
                if (currentShape.ptCount >= 2 && shapeCount < MAX_SHAPES) { 
                    SaveState(); shapes[shapeCount++] = currentShape; selectedShape = shapeCount-1; currentShape.ptCount = 0; currentMode = 0; UpdateStatusBar(); 
                }
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_RBUTTONDOWN: {
            int x = (int)(short)LOWORD(lParam); 
            int y = (int)(short)HIWORD(lParam);
            int i, p, k;
            if (currentMode == 0 || currentMode == 1 || currentMode == 2) {
                for (i = 0; i < shapeCount; i++) {
                    for (p = 0; p < shapes[i].ptCount; p++) {
                        int px = (int)round(shapes[i].ptsX[p] * scaleFactor);
                        int py = (int)round(shapes[i].ptsY[p] * scaleFactor);
                        if (sqrt(pow(px - x, 2) + pow(py - y, 2)) <= 8.0) {
                            if (shapes[i].ptCount > (shapes[i].type == 0 ? 3 : 2)) {
                                SaveState();
                                for (k = p; k < shapes[i].ptCount - 1; k++) {
                                    shapes[i].ptsX[k] = shapes[i].ptsX[k+1];
                                    shapes[i].ptsY[k] = shapes[i].ptsY[k+1];
                                }
                                shapes[i].ptCount--;
                                ClearSelection();
                                RedrawCanvas(hwnd);
                            }
                            return 0;
                        }
                    }
                }
            } else if (isDrawing && (currentMode >= 3 && currentMode <= 5)) { 
                if (currentShape.ptCount >= (currentMode == 3 ? 3 : 2) && shapeCount < MAX_SHAPES) {
                    SaveState(); shapes[shapeCount++] = currentShape; selectedShape = shapeCount - 1; currentMode = 0;
                }
                currentShape.ptCount = 0; isDrawing = 0; RedrawCanvas(hwnd); 
            } else { 
                currentShape.ptCount = 0; isDrawing = 0; if (shapeCount > 0) currentMode = 0; else currentMode = 3; RedrawCanvas(hwnd); 
            }
            break;
        }
        case WM_APP + 1: { 
            char buf[32]; double newDist, dx, dy, minX, maxX, minY, maxY, cx, cy, oldVal, scale; 
            int p, s1, p1, s2, p2;
            GetWindowText(hDistEdit, buf, 32); newDist = atof(buf);
            
            if (distEditMode == 0 && selOrderCount == 2) {
                s1 = selOrderS[0]; p1 = selOrderP[0]; s2 = selOrderS[1]; p2 = selOrderP[1];
                dx = shapes[s2].ptsX[p2] - shapes[s1].ptsX[p1]; dy = shapes[s2].ptsY[p2] - shapes[s1].ptsY[p1];
                SaveState(); shapes[s2].ptsX[p2] = shapes[s1].ptsX[p1] + dx * (newDist / sqrt(dx*dx + dy*dy)); 
                shapes[s2].ptsY[p2] = shapes[s1].ptsY[p1] + dy * (newDist / sqrt(dx*dx + dy*dy));
            } else if ((distEditMode == 1 || distEditMode == 2) && selectedShape != -1) {
                minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                for(p=0; p<shapes[selectedShape].ptCount; p++) { minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]); minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]); }
                cx = (minX + maxX)/2.0; cy = (minY + maxY)/2.0; oldVal = (distEditMode == 1) ? (maxX - minX) : (maxY - minY);
                if (oldVal > 0) {
                    scale = newDist / oldVal; SaveState();
                    for(p=0; p<shapes[selectedShape].ptCount; p++) {
                        if (distEditMode == 1) shapes[selectedShape].ptsX[p] = cx + (shapes[selectedShape].ptsX[p] - cx) * scale;
                        else shapes[selectedShape].ptsY[p] = cy + (shapes[selectedShape].ptsY[p] - cy) * scale;
                    }
                }
            } else if (distEditMode == 3) {
                double cScale = atof(buf);
                if (cScale <= 0.0) cScale = 1.0;
                LoadSVG(pendingSvgFile, hwnd, cScale);
            } else if (distEditMode == 4 && selOrderCount == 2) {
                s1 = selOrderS[0]; p1 = selOrderP[0]; s2 = selOrderS[1]; p2 = selOrderP[1];
                dx = shapes[s2].ptsX[p2] - shapes[s1].ptsX[p1]; dy = shapes[s2].ptsY[p2] - shapes[s1].ptsY[p1];
                double cDist = sqrt(dx*dx + dy*dy);
                double nAng = atof(buf);
                SaveState(); 
                shapes[s2].ptsX[p2] = shapes[s1].ptsX[p1] + cos(nAng * PI / 180.0) * cDist;
                shapes[s2].ptsY[p2] = shapes[s1].ptsY[p1] + sin(nAng * PI / 180.0) * cDist;
            }
            ShowWindow(hDistEdit, SW_HIDE); distEditMode = 0; RedrawCanvas(hwnd); SetFocus(hwnd); 
            break;
        }
        case WM_COMMAND: {
            UINT id = wParam;
            int btnId = id - 200;
            int i, j, k;

            if (id == 151) { 
                if (parsedCount < MAX_ICONS) {
                    int newId = 1;
                    for(i=0; i<parsedCount; i++) if(parsedIcons[i].caseId >= newId) newId = parsedIcons[i].caseId + 1;
                    parsedIcons[parsedCount].caseId = newId;
                    strcpy(parsedIcons[parsedCount].name, "New");
                    parsedIcons[parsedCount].shapes = NULL;
                    parsedIcons[parsedCount].shapeCount = 0;
                    parsedCount++;
                    SetScrollRange(hScrlIcon, SB_CTL, 0, parsedCount - 1, TRUE);
                    SwitchToIcon(parsedCount - 1);
                }
                SetFocus(hwnd); break;
            }
            else if (id == 152) { 
                if (parsedCount > 0 && currentIconIdx >= 0) {
                    if (parsedIcons[currentIconIdx].shapes) {
                        GlobalFreePtr(parsedIcons[currentIconIdx].shapes);
                        parsedIcons[currentIconIdx].shapes = NULL;
                    }
                    for(i=currentIconIdx; i<parsedCount-1; i++) parsedIcons[i] = parsedIcons[i+1];
                    parsedCount--;
                    if (parsedCount > 0) {
                        parsedIcons[parsedCount].shapes = NULL; 
                        parsedIcons[parsedCount].shapeCount = 0;
                    }
                    
                    if (parsedCount == 0) { 
                        parsedCount = 1; currentIconIdx = -1; currentCaseId = 1;
                        parsedIcons[0].caseId = 1;
                        strcpy(parsedIcons[0].name, "New");
                        parsedIcons[0].shapes = NULL; parsedIcons[0].shapeCount = 0;
                        shapeCount = 0; 
                        SetScrollRange(hScrlIcon, SB_CTL, 0, 0, TRUE); 
                        SwitchToIcon(0);
                        currentMode = 3;
                        UpdateStatusBar(); RedrawCanvas(hwnd); 
                    } else { 
                        int nIdx = currentIconIdx >= parsedCount ? parsedCount - 1 : currentIconIdx; 
                        currentIconIdx = -1; 
                        SetScrollRange(hScrlIcon, SB_CTL, 0, parsedCount - 1, TRUE);
                        SwitchToIcon(nIdx); 
                    }
                }
                SetFocus(hwnd); break;
            }

            if (btnId >= 0 && btnId < 28) {
                if (btnId >= 0 && btnId <= 6) { currentMode = btnId; isDrawing = 0; dragType = 0; ClearSelection(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, TRUE); }
                else if (btnId == 7) { currentMode = 7; textCursorActive = 0; }
                else if (btnId == 8) { currentMode = 8; isDrawing = 0; dragType = 0; ClearSelection(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, TRUE); }
                else if (btnId == 9) Undo(hwnd);
                else if (btnId == 10) { 
                    SaveState(); shapeCount = 0; selectedShape = -1; ClearSelection(); currentMode = 3; UpdateStatusBar(); RedrawCanvas(hwnd);
                }
                else if (btnId == 11) { SendMessage(hwnd, WM_KEYDOWN, VK_DELETE, 0); }
                else if (btnId == 12) { 
                    static OPENFILENAME ofn; static char szFile[260];
                    memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; 
                    ofn.lpstrFilter = "SVG Files (*.svg)\0*.svg\0"; 
                    ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) { 
                        int res = MessageBox(hwnd, "Scale SVG to fit canvas?\n(Select NO to enter custom scale)", "SVG Import", MB_YESNOCANCEL | MB_ICONQUESTION);
                        if (res == IDYES) {
                            LoadSVG(szFile, hwnd, -1.0);
                        } else if (res == IDNO) {
                            strcpy(pendingSvgFile, szFile);
                            distEditMode = 3;
                            SetWindowText(hDistEdit, "1.0");
                            MoveWindow(hDistEdit, canvasSize/2, canvasSize/2, 60, 20, TRUE);
                            ShowWindow(hDistEdit, SW_SHOW); SetFocus(hDistEdit);
                        }
                    }
                }
                else if (btnId == 14) { 
                    static OPENFILENAME ofn; static char szFile[260];
                    memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFilter = "C Data Files (*.c)\0*.c\0"; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) LoadCFile(szFile, hwnd); 
                }
                else if (btnId == 15) { DoSaveFile(hwnd); }
                else if (btnId == 16) { 
                    if (shapeCount > 0 && OpenClipboard(hwnd)) {
                        HGLOBAL hGlb; char* pBuf; int bSz = 4096; char tmp[128]; int pIdx;
                        EmptyClipboard();
                        hGlb = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bSz);
                        if (hGlb) {
                            pBuf = (char*)GlobalLock(hGlb); pBuf[0] = '\0';
                            for (i = 0; i < shapeCount; i++) {
                                Shape* ps = &shapes[i];
                                if (ps->type == 0 && ps->ptCount > 2) {
                                    sprintf(tmp, "POINT p%d[] = { ", i); strcat(pBuf, tmp);
                                    for (pIdx = 0; pIdx < ps->ptCount; pIdx++) {
                                        sprintf(tmp, "PT(%g,%g)%s", ps->ptsX[pIdx], ps->ptsY[pIdx], pIdx == ps->ptCount-1 ? "" : ", ");
                                        strcat(pBuf, tmp);
                                    }
                                    sprintf(tmp, " }; POLY(p%d);\r\n", i); strcat(pBuf, tmp);
                                }
                            }
                            GlobalUnlock(hGlb); SetClipboardData(CF_TEXT, hGlb);
                        }
                        CloseClipboard(); ShowStatus(" Code copied to clipboard.");
                    }
                }
                else if (btnId == 17) { 
                    int distinctShapes[2]; int distinctCount = 0;
                    int s1, s2, edgeCount, fCount, loopsFound, loopStartIdx, firstEdge, found, cleanCnt;
                    double cx, cy;
                    double *clnX, *clnY; Edge *pool, *filtered; int *keep, *used;
                    static Shape merged;

                    for (i = 0; i < selOrderCount; i++) {
                        int sId = selOrderS[i]; found = 0;
                        for (j = 0; j < distinctCount; j++) { if (distinctShapes[j] == sId) { found = 1; break; } }
                        if (!found) { if (distinctCount < 2) distinctShapes[distinctCount++] = sId; else { distinctCount++; break; } }
                    }

                    if (distinctCount == 2 && shapes[distinctShapes[0]].type == 0 && shapes[distinctShapes[1]].type == 0) {
                        s1 = distinctShapes[0]; s2 = distinctShapes[1]; edgeCount = 0; fCount = 0; loopsFound = 0;
                        clnX = (double*)GlobalAllocPtr(GHND, MAX_POINTS * sizeof(double));
                        clnY = (double*)GlobalAllocPtr(GHND, MAX_POINTS * sizeof(double));
                        pool = (Edge*)GlobalAllocPtr(GHND, 1024 * sizeof(Edge));
                        filtered = (Edge*)GlobalAllocPtr(GHND, 1024 * sizeof(Edge));
                        keep = (int*)GlobalAllocPtr(GHND, 1024 * sizeof(int));
                        used = (int*)GlobalAllocPtr(GHND, 1024 * sizeof(int));
                        
                        if (pool && filtered && keep && used && clnX && clnY) {
                            SaveState(); 
                            AddEdgesFromShape(&shapes[s1], &shapes[s2], pool, &edgeCount); 
                            AddEdgesFromShape(&shapes[s2], &shapes[s1], pool, &edgeCount);
                            
                            for(i=0; i<edgeCount; i++) keep[i] = 1;
                            for(i=0; i<edgeCount; i++) {
                                if (!keep[i]) continue;
                                for(j=i+1; j<edgeCount; j++) {
                                    if (!keep[j]) continue;
                                    if ((fabs(pool[i].x1 - pool[j].x1) < 1e-4 && fabs(pool[i].y1 - pool[j].y1) < 1e-4 && fabs(pool[i].x2 - pool[j].x2) < 1e-4 && fabs(pool[i].y2 - pool[j].y2) < 1e-4) ||
                                        (fabs(pool[i].x1 - pool[j].x2) < 1e-4 && fabs(pool[i].y1 - pool[j].y2) < 1e-4 && fabs(pool[i].x2 - pool[j].x1) < 1e-4 && fabs(pool[i].y2 - pool[j].y1) < 1e-4)) {
                                        keep[i] = 0; keep[j] = 0; break;
                                    }
                                }
                            }
                            
                            for(i=0; i<edgeCount; i++) if (keep[i]) filtered[fCount++] = pool[i];
                            
                            if (fCount > 0) {
                                merged = shapes[s1]; merged.ptCount = 0; 
                                memset(used, 0, 1024 * sizeof(int));
                                
                                while(1) {
                                    firstEdge = -1;
                                    for(j=0; j<fCount; j++) if(!used[j]) { firstEdge = j; break; }
                                    if (firstEdge == -1) break;
                                    
                                    cx = filtered[firstEdge].x1; cy = filtered[firstEdge].y1;
                                    loopStartIdx = merged.ptCount;
                                    
                                    while(1) {
                                        found = -1;
                                        if (merged.ptCount >= MAX_POINTS - 2) break;
                                        merged.ptsX[merged.ptCount] = cx; merged.ptsY[merged.ptCount] = cy; merged.ptCount++;
                                        
                                        for (j=0; j<fCount; j++) {
                                            if (!used[j]) {
                                                if (fabs(filtered[j].x1 - cx) < 1e-4 && fabs(filtered[j].y1 - cy) < 1e-4) { found = j; cx = filtered[j].x2; cy = filtered[j].y2; break; }
                                                if (fabs(filtered[j].x2 - cx) < 1e-4 && fabs(filtered[j].y2 - cy) < 1e-4) { found = j; cx = filtered[j].x1; cy = filtered[j].y1; break; }
                                            }
                                        }
                                        if (found != -1) used[found] = 1; else break;
                                    }
                                    
                                    if (loopsFound > 0 && loopStartIdx > 0 && merged.ptCount < MAX_POINTS - 2) {
                                        merged.ptsX[merged.ptCount] = merged.ptsX[loopStartIdx]; merged.ptsY[merged.ptCount] = merged.ptsY[loopStartIdx]; merged.ptCount++;
                                        merged.ptsX[merged.ptCount] = merged.ptsX[loopStartIdx - 1]; merged.ptsY[merged.ptCount] = merged.ptsY[loopStartIdx - 1]; merged.ptCount++;
                                    }
                                    loopsFound++;
                                }
                                
                                cleanCnt = 0; 
                                for (i=0; i<merged.ptCount; i++) { if (cleanCnt == 0 || pow(merged.ptsX[i]-clnX[cleanCnt-1], 2) + pow(merged.ptsY[i]-clnY[cleanCnt-1], 2) > 1e-4) { clnX[cleanCnt] = merged.ptsX[i]; clnY[cleanCnt++] = merged.ptsY[i]; } }
                                if (cleanCnt > 1 && pow(clnX[0]-clnX[cleanCnt-1], 2) + pow(clnY[0]-clnY[cleanCnt-1], 2) < 1e-4) cleanCnt--;
merged.ptCount = cleanCnt; for(i=0; i<cleanCnt; i++) { merged.ptsX[i]=clnX[i]; merged.ptsY[i]=clnY[i]; }
                                
                                {
                                    int keepIdx = (s1 < s2) ? s1 : s2;
                                    int delIdx = (s1 > s2) ? s1 : s2;
                                    
                                    shapes[keepIdx] = merged;
                                    
                                    for(k = delIdx; k < shapeCount - 1; k++) {
                                        shapes[k] = shapes[k + 1];
                                    }
                                    shapeCount--;
                                    
                                    ClearSelection(); 
                                    selectedShape = keepIdx; 
                                    for(i = 0; i < shapes[keepIdx].ptCount; i++) {
                                        ToggleSelection(keepIdx, i);
                                    }
                                }
                            }
                        }
                        if (pool) GlobalFreePtr(pool); if (filtered) GlobalFreePtr(filtered); if (keep) GlobalFreePtr(keep); if (used) GlobalFreePtr(used);
                        if (clnX) GlobalFreePtr(clnX); if (clnY) GlobalFreePtr(clnY);
                        UpdateStatusBar(); RedrawCanvas(hwnd);
                    }
                }
                else if (btnId == 18) { 
                    if (selectedShape != -1 && selectedShape < shapeCount - 1) { static Shape temp; SaveState(); temp = shapes[selectedShape]; shapes[selectedShape] = shapes[selectedShape + 1]; shapes[selectedShape + 1] = temp; selectedShape++; RedrawCanvas(hwnd); } 
                }
                else if (btnId == 19) { 
                    if (selectedShape > 0) { static Shape temp; SaveState(); temp = shapes[selectedShape]; shapes[selectedShape] = shapes[selectedShape - 1]; shapes[selectedShape - 1] = temp; selectedShape--; RedrawCanvas(hwnd); } 
                }
                else if (btnId == 20) { 
                    if (selOrderCount == 2) { SaveState(); shapes[selOrderS[1]].ptsX[selOrderP[1]] = shapes[selOrderS[0]].ptsX[selOrderP[0]]; RedrawCanvas(hwnd); } 
                }
                else if (btnId == 21) { 
                    if (selOrderCount == 2) { SaveState(); shapes[selOrderS[1]].ptsY[selOrderP[1]] = shapes[selOrderS[0]].ptsY[selOrderP[0]]; RedrawCanvas(hwnd); } 
                }
                else if (btnId == 22 || btnId == 23 || btnId == 24) { 
                    if (btnId == 22 && selOrderCount == 2) {
                        int s1_l = selOrderS[0], p1_l = selOrderP[0], s2_l = selOrderS[1], p2_l = selOrderP[1];
                        double dx = shapes[s2_l].ptsX[p2_l] - shapes[s1_l].ptsX[p1_l], dy = shapes[s2_l].ptsY[p2_l] - shapes[s1_l].ptsY[p1_l];
                        int px = (int)round(shapes[s2_l].ptsX[p2_l] * scaleFactor) + 10, py = (int)round(shapes[s2_l].ptsY[p2_l] * scaleFactor) + 10;
                        char buf[32]; distEditMode = 0; 
                        sprintf(buf, "%.2f", sqrt(dx*dx + dy*dy));
                        SetWindowText(hDistEdit, buf); MoveWindow(hDistEdit, px, py, 60, 20, TRUE); ShowWindow(hDistEdit, SW_SHOW); SetFocus(hDistEdit);
                    } else if (selectedShape != -1 && shapes[selectedShape].ptCount > 0) {
                        double minX = 9999, maxX = -9999, minY = 9999, maxY = -9999, val; int p; char buf[32];
                        for(p=0; p<shapes[selectedShape].ptCount; p++) { minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]); minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]); }
                        val = (btnId == 23) ? (maxX - minX) : (maxY - minY); distEditMode = (btnId == 23) ? 1 : 2;
                        sprintf(buf, "%.2f", val); SetWindowText(hDistEdit, buf); 
                        MoveWindow(hDistEdit, (int)((minX+maxX)/2.0 * scaleFactor), (int)((minY+maxY)/2.0 * scaleFactor), 60, 20, TRUE); 
                        ShowWindow(hDistEdit, SW_SHOW); SetFocus(hDistEdit);
                    }
                }
                else if (btnId == 25) { 
                    if (selectedShape != -1 && shapeCount < MAX_SHAPES) { SaveState(); shapes[shapeCount] = shapes[selectedShape]; selectedShape = shapeCount++; RedrawCanvas(hwnd); }
                }
                else if (btnId == 26) { 
                    if (selOrderCount == 2) {
                        int s1_l = selOrderS[0], p1_l = selOrderP[0], s2_l = selOrderS[1], p2_l = selOrderP[1];
                        double dx = shapes[s2_l].ptsX[p2_l] - shapes[s1_l].ptsX[p1_l];
                        double dy = shapes[s2_l].ptsY[p2_l] - shapes[s1_l].ptsY[p1_l];
                        double ang = atan2(dy, dx) * 180.0 / PI;
                        int px = (int)round(shapes[s2_l].ptsX[p2_l] * scaleFactor) + 10;
                        int py = (int)round(shapes[s2_l].ptsY[p2_l] * scaleFactor) + 10;
                        char buf[32]; distEditMode = 4;
                        if (ang < 0) ang += 360.0;
                        sprintf(buf, "%.2f", ang);
                        SetWindowText(hDistEdit, buf); MoveWindow(hDistEdit, px, py, 60, 20, TRUE); ShowWindow(hDistEdit, SW_SHOW); SetFocus(hDistEdit);
                    } else {
                        ShowStatus(" Select exactly 2 nodes to set angle.");
                    }
                }
                else if (btnId == 27) { 
                    if (selOrderCount == 2) {
                        if (dimCount < MAX_DIMS) {
                            dims[dimCount].s1 = selOrderS[0];
                            dims[dimCount].p1 = selOrderP[0];
                            dims[dimCount].s2 = selOrderS[1];
                            dims[dimCount].p2 = selOrderP[1];
                            dims[dimCount].offset = 20.0;
                            dims[dimCount].textPos = 0.5;
                            dims[dimCount].mode = 0;
                            dimCount++;
                        }
                        ClearSelection(); RedrawCanvas(hwnd);
                    } else {
                        ShowStatus(" Dimension tool needs exactly 2 nodes selected.");
                    }
                }
            }
            if (id != 300 && (id < 222 || id > 224) && id != 226) SetFocus(hwnd); break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc; HPEN pCur; HGDIOBJ old;
            hdc = BeginPaint(hwnd, &ps);
            if (shapes) {
                DrawGrid(hdc); RenderShapes(hdc, shapes, shapeCount, scaleFactor, 0, 0, &currentShape, isDrawing);
                
                if (currentMode == 0 || currentMode == 1 || currentMode == 2) DrawNodes(hdc);
                if (currentMode == 7 && textCursorActive) {
                    pCur = CreatePen(PS_SOLID, 2, RGB(255,165,0)); old = SelectObject(hdc, pCur);
                    MoveTo(hdc, (int)(textCursorX*scaleFactor), (int)(textCursorY*scaleFactor)); 
                    LineTo(hdc, (int)(textCursorX*scaleFactor), (int)((textCursorY+5)*scaleFactor));
                    SelectObject(hdc, old); DeleteObject(pCur);
                }
                
                DrawDimensions(hdc);
                DrawPalette(hdc); DrawPreview(hdc); 
            }
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_DESTROY: {
            int i;
            if (hDistEdit && oldEditProc) {
                SetWindowLong(hDistEdit, GWL_WNDPROC, (LONG)oldEditProc);
            }
            if (shapes) GlobalFreePtr(shapes);
            if (dragStartSnapshot) GlobalFreePtr(dragStartSnapshot);
            for(i=0; i<MAX_UNDO; i++) if (history[i]) GlobalFreePtr(history[i]);
            for(i=0; i<parsedCount; i++) if (parsedIcons[i].shapes) GlobalFreePtr(parsedIcons[i].shapes);
            
            if (subclassThunk) FreeProcInstance(subclassThunk);
            
            PostQuitMessage(0); break;
        }
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0L;
}
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow) {
    MSG msg; WNDCLASS wc; hInst = hInstance;
    if (!hPrevInstance) {
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WndProc; wc.cbClsExtra = 0; wc.cbWndExtra = 0;
        wc.hInstance = hInstance; wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = NULL; wc.lpszClassName = "IconEditClass";
        if (!RegisterClass(&wc)) return FALSE;
    }
    hMain = CreateWindow("IconEditClass", "Win16 C Pro Vector Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 680, 560, NULL, NULL, hInstance, NULL);
    if (!hMain) return FALSE; ShowWindow(hMain, nCmdShow); UpdateWindow(hMain);
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return msg.wParam;
}