/* ============================================================================
 * PUBLIC DOMAIN NOTICE
 * Free and unencumbered software released into the public domain.
 * ============================================================================
 *
 * OPENWATCOM WIN16 C PORT - FULL FEATURED VECTOR EDITOR (Windows 3.1x / 16-bit)
 *
 * COMPILATION INSTRUCTIONS:
 *   wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s -fe=icoedtv.exe icoedtv.c commdlg.lib
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

/* Win16 Limits */
#define GRID_SIZE 32
#define MAX_POINTS 64
#define MAX_SHAPES 40
#define MAX_UNDO 10
#define MAX_ICONS 50
#define PANEL_WIDTH 340
#define MAX_TOTAL_POINTS (MAX_SHAPES * MAX_POINTS)

/* C89 Math & Color Macros */
#define fmax(a,b) (((a)>(b))?(a):(b))
#define fmin(a,b) (((a)<(b))?(a):(b))
#define round(x) ((double)((long)((x) + ((x)>=0 ? 0.5 : -0.5))))
#define GET_R(c) ((int)((c) & 0xFF))
#define GET_G(c) ((int)(((c) >> 8) & 0xFF))
#define GET_B(c) ((int)(((c) >> 16) & 0xFF))

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

/* Heap-Allocated Globals */
Shape* shapes = NULL; 
Shape* dragStartSnapshot = NULL;
Shape** history = NULL; 

int shapeCount = 0;
int historyShapeCount[MAX_UNDO]; 
int undoIndex = -1;

IconDef parsedIcons[MAX_ICONS];
int parsedCount = 0, currentIconIdx = -1, currentCaseId = 1;
char loadedCFile[260] = "";

Shape currentShape;
int isDrawing = 0, currentMode = 0; 
int selectedShape = -1;

/* Hover & Drag State */
int hoverShape = -1, hoverPt = -1, hoverSegShape = -1, hoverSegPt = -1;
double hoverProjX = 0, hoverProjY = 0;
int isDraggingNodes = 0;
double shapeCx = 0, shapeCy = 0;
double dragStartX = 0, dragStartY = 0;
int isDraggingPoint = 0;
int isPartialSelection = 0;

int ptSelected[MAX_SHAPES][MAX_POINTS];
int selOrderS[MAX_TOTAL_POINTS], selOrderP[MAX_TOTAL_POINTS], selOrderCount = 0;

int startX = 0, startY = 0;
double currentEndX = 0, currentEndY = 0;
int snapToGrid = 1;

double textCursorX = 0, textCursorY = 0; 
int textCursorActive = 0;
long font5x3[128] = {0};

/* Palette & Colors */
COLORREF palette[16] = {
    RGB(0,0,0), RGB(255,255,255), RGB(128,128,128), RGB(192,192,192),
    RGB(255,0,0), RGB(128,0,0), RGB(255,255,0), RGB(128,128,0),
    RGB(0,255,0), RGB(0,128,0), RGB(0,255,255), RGB(0,128,128),
    RGB(0,0,255), RGB(0,0,128), RGB(255,0,255), RGB(128,0,128)
};
COLORREF currentFill = RGB(128, 128, 128); 
int useFill = 1;
COLORREF currentStroke = RGB(0, 0, 0); 
int useStroke = 1;

/* Window Handles & Instance */
HINSTANCE hInst = NULL;
HWND hMain, hBtn[28], hStatus;
HWND hScrlSides, hScrlDepth, hScrlIcon;
HWND hDistEdit = NULL; 
FARPROC oldEditProc = NULL;
FARPROC subclassThunk = NULL;
int distEditMode = 0; 

/* Parameters */
int paramSides = 4, paramStar = 100;
int canvasSize = 320, scaleFactor = 10, clientW = 0, clientH = 0;

/* Reference Image State */
HBITMAP hRefBmp = NULL;
int refAlpha = 128;
double absRefScale = 1.0, absRefPosX = 0.0, absRefPosY = 0.0;
double absRefStrX = 1.0, absRefStrY = 1.0;
HWND hTrkAlpha = NULL, hTrkScale = NULL, hTrkPosX = NULL;
HWND hTrkPosY = NULL, hTrkStrX = NULL, hTrkStrY = NULL;

const char* const bT[28] = {
    "Select/Edit", "Rotate", "Scale", "Polygon", "Line",
    "Polyline", "Shapes", "Text", "Flood Fill", "Undo",
    "Clear", "Delete", "Import SVG", "Open Ref", "Open .C",
    "Save .C", "Export Code", "Merge", "Move Up", "Move Down",
    "Align Vert", "Align Horz", "Set Dist", "Set Width", "Set Height", "Duplicate",
    "Add Icon", "Del Icon"
};

/* --- Utilities & Initialization --- */
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
    if (!shapes || !history || !history[0]) return;
    
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
    if (shapeCount > 0) memcpy(history[undoIndex], shapes, sizeof(Shape) * shapeCount); 
    UpdateStatusBar(); 
}

void Undo(HWND hwnd) { 
    if (undoIndex >= 0) { 
        shapeCount = historyShapeCount[undoIndex]; 
        if (shapeCount > 0) memcpy(shapes, history[undoIndex], sizeof(Shape) * shapeCount); 
        undoIndex--; 
    } 
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
        if (selOrderCount < MAX_TOTAL_POINTS) {
            ptSelected[s][p] = 1; 
            selOrderS[selOrderCount] = s; selOrderP[selOrderCount] = p; selOrderCount++; 
        }
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

/* --- Icon Navigation --- */
void SyncCurrentIcon(void) {
    if (currentIconIdx >= 0 && currentIconIdx < parsedCount) {
        if (parsedIcons[currentIconIdx].shapes) free(parsedIcons[currentIconIdx].shapes);
        parsedIcons[currentIconIdx].shapes = (Shape*)malloc(MAX_SHAPES * sizeof(Shape));
        if (shapeCount > 0 && parsedIcons[currentIconIdx].shapes) memcpy(parsedIcons[currentIconIdx].shapes, shapes, sizeof(Shape) * shapeCount);
        parsedIcons[currentIconIdx].shapeCount = shapeCount;
    }
}

void SwitchToIcon(int idx) {
    if (currentIconIdx == idx) return;
    SyncCurrentIcon();
    currentIconIdx = idx;
    if (idx >= 0 && idx < parsedCount) {
        currentCaseId = parsedIcons[idx].caseId; 
        shapeCount = parsedIcons[idx].shapeCount;
        if (parsedIcons[idx].shapes) memcpy(shapes, parsedIcons[idx].shapes, sizeof(Shape) * shapeCount); 
        else shapeCount = 0;
    } else shapeCount = 0;
    undoIndex = -1; ClearSelection(); selectedShape = -1;
    if (hScrlIcon) SetScrollPos(hScrlIcon, SB_CTL, currentIconIdx >= 0 ? currentIconIdx : 0, TRUE);
    UpdateStatusBar(); RedrawCanvas(hMain);
}

/* --- C File Parser --- */
void LoadCFile(const char* path, HWND hwnd) {
    FILE* f; long sz; char *d, *cur, *endBlock, *st, *pt, *sel, *next, *pC, *bracket, *lineEnd;
    int i, cId, tmpCount; double px, py;
    Shape* tmpShapes; Shape* exact; Shape s;
    HGLOBAL hMem;

    f = fopen(path, "rb"); 
    if (!f) return;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz >= 65000) sz = 65000;
    
    /* CRITICAL FIX: Safe file loading into global heap to prevent DGROUP Near Exhaustion */
    hMem = GlobalAlloc(GHND, sz + 1);
    if (!hMem) { fclose(f); return; }
    d = (char*)GlobalLock(hMem);
    
    if (d) {
        fread(d, 1, (size_t)sz, f); d[sz] = 0; fclose(f);
        
        for(i=0; i<MAX_ICONS; i++) { 
            if (parsedIcons[i].shapes) { free(parsedIcons[i].shapes); parsedIcons[i].shapes = NULL; } 
        }
        
        parsedCount = 0; 
        currentIconIdx = -1; /* CRITICAL FIX: Break double-free chains */
        cur = d;
        
        while ((cur = strstr(cur, "case ")) != NULL && parsedCount < MAX_ICONS) {
            cId = atoi(cur + 5); 
            endBlock = strstr(cur, "break;"); 
            if (!endBlock) endBlock = cur + strlen(cur);
            
            tmpShapes = (Shape*)malloc(MAX_SHAPES * sizeof(Shape));
            if (!tmpShapes) break;
            tmpCount = 0;
            st = cur;
            
            while (st < endBlock) {
                pt = strstr(st, "POINT "); 
                sel = strstr(st, "SelectObject");
                next = pt ? pt : sel;
                if (sel && (!next || sel < next)) next = sel;
                if (!next || next >= endBlock) break;
                
                if (next == pt) {
                    memset(&s, 0, sizeof(Shape));
                    s.type = 0; s.useFill = 1; s.useStroke = 1; 
                    s.fill = RGB(128,128,128); s.stroke = RGB(0,0,0);
                    pC = next; 
                    bracket = strchr(next, '}');
                    
                    if (bracket && bracket > endBlock) bracket = NULL;

                    while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) { 
                        if (sscanf(pC, "PT(%lf,%lf)", &px, &py) == 2 && s.ptCount < MAX_POINTS) { 
                            s.ptsX[s.ptCount]=px; 
                            s.ptsY[s.ptCount]=py; 
                            s.ptCount++; 
                        } 
                        pC += 3; 
                    }
                    
                    if (s.ptCount > 0) {
                        lineEnd = bracket ? strchr(bracket, '\n') : strchr(next, '\n');
                        if (lineEnd && lineEnd < endBlock) {
                            char* poly = strstr(lineEnd, "Polyline");
                            char* polycap = strstr(lineEnd, "POLY");
                            
                            if (poly && poly > endBlock) poly = NULL;
                            if (polycap && polycap > endBlock) polycap = NULL;
                            
                            if (poly && (!polycap || poly < polycap)) {
                                s.type = 2; s.useFill = 0;
                            }
                        }
                    }
                    
                    if (s.ptCount > 0 && tmpCount < MAX_SHAPES) tmpShapes[tmpCount++] = s;
                }
                st = next + 1;
            }
            
            parsedIcons[parsedCount].caseId = cId; 
            if (tmpCount > 0) {
                exact = (Shape*)malloc(MAX_SHAPES * sizeof(Shape));
                if (exact) { 
                    memcpy(exact, tmpShapes, tmpCount * sizeof(Shape)); 
                    parsedIcons[parsedCount].shapes = exact; 
                } else {
                    parsedIcons[parsedCount].shapes = NULL;
                }
            } else { parsedIcons[parsedCount].shapes = NULL; }
            
            parsedIcons[parsedCount].shapeCount = tmpCount; 
            parsedCount++; 
            cur = endBlock;
            free(tmpShapes);
        }
        GlobalUnlock(hMem);
        GlobalFree(hMem);
        
        if (parsedCount > 0) { 
            strcpy(loadedCFile, path); 
            SetScrollRange(hScrlIcon, SB_CTL, 0, parsedCount - 1, TRUE); 
            SwitchToIcon(0); 
            ShowStatus(" C Data File Loaded.");
        } else {
            ShowStatus(" No valid icons found.");
        }
    } else {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
    }
}

/* --- C Code Generation --- */
void WriteIconCCode(FILE* f, int caseId, Shape* sArr, int sCnt) {
    int cF = -2, cS = -2;
    int i, p;

    fprintf(f, "case %d: {\n", caseId);
    
    for (i = 0; i < sCnt; i++) {
        Shape* s = &sArr[i]; 
        int wS = s->useStroke ? s->stroke : -1;
        if (wS != cS) { 
            if (wS == -1) fprintf(f, "    SelectObject(hdc, GetStockObject(NULL_PEN));\n"); 
            else { 
                char colorName[32] = "gray";
                if (wS == RGB(255,255,255)) strcpy(colorName, "white");
                else if (wS == RGB(0,0,0)) strcpy(colorName, "black");
                else if (wS == RGB(128,128,128)) strcpy(colorName, "gray");
                else if (wS == RGB(192,192,192)) strcpy(colorName, "ltGray");
                else if (wS == RGB(255,0,0)) strcpy(colorName, "red");
                else if (wS == RGB(0,128,0)) strcpy(colorName, "green");
                else if (wS == RGB(0,0,128)) strcpy(colorName, "blue");
                else if (wS == RGB(255,255,0)) strcpy(colorName, "yellow");
                else if (wS == RGB(0,255,255)) strcpy(colorName, "cyan");
                else if (wS == RGB(255,128,0)) strcpy(colorName, "orange");
                else if (wS == RGB(128,0,128)) strcpy(colorName, "purple");
                fprintf(f, "    SelectObject(hdc, t->%sPen);\n", colorName); 
            }
            cS = wS; 
        }
        if (s->type == 0 || s->type == 2) { 
            int wF = s->useFill ? s->fill : -1; 
            if (wF != cF) { 
                if (wF == -1) fprintf(f, "    SelectObject(hdc, GetStockObject(NULL_BRUSH));\n"); 
                else { 
                    char colorName[32] = "gray";
                    if (wF == RGB(255,255,255)) strcpy(colorName, "white");
                    else if (wF == RGB(0,0,0)) strcpy(colorName, "black");
                    else if (wF == RGB(128,128,128)) strcpy(colorName, "gray");
                    else if (wF == RGB(192,192,192)) strcpy(colorName, "ltGray");
                    else if (wF == RGB(255,0,0)) strcpy(colorName, "red");
                    else if (wF == RGB(0,128,0)) strcpy(colorName, "green");
                    else if (wF == RGB(0,0,128)) strcpy(colorName, "blue");
                    else if (wF == RGB(255,255,0)) strcpy(colorName, "yellow");
                    else if (wF == RGB(0,255,255)) strcpy(colorName, "cyan");
                    else if (wF == RGB(255,128,0)) strcpy(colorName, "orange");
                    else if (wF == RGB(128,0,128)) strcpy(colorName, "purple");
                    fprintf(f, "    SelectObject(hdc, t->%s);\n", colorName); 
                }
                cF = wF; 
            } 
        }
        
        if (s->type == 0 && s->ptCount > 2) {
            fprintf(f, "    POINT p%d[] = { ", i);
            for (p = 0; p < s->ptCount; p++) { 
                fprintf(f, "PT(%g,%g)%s", s->ptsX[p], s->ptsY[p], p==s->ptCount-1?"":", "); 
            }
            fprintf(f, " }; POLY(p%d);\n", i);
        } else if (s->type == 2 && s->ptCount >= 2) {
            fprintf(f, "    POINT p%d[] = { ", i);
            for (p = 0; p < s->ptCount; p++) { 
                fprintf(f, "PT(%g,%g)%s", s->ptsX[p], s->ptsY[p], p==s->ptCount-1?"":", "); 
            }
            fprintf(f, " }; Polyline(hdc, p%d, %d);\n", i, s->ptCount);
        } else if (s->type == 1 && s->ptCount == 2) {
            fprintf(f, "    L(%g,%g,%g,%g);\n", s->ptsX[0], s->ptsY[0], s->ptsX[1], s->ptsY[1]);
        }
    } 
    fprintf(f, "    SelectObject(hdc, GetStockObject(BLACK_PEN));\n    SelectObject(hdc, GetStockObject(WHITE_BRUSH));\n    break;\n}\n");
}

void ToClipboard(HWND hwnd, const char* text) { 
    if (OpenClipboard(hwnd)) { 
        EmptyClipboard(); 
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(text)+1); 
        if (hMem) {
            memcpy(GlobalLock(hMem), text, strlen(text)+1); 
            GlobalUnlock(hMem); 
            SetClipboardData(CF_TEXT, hMem); 
        }
        CloseClipboard(); 
        ShowStatus("Success: GDI Code exported to clipboard!"); 
    } 
}

void ExportCode(HWND hwnd) { 
    FILE* tmp; long sz; char* buf;
    tmp = fopen("~tmp_ex.c", "w+b");
    if (tmp) {
        WriteIconCCode(tmp, currentCaseId, shapes, shapeCount);
        fseek(tmp, 0, SEEK_END); sz = ftell(tmp); fseek(tmp, 0, SEEK_SET);
        buf = (char*)malloc((size_t)sz + 1);
        if (buf) {
            fread(buf, 1, (size_t)sz, tmp); buf[sz] = 0;
            ToClipboard(hwnd, buf);
            free(buf);
        }
        fclose(tmp);
        remove("~tmp_ex.c");
    }
}

void DoSaveFile(HWND hwnd) {
    OPENFILENAME ofn; char szFile[260]; FILE* f; int i;
    memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "C Data Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "c";
    
    if (GetSaveFileName(&ofn)) {
        f = fopen(szFile, "w");
        if (f) {
            if (parsedCount == 0) {
                WriteIconCCode(f, currentCaseId, shapes, shapeCount);
            } else {
                SyncCurrentIcon();
                for (i = 0; i < parsedCount; i++) {
                    WriteIconCCode(f, parsedIcons[i].caseId, parsedIcons[i].shapes, parsedIcons[i].shapeCount);
                }
            }
            fclose(f); 
            strcpy(loadedCFile, szFile);
            ShowStatus(" File saved successfully.");
        }
    }
}

/* --- SVG Parser --- */
double GetAttr(const char* tag, const char* end, const char* attr, double def) { 
    char* p = strstr((char*)tag, attr); 
    if (p && p < end) { p += strlen(attr); return atof(p); } 
    return def; 
}

void ParseSVG(const char* path, HWND hwnd) {
    FILE* f = fopen(path, "rb"); 
    long sz;
    char* d;
    double gTx = 0, gTy = 0;
    char* gT;
    double minX=99999, minY=99999, maxX=-99999, maxY=-99999; 
    Shape* tmp;
    int tC = 0, i, p; 
    char* r;
    HGLOBAL hMem;

    if (!f) return;
    fseek(f, 0, SEEK_END); 
    sz = ftell(f); 
    fseek(f, 0, SEEK_SET); 
    if (sz >= 65000) sz = 65000;
    
    hMem = GlobalAlloc(GHND, sz + 1);
    if (!hMem) { fclose(f); return; }
    d = (char*)GlobalLock(hMem);
    
    fread(d, 1, (size_t)sz, f); 
    d[sz] = 0; 
    fclose(f);
    
    gT = strstr(d, "<g"); 
    if (gT) { 
        char* tr = strstr(gT, "translate("); 
        if (tr && tr < strchr(gT, '>')) sscanf(tr, "translate(%lf,%lf)", &gTx, &gTy); 
    }
    
    tmp = (Shape*)calloc(MAX_SHAPES, sizeof(Shape)); 
    r = d;
    
    while ((r = strpbrk(r, "<")) != NULL && tC < MAX_SHAPES) {
        char* eT; double a=1,b=0,c=0,D=1,e=0,F=0;
        char *mat, *fill, *strk;
        COLORREF fCol=RGB(128,128,128); 
        int uF=1, uS=0; 
        COLORREF sCol=RGB(0,0,0); 
        Shape s;

        if (strncmp(r, "<rect", 5) != 0 && strncmp(r, "<polygon", 8) != 0 && strncmp(r, "<path", 5) != 0) { r++; continue; }
        eT = strchr(r, '>'); 
        if (!eT) break;
        
        memset(&s, 0, sizeof(Shape));
        mat=strstr(r, "matrix("); 
        if (mat && mat < eT) sscanf(mat, "matrix(%lf,%lf,%lf,%lf,%lf,%lf)", &a,&b,&c,&D,&e,&F);
        
        fill = strstr(r, "fill:#"); 
        if (fill && fill < eT) { 
            int rC,gC,bC; 
            sscanf(fill, "fill:#%2x%2x%2x", &rC,&gC,&bC); 
            fCol=RGB(rC,gC,bC); 
        } else if (strstr(r, "fill=\"none\"")) uF=0;
        
        strk = strstr(r, "stroke:#"); 
        if (strk && strk < eT) { 
            int rC,gC,bC; 
            sscanf(strk, "stroke:#%2x%2x%2x", &rC,&gC,&bC); 
            sCol=RGB(rC,gC,bC); uS=1; 
        }
        
        s.type = 0; s.fill = fCol; s.useFill = uF; s.stroke = sCol; s.useStroke = uS;

        if (strncmp(r, "<rect", 5) == 0) {
            double rx=GetAttr(r, eT, "x=\"", 0), ry=GetAttr(r, eT, "y=\"", 0), 
                   rw=GetAttr(r, eT, "width=\"", 0), rh=GetAttr(r, eT, "height=\"", 0);
            s.ptsX[0]=rx; s.ptsY[0]=ry; s.ptsX[1]=rx+rw; s.ptsY[1]=ry; 
            s.ptsX[2]=rx+rw; s.ptsY[2]=ry+rh; s.ptsX[3]=rx; s.ptsY[3]=ry+rh; 
            s.ptCount = 4;
        } else if (strncmp(r, "<polygon", 8) == 0 || strncmp(r, "<path", 5) == 0) {
            char* pts = strstr(r, strncmp(r,"<path",5)==0 ? "d=\"" : "points=\"");
            if (pts && pts < eT) {
                pts += (strncmp(r,"<path",5)==0 ? 3 : 8); 
                double px, py; int n;
                while (*pts && *pts != '"' && s.ptCount < MAX_POINTS) {
                    if (isalpha(*pts)) { pts++; continue; }
                    if (isspace(*pts) || *pts == ',') { pts++; continue; }
                    if (sscanf(pts, "%lf%n", &px, &n) == 1) {
                        pts += n; while(*pts == ' ' || *pts == ',') pts++;
                        if (sscanf(pts, "%lf%n", &py, &n) == 1) { 
                            s.ptsX[s.ptCount]=px; s.ptsY[s.ptCount]=py; s.ptCount++; pts += n; 
                        }
                    } else pts++;
                }
            }
        }
        
        for (i=0; i<s.ptCount; i++) {
            double nx = a*s.ptsX[i] + c*s.ptsY[i] + e + gTx; 
            double ny = b*s.ptsX[i] + D*s.ptsY[i] + F + gTy;
            if (nx<minX)minX=nx; if(nx>maxX)maxX=nx; 
            if (ny<minY)minY=ny; if(ny>maxY)maxY=ny;
            s.ptsX[i] = nx; s.ptsY[i] = ny;
        } 
        if (s.ptCount >= 2 && tC < MAX_SHAPES) tmp[tC++] = s; 
        r = eT;
    } 
    GlobalUnlock(hMem);
    GlobalFree(hMem);
    
    if (tC > 0) {
        double w = maxX-minX, ht = maxY-minY; 
        double maxD = (w>ht?w:ht); 
        double sf, offX, offY;
        SaveState(); 
        shapeCount = 0; 
        if (maxD==0) maxD=1; 
        sf = (GRID_SIZE-2.0)/maxD; 
        offX = ((GRID_SIZE-2.0)-(w*sf))/2.0+1.0; 
        offY = ((GRID_SIZE-2.0)-(ht*sf))/2.0+1.0;
        
        for (i=0; i<tC; i++) { 
            shapes[shapeCount] = tmp[i]; 
            for (p=0; p<tmp[i].ptCount; p++) { 
                shapes[shapeCount].ptsX[p] = (tmp[i].ptsX[p]-minX)*sf+offX; 
                shapes[shapeCount].ptsY[p] = (tmp[i].ptsY[p]-minY)*sf+offY; 
            } 
            shapeCount++; 
        }
    } 
    UpdateStatusBar(); 
    InvalidateRect(hwnd, NULL, FALSE); 
    free(tmp);
}

/* --- Custom Win16 BMP Loader --- */
HBITMAP LoadDIBitmap16(const char* path, HDC hdc) {
    FILE* f = fopen(path, "rb");
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    int colorTableSize;
    long imageSize;
    HGLOBAL hData = NULL, hBmi = NULL;
    char* pData = NULL;
    BITMAPINFO* pbmi = NULL;
    HBITMAP hBmp = NULL;
    long bytesToRead;

    if (!f) return NULL;
    fread(&bfh, sizeof(BITMAPFILEHEADER), 1, f);
    if (bfh.bfType != 0x4D42) { fclose(f); return NULL; }

    fread(&bih, sizeof(BITMAPINFOHEADER), 1, f);
    colorTableSize = 0;
    if (bih.biBitCount < 16) {
        colorTableSize = (bih.biClrUsed ? bih.biClrUsed : (1 << bih.biBitCount)) * sizeof(RGBQUAD);
    }

    imageSize = bfh.bfSize - bfh.bfOffBits;
    if (imageSize <= 0) imageSize = bih.biSizeImage;
    if (imageSize <= 0) imageSize = ((((bih.biWidth * bih.biBitCount) + 31) & ~31) / 8) * abs(bih.biHeight);
    
    hData = GlobalAlloc(GHND, imageSize);
    if (!hData) { fclose(f); return NULL; }
    pData = (char*)GlobalLock(hData);

    fseek(f, bfh.bfOffBits, SEEK_SET);
    bytesToRead = imageSize;
    
    while(bytesToRead > 0) {
        unsigned int chunk = (bytesToRead > 60000) ? 60000 : (unsigned int)bytesToRead;
        unsigned int readCount = fread(pData + (imageSize - bytesToRead), 1, chunk, f);
        if (readCount == 0) break;
        bytesToRead -= readCount;
    }

    fseek(f, sizeof(BITMAPFILEHEADER), SEEK_SET);
    hBmi = GlobalAlloc(GHND, sizeof(BITMAPINFOHEADER) + colorTableSize);
    if (!hBmi) { GlobalUnlock(hData); GlobalFree(hData); fclose(f); return NULL; }
    pbmi = (BITMAPINFO*)GlobalLock(hBmi);

    fread(pbmi, 1, sizeof(BITMAPINFOHEADER) + colorTableSize, f);
    hBmp = CreateDIBitmap(hdc, &pbmi->bmiHeader, CBM_INIT, pData, pbmi, DIB_RGB_COLORS);

    GlobalUnlock(hBmi); GlobalFree(hBmi);
    GlobalUnlock(hData); GlobalFree(hData);
    fclose(f);
    return hBmp;
}

/* --- Reference Image Loading --- */
void LoadReferenceImage(const char* path, HWND hwnd) {
    HDC hdc;
    if (hRefBmp) { DeleteObject(hRefBmp); hRefBmp = NULL; }
    
    hdc = GetDC(hwnd);
    hRefBmp = LoadDIBitmap16(path, hdc);
    ReleaseDC(hwnd, hdc);
    
    absRefScale = 1.0; absRefPosX = 0.0; absRefPosY = 0.0; 
    absRefStrX = 1.0; absRefStrY = 1.0; refAlpha = 128;
    
    if (hTrkAlpha) SetScrollPos(hTrkAlpha, SB_CTL, 128, TRUE);
    if (hTrkScale) SetScrollPos(hTrkScale, SB_CTL, 100, TRUE);
    if (hTrkPosX) SetScrollPos(hTrkPosX, SB_CTL, 100, TRUE);
    if (hTrkPosY) SetScrollPos(hTrkPosY, SB_CTL, 100, TRUE);
    if (hTrkStrX) SetScrollPos(hTrkStrX, SB_CTL, 100, TRUE);
    if (hTrkStrY) SetScrollPos(hTrkStrY, SB_CTL, 100, TRUE);
    
    InvalidateRect(hwnd, NULL, TRUE);
}

/* --- Math & Geometry --- */
double Snap(double val) { return snapToGrid ? round(val) : val; }

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
        if (((s->ptsY[i] > py) != (s->ptsY[j] > py)) && 
            (px < (s->ptsX[j] - s->ptsX[i]) * (py - s->ptsY[i]) / (s->ptsY[j] - s->ptsY[i]) + s->ptsX[i])) 
            c = !c;
    }
    return c;
}

int IsPointOnPolyEdge(double px, double py, Shape* s) {
    int i, j; 
    double x1, y1, x2, y2, l2, t;
    for(i = 0; i < s->ptCount; i++) {
        j = (i + 1) % s->ptCount;
        x1 = s->ptsX[i]; y1 = s->ptsY[i]; x2 = s->ptsX[j]; y2 = s->ptsY[j];
        l2 = pow(x2-x1, 2) + pow(y2-y1, 2); 
        if (l2 < 1e-7) continue;
        if (fabs((px-x1)*(y2-y1) - (py-y1)*(x2-x1)) / sqrt(l2) < 1e-5) {
            t = ((px-x1)*(x2-x1) + (py-y1)*(y2-y1)) / l2;
            if (t >= -1e-5 && t <= 1 + 1e-5) return 1;
        }
    } 
    return 0;
}

void AddEdgesFromShape(Shape* s1, Shape* s2, Edge* pool, int* edgeCount) {
    int i, j, a, b, nCnt; 
    double p1x, p1y, p2x, p2y, p3x, p3y, p4x, p4y, d, t, u, px, py, l2, tx, ty, midX, midY;
    double nx[MAX_POINTS], ny[MAX_POINTS];
    
    for (i = 0; i < s1->ptCount; i++) {
        p1x = s1->ptsX[i]; p1y = s1->ptsY[i];
        p2x = s1->ptsX[(i+1)%s1->ptCount]; p2y = s1->ptsY[(i+1)%s1->ptCount];
        nCnt = 0; nx[nCnt] = p1x; ny[nCnt++] = p1y;
        
        for (j = 0; j < s2->ptCount; j++) {
            p3x = s2->ptsX[j]; p3y = s2->ptsY[j];
            p4x = s2->ptsX[(j+1)%s2->ptCount]; p4y = s2->ptsY[(j+1)%s2->ptCount];
            d = (p1x-p2x)*(p3y-p4y) - (p1y-p2y)*(p3x-p4x);
            if (fabs(d) > 1e-7) {
                t = ((p1x-p3x)*(p3y-p4y) - (p1y-p3y)*(p3x-p4x)) / d;
                u = ((p1x-p3x)*(p1y-p2y) - (p1y-p3y)*(p1x-p2x)) / d;
                if (t > 1e-5 && t < 1 - 1e-5 && u > 1e-5 && u < 1 - 1e-5) { 
                    nx[nCnt] = p1x + t*(p2x-p1x); ny[nCnt++] = p1y + t*(p2y-p1y); 
                }
            }
        }
        
        for (j = 0; j < s2->ptCount; j++) {
            px = s2->ptsX[j]; py = s2->ptsY[j];
            l2 = pow(p2x-p1x, 2) + pow(p2y-p1y, 2);
            if (l2 > 1e-7 && fabs((px-p1x)*(p2y-p1y) - (py-p1y)*(p2x-p1x)) / sqrt(l2) < 1e-5) {
                t = ((px-p1x)*(p2x-p1x) + (py-p1y)*(p2y-p1y)) / l2;
                if (t > 1e-5 && t < 1 - 1e-5) { nx[nCnt] = px; ny[nCnt++] = py; }
            }
        }

        nx[nCnt]=p2x; ny[nCnt++]=p2y;
        for(a=0; a<nCnt-1; a++) { 
            for(b=a+1; b<nCnt; b++) { 
                if (pow(nx[b]-p1x,2) + pow(ny[b]-p1y,2) < pow(nx[a]-p1x,2) + pow(ny[a]-p1y,2)) { 
                    tx = nx[a]; ty = ny[a]; nx[a] = nx[b]; ny[a] = ny[b]; nx[b] = tx; ny[b] = ty; 
                } 
            } 
        }
        for(a=0; a<nCnt-1; a++) {
            if (pow(nx[a]-nx[a+1], 2) + pow(ny[a]-ny[a+1], 2) < 1e-7) continue;
            midX = (nx[a]+nx[a+1])/2.0; midY = (ny[a]+ny[a+1])/2.0;
            if (!PointInPolyShape(midX, midY, s2) || IsPointOnPolyEdge(midX, midY, s2)) { 
                if (*edgeCount < 256) { 
                    pool[*edgeCount].x1 = nx[a]; pool[*edgeCount].y1 = ny[a]; 
                    pool[*edgeCount].x2 = nx[a+1]; pool[*edgeCount].y2 = ny[a+1]; 
                    (*edgeCount)++; 
                }
            }
        }
    }
}

void GenShape(double eX, double eY) {
    int i; double cx, cy, rx, ry;
    if (currentMode == 4) {
        currentShape.ptCount = 2; 
        currentShape.ptsX[0] = startX; currentShape.ptsY[0] = startY;
        currentShape.ptsX[1] = eX; currentShape.ptsY[1] = eY; 
        return;
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
        currentShape.ptsX[i] = cx + cos(ang) * rx * rF; 
        currentShape.ptsY[i] = cy + sin(ang) * ry * rF; 
    }
}

/* --- Subclassed Edit Control --- */
LRESULT CALLBACK _export DistEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    int i; 
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
    HPEN hOldPen = (HPEN)SelectObject(dc, hPen);
    for (i = 0; i <= GRID_SIZE; i++) {
        MoveTo(dc, 0, i * scaleFactor); 
        LineTo(dc, canvasSize, i * scaleFactor);
        MoveTo(dc, i * scaleFactor, 0); 
        LineTo(dc, i * scaleFactor, canvasSize);
    }
    SelectObject(dc, hOldPen); DeleteObject(hPen);
}

void DrawPalette(HDC dc) {
    int i, col, row, cx = canvasSize + 15;
    HBRUSH hBr, hOldBr, tempBr; 
    HPEN hPen = (HPEN)GetStockObject(BLACK_PEN); 
    HPEN hOldPen = (HPEN)SelectObject(dc, hPen);
    
    for (i = 0; i < 16; i++) {
        col = i % 8; row = i / 8;
        hBr = CreateSolidBrush(palette[i]); 
        hOldBr = (HBRUSH)SelectObject(dc, hBr);
        Rectangle(dc, cx + col * 32, 356 + row * 16, cx + col * 32 + 32, 356 + row * 16 + 16);
        SelectObject(dc, hOldBr); DeleteObject(hBr);
    }
    
    tempBr = useFill ? CreateSolidBrush(currentFill) : NULL;
    hBr = tempBr ? tempBr : (HBRUSH)GetStockObject(NULL_BRUSH);
    hOldBr = (HBRUSH)SelectObject(dc, hBr); 
    Rectangle(dc, cx, 396, cx + 32, 428);
    SelectObject(dc, hOldBr); 
    if (tempBr) DeleteObject(tempBr); 
    
    tempBr = useStroke ? CreateSolidBrush(currentStroke) : NULL;
    hBr = tempBr ? tempBr : (HBRUSH)GetStockObject(NULL_BRUSH);
    hOldBr = (HBRUSH)SelectObject(dc, hBr); 
    Rectangle(dc, cx + 80, 396, cx + 112, 428);
    SelectObject(dc, hOldBr); 
    if (tempBr) DeleteObject(tempBr); 
    
    SelectObject(dc, hOldPen); 
    SetBkMode(dc, TRANSPARENT);
    TextOut(dc, cx + 40, 404, "Fill", 4); 
    TextOut(dc, cx + 120, 404, "Stroke", 6);
    TextOut(dc, cx, 440, "Sides:", 6); 
    TextOut(dc, cx, 465, "Depth:", 6);
}

void RenderShapes(HDC dc, Shape* sArr, int sCnt, int sc, int offX, int offY, Shape* activeShape, int isActDrawing) {
    int i, j;
    for (i = 0; i <= sCnt; i++) {
        Shape* s = (i == sCnt) ? (isActDrawing ? activeShape : NULL) : &sArr[i]; 
        HBRUSH b, tempB; HPEN p, tempP; HGDIOBJ ob, op; 
        POINT pA[MAX_POINTS];
        
        if (!s || s->ptCount == 0) continue;
        
        tempB = s->useFill ? CreateSolidBrush(s->fill) : NULL;
        tempP = s->useStroke ? CreatePen(PS_SOLID, 1, s->stroke) : NULL;
        
        b = tempB ? tempB : (HBRUSH)GetStockObject(NULL_BRUSH);
        p = tempP ? tempP : (HPEN)GetStockObject(NULL_PEN);
        
        ob = SelectObject(dc, b); 
        op = SelectObject(dc, p);
        
        for(j=0; j<s->ptCount; j++) { 
            double vx = offX + s->ptsX[j] * sc;
            double vy = offY + s->ptsY[j] * sc;
            if (vx < -16000.0) vx = -16000.0; if (vx > 16000.0) vx = 16000.0;
            if (vy < -16000.0) vy = -16000.0; if (vy > 16000.0) vy = 16000.0;
            pA[j].x = (int)round(vx); 
            pA[j].y = (int)round(vy); 
        }
        
        if (s->type == 0) { 
            SetPolyFillMode(dc, ALTERNATE); Polygon(dc, pA, s->ptCount); 
        } 
        else Polyline(dc, pA, s->ptCount);
        
        SelectObject(dc, ob); SelectObject(dc, op);
        if (tempB) DeleteObject(tempB); 
        if (tempP) DeleteObject(tempP);
    }
}

void DrawReferenceImage(HDC dc) {
    int dstW, dstH, dstX, dstY, oldMode;
    HDC ht;
    BITMAP bm;
    HGDIOBJ hOld;

    if (!hRefBmp) return;
    
    dstW = (int)(canvasSize * absRefScale * absRefStrX); 
    dstH = (int)(canvasSize * absRefScale * absRefStrY);
    dstX = (canvasSize - dstW) / 2 + (int)absRefPosX; 
    dstY = (canvasSize - dstH) / 2 + (int)absRefPosY;
    
    if (dstW > 0 && dstH > 0) {
        ht = CreateCompatibleDC(dc); 
        hOld = SelectObject(ht, hRefBmp); 
        GetObject(hRefBmp, sizeof(bm), &bm);
        
        oldMode = SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, dstX, dstY, dstW, dstH, ht, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY); 
        SetStretchBltMode(dc, oldMode);
        
        SelectObject(ht, hOld);
        DeleteDC(ht); 
    }
}

void DrawPreview(HDC dc) {
    int cx = canvasSize + 15, px = cx + 220, py = 394; 
    HBRUSH hBr = (HBRUSH)GetStockObject(WHITE_BRUSH);
    HPEN hPen = (HPEN)GetStockObject(BLACK_PEN);
    HGDIOBJ hOldBr = SelectObject(dc, hBr);
    HGDIOBJ hOldPen = SelectObject(dc, hPen);
    
    Rectangle(dc, px, py, px + 34, py + 34);
    SelectObject(dc, hOldBr); 
    SelectObject(dc, hOldPen);
    
    if (shapes) {
        HDC previewDC = CreateCompatibleDC(dc);
        HBITMAP previewBmp = CreateCompatibleBitmap(dc, 34, 34);
        HGDIOBJ pOld = SelectObject(previewDC, previewBmp);
        RECT rPrev; rPrev.left = 0; rPrev.top = 0; rPrev.right = 34; rPrev.bottom = 34;

        FillRect(previewDC, &rPrev, (HBRUSH)GetStockObject(WHITE_BRUSH));
        RenderShapes(previewDC, shapes, shapeCount, 1, 0, 0, NULL, 0);
        
        BitBlt(dc, px, py, 34, 34, previewDC, 0, 0, SRCCOPY);
        
        SelectObject(previewDC, pOld);
        DeleteObject(previewBmp);
        DeleteDC(previewDC);
    }
    
    SetBkMode(dc, TRANSPARENT); 
    TextOut(dc, px - 60, py + 8, "Preview:", 8);
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
                    double vx = shapes[i].ptsX[j] * scaleFactor;
                    double vy = shapes[i].ptsY[j] * scaleFactor;
                    if (vx < -16000.0) vx = -16000.0; if (vx > 16000.0) vx = 16000.0;
                    if (vy < -16000.0) vy = -16000.0; if (vy > 16000.0) vy = 16000.0;
                    px = (int)round(vx);
                    py = (int)round(vy);
                    
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
    SelectObject(dc, hOrigBr); 
    SelectObject(dc, hOrigPen);
    DeleteObject(hSelBr); DeleteObject(hUnselBr); 
    DeleteObject(hHovBr); DeleteObject(hEdgeBr);
}

/* --- Main Windows Procedure --- */
LRESULT CALLBACK _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int i;
            shapes = (Shape*)malloc(MAX_SHAPES * sizeof(Shape));
            dragStartSnapshot = (Shape*)malloc(MAX_SHAPES * sizeof(Shape));
            history = (Shape**)malloc(MAX_UNDO * sizeof(Shape*));
            for(i=0; i<MAX_UNDO; i++) history[i] = (Shape*)malloc(MAX_SHAPES * sizeof(Shape));
            
            if (!shapes || !dragStartSnapshot || !history || !history[0]) {
                MessageBox(hwnd, "Failed to allocate memory!", "Error", MB_ICONHAND);
                PostQuitMessage(0); return -1;
            }

            InitFont();
            hStatus = CreateWindow("STATIC", " Ready", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)100, hInst, NULL);
            hScrlSides = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)153, hInst, NULL);
            hScrlDepth = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)154, hInst, NULL);
            hScrlIcon  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)155, hInst, NULL);
            
            SetScrollRange(hScrlSides, SB_CTL, 3, 32, FALSE); SetScrollPos(hScrlSides, SB_CTL, paramSides, TRUE);
            SetScrollRange(hScrlDepth, SB_CTL, 10, 100, FALSE); SetScrollPos(hScrlDepth, SB_CTL, paramStar, TRUE);
            SetScrollRange(hScrlIcon,  SB_CTL, 0, 0, FALSE); SetScrollPos(hScrlIcon, SB_CTL, 0, TRUE);
            
            for(i = 0; i < 28; i++) hBtn[i] = CreateWindow("BUTTON", bT[i], WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)(200+i), hInst, NULL);
            
            hDistEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 60, 20, hwnd, (HMENU)300, hInst, NULL);
            subclassThunk = MakeProcInstance((FARPROC)DistEditProc, hInst);
            oldEditProc = (FARPROC)SetWindowLong(hDistEdit, GWL_WNDPROC, (LONG)subclassThunk);
            
            hTrkAlpha = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)160, hInst, NULL);
            hTrkScale = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)161, hInst, NULL);
            hTrkPosX  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)162, hInst, NULL);
            hTrkPosY  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)163, hInst, NULL);
            hTrkStrX  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)164, hInst, NULL);
            hTrkStrY  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)165, hInst, NULL);
            SetScrollRange(hTrkAlpha, SB_CTL, 0, 255, FALSE); SetScrollPos(hTrkAlpha, SB_CTL, refAlpha, TRUE);
            SetScrollRange(hTrkScale, SB_CTL, 10, 190, FALSE); SetScrollPos(hTrkScale, SB_CTL, 100, TRUE);
            SetScrollRange(hTrkPosX, SB_CTL, 10, 190, FALSE); SetScrollPos(hTrkPosX, SB_CTL, 100, TRUE);
            SetScrollRange(hTrkPosY, SB_CTL, 10, 190, FALSE); SetScrollPos(hTrkPosY, SB_CTL, 100, TRUE);
            SetScrollRange(hTrkStrX, SB_CTL, 10, 190, FALSE); SetScrollPos(hTrkStrX, SB_CTL, 100, TRUE);
            SetScrollRange(hTrkStrY, SB_CTL, 10, 190, FALSE); SetScrollPos(hTrkStrY, SB_CTL, 100, TRUE);
            break;
        }
        case WM_SIZE: {
            int cx, w, by, i, cw, ch, spaceW, spaceH, imgBottom, trackW;
            cw = (int)LOWORD(lParam);
            ch = (int)HIWORD(lParam);
            
            if (cw <= 0 || ch <= 0) return 0;
            
            spaceW = cw - PANEL_WIDTH;
            spaceH = ch - 100;
            
            if (spaceW < 64) spaceW = 64;
            if (spaceH < 64) spaceH = 64;
            
            scaleFactor = (spaceW < spaceH) ? (spaceW / GRID_SIZE) : (spaceH / GRID_SIZE);
            if (scaleFactor < 1) scaleFactor = 1; 
            canvasSize = scaleFactor * GRID_SIZE;
            
            cx = canvasSize + 15; w = PANEL_WIDTH - 30; by = 10;
            for(i = 0; i < 28; i++) {
                MoveWindow(hBtn[i], cx + (i%2)*(w/2 + 2), by + (i/2)*26, (w/2)-4, 24, TRUE); 
            }
            MoveWindow(hScrlSides, cx + 50, 440, w - 50, 18, TRUE);
            MoveWindow(hScrlDepth, cx + 50, 465, w - 50, 18, TRUE);
            MoveWindow(hStatus, 0, ch - 20, cw, 20, TRUE);
            
            imgBottom = canvasSize + 5;
            trackW = canvasSize / 2 - 45;
            if (trackW < 10) trackW = 10;
            
            MoveWindow(hScrlIcon, 45, imgBottom, canvasSize - 50, 15, TRUE); imgBottom += 18;
            MoveWindow(hTrkAlpha, 45, imgBottom, trackW, 15, TRUE);
            MoveWindow(hTrkScale, 45 + canvasSize/2, imgBottom, trackW, 15, TRUE); imgBottom += 18;
            MoveWindow(hTrkPosX,  45, imgBottom, trackW, 15, TRUE);
            MoveWindow(hTrkPosY,  45 + canvasSize/2, imgBottom, trackW, 15, TRUE); imgBottom += 18;
            MoveWindow(hTrkStrX,  45, imgBottom, trackW, 15, TRUE);
            MoveWindow(hTrkStrY,  45 + canvasSize/2, imgBottom, trackW, 15, TRUE);
            
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        case WM_HSCROLL: {
            HWND hTrk = (HWND)lParam;
            if (hTrk == hScrlIcon) {
                int pos = GetScrollPos(hScrlIcon, SB_CTL);
                switch(LOWORD(wParam)) { 
                    case SB_LINELEFT: pos--; break; 
                    case SB_LINERIGHT: pos++; break; 
                    case SB_PAGELEFT: pos-=10; break; 
                    case SB_PAGERIGHT: pos+=10; break; 
                    case SB_THUMBTRACK: 
                    case SB_THUMBPOSITION: pos = HIWORD(wParam); break; 
                }
                if (pos < 0) pos = 0; 
                if (parsedCount > 0 && pos >= parsedCount) pos = parsedCount - 1;
                SwitchToIcon(pos);
            } else if (hTrk == hTrkAlpha) {
                refAlpha = GetScrollPos(hTrkAlpha, SB_CTL);
                switch(LOWORD(wParam)) { case SB_THUMBTRACK: case SB_THUMBPOSITION: refAlpha = HIWORD(wParam); break; }
            } else if (hTrk == hTrkScale) {
                int p = GetScrollPos(hTrkScale, SB_CTL); 
                switch(LOWORD(wParam)) { case SB_THUMBTRACK: case SB_THUMBPOSITION: p = HIWORD(wParam); break; }
                absRefScale *= (p / 100.0); 
            } else if (hTrk == hTrkPosX) {
                int p = GetScrollPos(hTrkPosX, SB_CTL); 
                switch(LOWORD(wParam)) { case SB_THUMBTRACK: case SB_THUMBPOSITION: p = HIWORD(wParam); break; }
                absRefPosX += (p - 100);
            } else if (hTrk == hTrkPosY) {
                int p = GetScrollPos(hTrkPosY, SB_CTL); 
                switch(LOWORD(wParam)) { case SB_THUMBTRACK: case SB_THUMBPOSITION: p = HIWORD(wParam); break; }
                absRefPosY += (p - 100);
            } else if (hTrk == hTrkStrX) {
                int p = GetScrollPos(hTrkStrX, SB_CTL); 
                switch(LOWORD(wParam)) { case SB_THUMBTRACK: case SB_THUMBPOSITION: p = HIWORD(wParam); break; }
                absRefStrX *= (p / 100.0);
            } else if (hTrk == hTrkStrY) {
                int p = GetScrollPos(hTrkStrY, SB_CTL); 
                switch(LOWORD(wParam)) { case SB_THUMBTRACK: case SB_THUMBPOSITION: p = HIWORD(wParam); break; }
                absRefStrY *= (p / 100.0);
            }
            if (hTrk == hTrkAlpha || hTrk == hTrkScale || hTrk == hTrkPosX || hTrk == hTrkPosY || hTrk == hTrkStrX || hTrk == hTrkStrY) {
                SetScrollPos(hTrkAlpha, SB_CTL, refAlpha, TRUE);
                SetScrollPos(hTrkScale, SB_CTL, 100, TRUE);
                SetScrollPos(hTrkPosX, SB_CTL, 100, TRUE);
                SetScrollPos(hTrkPosY, SB_CTL, 100, TRUE);
                SetScrollPos(hTrkStrX, SB_CTL, 100, TRUE);
                SetScrollPos(hTrkStrY, SB_CTL, 100, TRUE);
                InvalidateRect(hMain, NULL, TRUE);
            }
            break;
        }
        case WM_CHAR: {
            if (currentMode == 7 && textCursorActive) {
                unsigned char c = (unsigned char)toupper((int)wParam);
                int r, c_idx, bit, start_c, glyph;
                Shape s;
                if (c == 13) { textCursorY += 6; textCursorX = startX; } 
                else if (c == 8) { textCursorX = fmax(0, textCursorX - 4); } 
                else if (c < 128 && (font5x3[c] || c == ' ')) {
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
                                    s.fill = currentFill; s.stroke = currentStroke; 
                                    s.useFill = useFill; s.useStroke = useStroke;
                                    s.ptsX[0] = textCursorX + start_c; s.ptsY[0] = textCursorY + r; 
                                    s.ptsX[1] = textCursorX + c_idx; s.ptsY[1] = textCursorY + r;
                                    s.ptsX[2] = textCursorX + c_idx; s.ptsY[2] = textCursorY + r + 1; 
                                    s.ptsX[3] = textCursorX + start_c; s.ptsY[3] = textCursorY + r + 1;
                                    if(shapeCount < MAX_SHAPES) shapes[shapeCount++] = s; 
                                    start_c = -1;
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
                if (distEditMode != 0 && IsWindowVisible(hDistEdit)) { 
                    ShowWindow(hDistEdit, SW_HIDE); distEditMode = 0; RedrawCanvas(hwnd); return 0; 
                }
                if (isDrawing) { isDrawing = 0; currentShape.ptCount = 0; }
                if (shapeCount > 0) currentMode = 0; 
                ClearSelection(); RedrawCanvas(hwnd); return 0;
            }
            if (wParam == VK_RETURN) {
                if (currentMode == 0 && selOrderCount == 2) { SendMessage(hwnd, WM_COMMAND, 222, 0); return 0; } 
                if (isDrawing && (currentMode == 3 || currentMode == 5) && currentShape.ptCount >= 2) { 
                    SaveState(); shapes[shapeCount++] = currentShape; 
                    isDrawing = 0; currentShape.ptCount = 0; 
                    selectedShape = shapeCount - 1; currentMode = 0; 
                    UpdateStatusBar(); RedrawCanvas(hwnd); return 0;
                }
            }
            if (wParam == VK_DELETE || wParam == VK_BACK) {
                if (isDrawing && currentShape.ptCount > 0) { 
                    currentShape.ptCount--; if (currentShape.ptCount == 0) isDrawing = 0; 
                    RedrawCanvas(hwnd); 
                }
                else if (currentMode == 0 || currentMode == 1 || currentMode == 2) {
                    if (selOrderCount > 0) {
                        SaveState();
                        for (i = shapeCount - 1; i >= 0; i--) {
                            for (k = shapes[i].ptCount - 1; k >= 0; k--) {
                                if (ptSelected[i][k]) {
                                    for(j = k; j < shapes[i].ptCount - 1; j++) { 
                                        shapes[i].ptsX[j] = shapes[i].ptsX[j+1]; 
                                        shapes[i].ptsY[j] = shapes[i].ptsY[j+1]; 
                                    }
                                    shapes[i].ptCount--;
                                }
                            }
                            if (shapes[i].ptCount < (shapes[i].type==0?3:2)) { 
                                for(k = i; k < shapeCount - 1; k++) shapes[k] = shapes[k+1]; 
                                shapeCount--; 
                            }
                        }
                        ClearSelection(); selectedShape = -1; UpdateStatusBar(); RedrawCanvas(hwnd);
                    }
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam), y = HIWORD(lParam), cx = canvasSize + 15, shiftDown, ctrlDown, origCount, i, j, k;
            double gx, gy, minX, maxX, minY, maxY;
            int clickShape = -1, clickPt = -1, clickSegShape = -1, clickSegPt = -1, clickBody = -1;
            double bestDist = 9999.0;
            
            if (x >= cx && x < cx + 256 && y >= 356 && y < 388) {
                int index = ((y - 356) / 16) * 8 + (x - cx) / 32;
                if (index >= 0 && index < 16) {
                    currentFill = palette[index]; useFill = 1;
                    if (selectedShape != -1) { 
                        SaveState(); shapes[selectedShape].fill = currentFill; shapes[selectedShape].useFill = 1; 
                    }
                    InvalidateRect(hwnd, NULL, TRUE); return 0;
                }
            }
            SetFocus(hwnd); SetCapture(hwnd);
            startX = Snap(x / (double)scaleFactor); startY = Snap(y / (double)scaleFactor);
            gx = startX; gy = startY;

            if (currentMode == 7) { textCursorX = gx; textCursorY = gy; textCursorActive = 1; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            
            if (currentMode == 8) {
                for (i = shapeCount - 1; i >= 0; i--) {
                    if (PointInPolyShape(gx, gy, &shapes[i]) || IsPointOnPolyEdge(gx, gy, &shapes[i])) { clickBody = i; break; }
                }
                if (clickBody != -1) { SaveState(); shapes[clickBody].fill = currentFill; shapes[clickBody].useFill = 1; RedrawCanvas(hwnd); }
                return 0;
            }
            
            if (currentMode == 0 || currentMode == 1 || currentMode == 2) { 
                shiftDown = (GetKeyState(VK_SHIFT) & 0x8000); ctrlDown  = (GetKeyState(VK_CONTROL) & 0x8000);
                
                for (i = shapeCount - 1; i >= 0; i--) {
                    for (j = 0; j < shapes[i].ptCount; j++) {
                        double d = sqrt(pow(shapes[i].ptsX[j]*scaleFactor - x, 2) + pow(shapes[i].ptsY[j]*scaleFactor - y, 2));
                        if (d < 15.0 && d < bestDist) { clickShape = i; clickPt = j; bestDist = d; }
                    }
                }
                if (clickPt == -1) {
                    bestDist = 9999.0;
                    for (i = shapeCount - 1; i >= 0; i--) {
                        if (shapes[i].ptCount >= MAX_POINTS) continue;
                        for (j = 0; j < (shapes[i].type == 0 ? shapes[i].ptCount : shapes[i].ptCount - 1); j++) {
                            int np = (j + 1) % shapes[i].ptCount; double prX, prY, d;
                            PtToSegProj((double)x, (double)y, shapes[i].ptsX[j]*scaleFactor, shapes[i].ptsY[j]*scaleFactor, shapes[i].ptsX[np]*scaleFactor, shapes[i].ptsY[np]*scaleFactor, &prX, &prY, &d);
                            if (d < 10.0 && d < bestDist) { clickSegShape = i; clickSegPt = j; hoverProjX = prX; hoverProjY = prY; bestDist = d; }
                        }
                    }
                }
                if (clickPt == -1 && clickSegShape == -1) {
                    for (i = shapeCount - 1; i >= 0; i--) {
                        if (PointInPolyShape(gx, gy, &shapes[i]) || IsPointOnPolyEdge(gx, gy, &shapes[i])) { clickBody = i; break; }
                    }
                }

                if (clickPt != -1) {
                    if (shiftDown) ToggleSelection(clickShape, clickPt);
                    else if (!ptSelected[clickShape][clickPt]) { ClearSelection(); ToggleSelection(clickShape, clickPt); }
                    selectedShape = clickShape;
                } else if (clickSegShape != -1 && shiftDown) {
                    SaveState(); i = clickSegShape; j = clickSegPt;
                    for(k = shapes[i].ptCount; k > j + 1; k--) { shapes[i].ptsX[k] = shapes[i].ptsX[k-1]; shapes[i].ptsY[k] = shapes[i].ptsY[k-1]; }
                    shapes[i].ptsX[j+1] = Snap(hoverProjX / scaleFactor); shapes[i].ptsY[j+1] = Snap(hoverProjY / scaleFactor); 
                    shapes[i].ptCount++;
                    ClearSelection(); ToggleSelection(i, j+1); selectedShape = i;
                } else if (clickBody != -1) {
                    int sHasSel = 0, allSel = 1;
                    for(j = 0; j < shapes[clickBody].ptCount; j++) { if (ptSelected[clickBody][j]) sHasSel = 1; else allSel = 0; }
                    if (shiftDown) {
                        for(j = 0; j < shapes[clickBody].ptCount; j++) {
                            if (allSel && ptSelected[clickBody][j]) ToggleSelection(clickBody, j);
                            else if (!allSel && !ptSelected[clickBody][j]) ToggleSelection(clickBody, j);
                        }
                    } else if (!sHasSel) {
                        ClearSelection(); for(j = 0; j < shapes[clickBody].ptCount; j++) ToggleSelection(clickBody, j);
                    }
                    selectedShape = clickBody;
                } else { ClearSelection(); selectedShape = -1; }

                if (selectedShape != -1) {
                    currentFill = shapes[selectedShape].fill; useFill = shapes[selectedShape].useFill; 
                    currentStroke = shapes[selectedShape].stroke; useStroke = shapes[selectedShape].useStroke;
                    
                    if (ctrlDown && selOrderCount > 0) {
                        origCount = shapeCount; SaveState();
                        for (i = 0; i < origCount; i++) {
                            int sHasSel = 0; for(j = 0; j < shapes[i].ptCount; j++) if(ptSelected[i][j]) sHasSel = 1;
                            if (sHasSel && shapeCount < MAX_SHAPES) { 
                                shapes[shapeCount] = shapes[i]; 
                                for(j = 0; j < shapes[i].ptCount; j++) {
                                    if(ptSelected[i][j]) { ToggleSelection(i, j); ToggleSelection(shapeCount, j); }
                                } 
                                shapeCount++; 
                            }
                        }
                    }
                    
                    memcpy(dragStartSnapshot, shapes, sizeof(Shape) * shapeCount);
                    isDraggingNodes = 1; dragStartX = startX; dragStartY = startY;
                    minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                    for(i = 0; i < shapeCount; i++) {
                        for(j = 0; j < shapes[i].ptCount; j++) {
                            if (ptSelected[i][j]) { 
                                minX = fmin(minX, shapes[i].ptsX[j]); maxX = fmax(maxX, shapes[i].ptsX[j]); 
                                minY = fmin(minY, shapes[i].ptsY[j]); maxY = fmax(maxY, shapes[i].ptsY[j]); 
                            }
                        }
                    }
                    shapeCx = (minX + maxX)/2.0; shapeCy = (minY + maxY)/2.0;
                }
                RedrawCanvas(hwnd); return 0;
            }
            else if (currentMode == 4 || currentMode == 6) {
                currentEndX = startX; currentEndY = startY; isDrawing = 1;
                currentShape.type = (currentMode == 4) ? 1 : 0;
                currentShape.useFill = useFill; currentShape.fill = currentFill;
                currentShape.useStroke = useStroke; currentShape.stroke = currentStroke;
                if (currentMode == 4) { currentShape.ptCount = 2; currentShape.ptsX[0] = startX; currentShape.ptsY[0] = startY; currentShape.ptsX[1] = currentEndX; currentShape.ptsY[1] = currentEndY; }
                else GenShape(currentEndX, currentEndY);
                RedrawCanvas(hwnd);
            }
            else if (currentMode == 3 || currentMode == 5) {
                if (!isDrawing) {
                    isDrawing = 1; currentShape.type = (currentMode == 3) ? 0 : 2; 
                    currentShape.useFill = (currentMode == 3) ? useFill : 0; currentShape.fill = currentFill;
                    currentShape.useStroke = useStroke; currentShape.stroke = currentStroke;
                    currentShape.ptsX[0] = startX; currentShape.ptsY[0] = startY;
                    currentShape.ptsX[1] = startX; currentShape.ptsY[1] = startY; 
                    currentShape.ptCount = 2;
                } else if (currentShape.ptCount < MAX_POINTS) {
                    currentShape.ptsX[currentShape.ptCount-1] = startX; currentShape.ptsY[currentShape.ptCount-1] = startY;
                    currentShape.ptCount++; 
                    currentShape.ptsX[currentShape.ptCount-1] = startX; currentShape.ptsY[currentShape.ptCount-1] = startY;
                }
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            double nx, ny, dx, dy, bestDist, prX, prY, d, newX, newY;
            int x = LOWORD(lParam), y = HIWORD(lParam), i, j, p, np;
            nx = Snap(x / (double)scaleFactor); ny = Snap(y / (double)scaleFactor);
            
            if (isDraggingNodes) {
                dx = nx - dragStartX; dy = ny - dragStartY;
                if (dx != 0 || dy != 0) {
                    if (currentMode == 1) { 
                        double theta = atan2(ny - shapeCy, nx - shapeCx) - atan2(dragStartY - shapeCy, dragStartX - shapeCx);
                        for(i = 0; i < shapeCount; i++) {
                            for(j = 0; j < shapes[i].ptCount; j++) {
                                if (ptSelected[i][j]) {
                                    double ox = dragStartSnapshot[i].ptsX[j]; double oy = dragStartSnapshot[i].ptsY[j];
                                    newX = shapeCx + (ox - shapeCx)*cos(theta) - (oy - shapeCy)*sin(theta);
                                    newY = shapeCy + (ox - shapeCx)*sin(theta) + (oy - shapeCy)*cos(theta);
                                    shapes[i].ptsX[j] = snapToGrid ? round(newX) : newX; shapes[i].ptsY[j] = snapToGrid ? round(newY) : newY;
                                }
                            }
                        }
                    } 
                    else if (currentMode == 2) { 
                        double d0 = sqrt(pow(dragStartX - shapeCx, 2) + pow(dragStartY - shapeCy, 2));
                        double d1 = sqrt(pow(nx - shapeCx, 2) + pow(ny - shapeCy, 2));
                        double S = (d0 > 0.001) ? (d1 / d0) : 1.0;
                        for(i = 0; i < shapeCount; i++) {
                            for(j = 0; j < shapes[i].ptCount; j++) {
                                if (ptSelected[i][j]) {
                                    double ox = dragStartSnapshot[i].ptsX[j]; double oy = dragStartSnapshot[i].ptsY[j];
                                    newX = shapeCx + (ox - shapeCx)*S; newY = shapeCy + (oy - shapeCy)*S;
                                    shapes[i].ptsX[j] = snapToGrid ? round(newX) : newX; shapes[i].ptsY[j] = snapToGrid ? round(newY) : newY;
                                }
                            }
                        }
                    } 
                    else { 
                        for(i = 0; i < shapeCount; i++) {
                            for(j = 0; j < shapes[i].ptCount; j++) {
                                if (ptSelected[i][j]) {
                                    newX = dragStartSnapshot[i].ptsX[j] + dx; newY = dragStartSnapshot[i].ptsY[j] + dy;
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
                if (currentMode == 4 || currentMode == 6) { 
                    currentEndX = nx; currentEndY = ny; 
                    if (currentMode == 4) { currentShape.ptsX[1] = currentEndX; currentShape.ptsY[1] = currentEndY; }
                    else GenShape(currentEndX, currentEndY); 
                } 
                else if (currentMode == 3 || currentMode == 5) { 
                    currentShape.ptsX[currentShape.ptCount-1] = nx; currentShape.ptsY[currentShape.ptCount-1] = ny; 
                }
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (isDraggingNodes) { isDraggingNodes = 0; SaveState(); }
            else if (isDrawing && (currentMode == 4 || currentMode == 6)) {
                isDrawing = 0;
                if (currentShape.ptCount > 0 && shapeCount < MAX_SHAPES) { 
                    SaveState(); shapes[shapeCount++] = currentShape; 
                    selectedShape = shapeCount-1; currentShape.ptCount = 0; UpdateStatusBar(); 
                }
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_RBUTTONDOWN: {
            int x = LOWORD(lParam), y = HIWORD(lParam), i, p, k;
            if (currentMode == 0 || currentMode == 1 || currentMode == 2) {
                for (i = 0; i < shapeCount; i++) {
                    for (p = 0; p < shapes[i].ptCount; p++) {
                        int px = (int)round(shapes[i].ptsX[p] * scaleFactor), py = (int)round(shapes[i].ptsY[p] * scaleFactor);
                        if (sqrt(pow(px - x, 2) + pow(py - y, 2)) <= 8.0) {
                            if (shapes[i].ptCount > (shapes[i].type == 0 ? 3 : 2)) {
                                SaveState();
                                for (k = p; k < shapes[i].ptCount - 1; k++) {
                                    shapes[i].ptsX[k] = shapes[i].ptsX[k+1]; shapes[i].ptsY[k] = shapes[i].ptsY[k+1];
                                }
                                shapes[i].ptCount--; ClearSelection(); RedrawCanvas(hwnd);
                            }
                            return 0;
                        }
                    }
                }
            }
            break;
        }
        case WM_APP + 1: { 
            char buf[32]; 
            double newDist, dx, dy, minX, maxX, minY, maxY, cx, cy, oldVal, scale; 
            int p, s1, p1, s2, p2;
            
            GetWindowText(hDistEdit, buf, 32); 
            newDist = atof(buf);
            if (newDist > 10000.0) newDist = 10000.0;
            if (newDist < -10000.0) newDist = -10000.0;
            
            if (distEditMode == 0 && selOrderCount == 2) {
                double dist;
                s1 = selOrderS[0]; p1 = selOrderP[0]; s2 = selOrderS[1]; p2 = selOrderP[1];
                
                if (s1 >= 0 && s1 < MAX_SHAPES && s2 >= 0 && s2 < MAX_SHAPES &&
                    p1 >= 0 && p1 < MAX_POINTS && p2 >= 0 && p2 < MAX_POINTS) {
                    
                    dx = shapes[s2].ptsX[p2] - shapes[s1].ptsX[p1]; 
                    dy = shapes[s2].ptsY[p2] - shapes[s1].ptsY[p1];
                    dist = sqrt(dx*dx + dy*dy);
                    
                    if (dist > 0.0) { 
                        SaveState(); 
                        shapes[s2].ptsX[p2] = shapes[s1].ptsX[p1] + dx * (newDist / dist); 
                        shapes[s2].ptsY[p2] = shapes[s1].ptsY[p1] + dy * (newDist / dist);
                    }
                }
            } else if ((distEditMode == 1 || distEditMode == 2) && selectedShape >= 0 && selectedShape < MAX_SHAPES) {
                minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                for(p=0; p<shapes[selectedShape].ptCount; p++) { 
                    minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]); 
                    minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]); 
                }
                cx = (minX + maxX)/2.0; cy = (minY + maxY)/2.0; oldVal = (distEditMode == 1) ? (maxX - minX) : (maxY - minY);
                
                if (oldVal > 0.0) {
                    scale = newDist / oldVal; SaveState();
                    for(p=0; p<shapes[selectedShape].ptCount; p++) {
                        if (distEditMode == 1) shapes[selectedShape].ptsX[p] = cx + (shapes[selectedShape].ptsX[p] - cx) * scale;
                        else shapes[selectedShape].ptsY[p] = cy + (shapes[selectedShape].ptsY[p] - cy) * scale;
                    }
                }
            }
            ShowWindow(hDistEdit, SW_HIDE); RedrawCanvas(hwnd); SetFocus(hwnd); 
            break;
        }
        case WM_COMMAND: {
            UINT id = wParam; int btnId = id - 200, i, j, k;

            if (btnId >= 0 && btnId < 28) {
                if (btnId >= 0 && btnId <= 6) { currentMode = btnId; isDrawing = 0; isDraggingNodes = 0; ClearSelection(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, TRUE); }
                else if (btnId == 7) { currentMode = 7; textCursorActive = 0; UpdateStatusBar(); }
                else if (btnId == 8) { currentMode = 8; UpdateStatusBar(); }
                else if (btnId == 9) Undo(hwnd);
                else if (btnId == 10) { SaveState(); shapeCount = 0; selectedShape = -1; ClearSelection(); UpdateStatusBar(); RedrawCanvas(hwnd); }
                else if (btnId == 11) SendMessage(hwnd, WM_KEYDOWN, VK_DELETE, 0);
                else if (btnId == 12) { 
                    OPENFILENAME ofn; char szFile[260]; memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; 
                    ofn.lpstrFilter = "SVG Files (*.svg)\0*.svg\0"; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); 
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) ParseSVG(szFile, hwnd); 
                }
                else if (btnId == 13) { 
                    OPENFILENAME ofn; char szFile[260]; memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; 
                    ofn.lpstrFilter = "Image Files (*.bmp)\0*.bmp\0"; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); 
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) LoadReferenceImage(szFile, hwnd); 
                }
                else if (btnId == 14) { 
                    OPENFILENAME ofn; char szFile[260]; memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; 
                    ofn.lpstrFilter = "C Data Files (*.c)\0*.c\0"; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); 
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) LoadCFile(szFile, hwnd); 
                }
                else if (btnId == 15) DoSaveFile(hwnd);
                else if (btnId == 16) ExportCode(hwnd);
                else if (btnId == 17) { 
                    int selShapes[MAX_SHAPES]; int numSelShapes = 0;
                    for (i = 0; i < shapeCount; i++) {
                        int hasSel = 0;
                        for (j = 0; j < shapes[i].ptCount; j++) if (ptSelected[i][j]) hasSel = 1;
                        if (hasSel) selShapes[numSelShapes++] = i;
                    }
                    
                    if (numSelShapes == 2 && shapes[selShapes[0]].type == 0 && shapes[selShapes[1]].type == 0) {
                        int s1 = selShapes[0], s2 = selShapes[1], edgeCount = 0, fCount = 0, loopsFound = 0, loopStartIdx, firstEdge, found, cleanCnt;
                        double cx, cy, clnX[MAX_POINTS], clnY[MAX_POINTS];
                        Edge* pool = (Edge*)malloc(256 * sizeof(Edge));
                        Edge* filtered = (Edge*)malloc(256 * sizeof(Edge));
                        int* keep = (int*)malloc(256 * sizeof(int));
                        int* used = (int*)malloc(256 * sizeof(int));
                        Shape merged;
                        
                        if (pool && filtered && keep && used) {
                            SaveState(); 
                            AddEdgesFromShape(&shapes[s1], &shapes[s2], pool, &edgeCount); 
                            AddEdgesFromShape(&shapes[s2], &shapes[s1], pool, &edgeCount);
                            
                            for(i=0; i<edgeCount; i++) keep[i] = 1;
                            for(i=0; i<edgeCount; i++) {
                                if (!keep[i]) continue;
                                for(j=i+1; j<edgeCount; j++) {
                                    if (!keep[j]) continue;
                                    if ((fabs(pool[i].x1 - pool[j].x1) < 1e-5 && fabs(pool[i].y1 - pool[j].y1) < 1e-5 && fabs(pool[i].x2 - pool[j].x2) < 1e-5 && fabs(pool[i].y2 - pool[j].y2) < 1e-5) ||
                                        (fabs(pool[i].x1 - pool[j].x2) < 1e-5 && fabs(pool[i].y1 - pool[j].y2) < 1e-5 && fabs(pool[i].x2 - pool[j].x1) < 1e-5 && fabs(pool[i].y2 - pool[j].y1) < 1e-5)) {
                                        keep[i] = 0; keep[j] = 0; break;
                                    }
                                }
                            }
                            
                            for(i=0; i<edgeCount; i++) if (keep[i]) filtered[fCount++] = pool[i];
                            
                            if (fCount > 0) {
                                merged = shapes[s1]; merged.ptCount = 0; memset(used, 0, 256 * sizeof(int));
                                
                                while(1) {
                                    firstEdge = -1; for(j=0; j<fCount; j++) if(!used[j]) { firstEdge = j; break; }
                                    if (firstEdge == -1) break;
                                    
                                    cx = filtered[firstEdge].x1; cy = filtered[firstEdge].y1; loopStartIdx = merged.ptCount;
                                    
                                    while(1) {
                                        found = -1; if (merged.ptCount >= MAX_POINTS - 2) break;
                                        merged.ptsX[merged.ptCount] = cx; merged.ptsY[merged.ptCount] = cy; merged.ptCount++;
                                        
                                        for (j=0; j<fCount; j++) {
                                            if (!used[j]) {
                                                if (fabs(filtered[j].x1 - cx) < 1e-5 && fabs(filtered[j].y1 - cy) < 1e-5) { found = j; cx = filtered[j].x2; cy = filtered[j].y2; break; }
                                                if (fabs(filtered[j].x2 - cx) < 1e-5 && fabs(filtered[j].y2 - cy) < 1e-5) { found = j; cx = filtered[j].x1; cy = filtered[j].y1; break; }
                                            }
                                        }
                                        if (found != -1) used[found] = 1; else break;
                                    }
                                    if (loopsFound > 0 && merged.ptCount < MAX_POINTS - 2) {
                                        merged.ptsX[merged.ptCount] = merged.ptsX[loopStartIdx]; merged.ptsY[merged.ptCount] = merged.ptsY[loopStartIdx]; merged.ptCount++;
                                        merged.ptsX[merged.ptCount] = merged.ptsX[loopStartIdx - 1]; merged.ptsY[merged.ptCount] = merged.ptsY[loopStartIdx - 1]; merged.ptCount++;
                                    }
                                    loopsFound++;
                                }
                                
                                cleanCnt = 0; 
                                for (i=0; i<merged.ptCount; i++) { 
                                    if (cleanCnt == 0 || pow(merged.ptsX[i]-clnX[cleanCnt-1], 2) + pow(merged.ptsY[i]-clnY[cleanCnt-1], 2) > 1e-5) { 
                                        clnX[cleanCnt] = merged.ptsX[i]; clnY[cleanCnt++] = merged.ptsY[i]; 
                                    } 
                                }
                                if (cleanCnt > 1 && pow(clnX[0]-clnX[cleanCnt-1], 2) + pow(clnY[0]-clnY[cleanCnt-1], 2) < 1e-5) cleanCnt--;
                                merged.ptCount = cleanCnt; 
                                for(i=0; i<cleanCnt; i++) { merged.ptsX[i]=clnX[i]; merged.ptsY[i]=clnY[i]; }
                                
                                shapes[s1] = merged;
                                if (s2 > s1) { for(k=s2; k<shapeCount-1; k++) shapes[k] = shapes[k+1]; shapeCount--; }
                                else { for(k=s1; k<shapeCount-1; k++) shapes[k] = shapes[k+1]; shapeCount--; s1--; }
                                
                                ClearSelection(); selectedShape = s1; for(i=0; i<shapes[s1].ptCount; i++) ToggleSelection(s1, i);
                            }
                        }
                        if (pool) free(pool); if (filtered) free(filtered); if (keep) free(keep); if (used) free(used);
                        UpdateStatusBar(); RedrawCanvas(hwnd);
                    }
                }
                else if (btnId == 18) { 
                    if (selectedShape != -1 && selectedShape < shapeCount - 1) { 
                        Shape temp; SaveState(); 
                        temp = shapes[selectedShape]; shapes[selectedShape] = shapes[selectedShape + 1]; shapes[selectedShape + 1] = temp; selectedShape++; 
                        RedrawCanvas(hwnd); 
                    } 
                }
                else if (btnId == 19) { 
                    if (selectedShape > 0) { 
                        Shape temp; SaveState(); 
                        temp = shapes[selectedShape]; shapes[selectedShape] = shapes[selectedShape - 1]; shapes[selectedShape - 1] = temp; selectedShape--; 
                        RedrawCanvas(hwnd); 
                    } 
                }
                else if (btnId == 20) { if (selOrderCount == 2) { SaveState(); shapes[selOrderS[1]].ptsX[selOrderP[1]] = shapes[selOrderS[0]].ptsX[selOrderP[0]]; RedrawCanvas(hwnd); } }
                else if (btnId == 21) { if (selOrderCount == 2) { SaveState(); shapes[selOrderS[1]].ptsY[selOrderP[1]] = shapes[selOrderS[0]].ptsY[selOrderP[0]]; RedrawCanvas(hwnd); } }
                else if (btnId == 22 || btnId == 23 || btnId == 24) { 
                    if (btnId == 22 && selOrderCount == 2) {
                        int s1 = selOrderS[0], p1 = selOrderP[0], s2 = selOrderS[1], p2 = selOrderP[1];
                        double dx = shapes[s2].ptsX[p2] - shapes[s1].ptsX[p1], dy = shapes[s2].ptsY[p2] - shapes[s1].ptsY[p1];
                        int px = (int)round(shapes[s2].ptsX[p2] * scaleFactor) + 10, py = (int)round(shapes[s2].ptsY[p2] * scaleFactor) + 10;
                        char buf[32]; distEditMode = 0; sprintf(buf, "%.2f", sqrt(dx*dx + dy*dy));
                        SetWindowText(hDistEdit, buf); MoveWindow(hDistEdit, px, py, 60, 20, TRUE); ShowWindow(hDistEdit, SW_SHOW); SetFocus(hDistEdit);
                    } else if (selectedShape != -1 && shapes[selectedShape].ptCount > 0) {
                        double minX = 9999, maxX = -9999, minY = 9999, maxY = -9999, val; int p; char buf[32];
                        for(p=0; p<shapes[selectedShape].ptCount; p++) { 
                            minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]); 
                            minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]); 
                        }
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
                    if (parsedCount < MAX_ICONS) {
                        SyncCurrentIcon();
                        parsedIcons[parsedCount].caseId = (parsedCount > 0) ? parsedIcons[parsedCount-1].caseId + 1 : 1;
                        parsedIcons[parsedCount].shapeCount = 0;
                        parsedIcons[parsedCount].shapes = NULL;
                        parsedCount++;
                        SetScrollRange(hScrlIcon, SB_CTL, 0, parsedCount - 1, TRUE);
                        SwitchToIcon(parsedCount - 1);
                    }
                }
                else if (btnId == 27) {
                    if (parsedCount > 0 && currentIconIdx >= 0) {
                        if (parsedIcons[currentIconIdx].shapes) free(parsedIcons[currentIconIdx].shapes);
                        for(i = currentIconIdx; i < parsedCount - 1; i++) {
                            parsedIcons[i] = parsedIcons[i+1];
                        }
                        parsedCount--;
                        parsedIcons[parsedCount].shapes = NULL;
                        SetScrollRange(hScrlIcon, SB_CTL, 0, parsedCount > 0 ? parsedCount - 1 : 0, TRUE);
                        
                        if (parsedCount == 0) {
                            shapeCount = 0; currentIconIdx = -1; currentCaseId = 1; RedrawCanvas(hwnd); UpdateStatusBar();
                        } else {
                            currentIconIdx = -1;
                            SwitchToIcon(0);
                        }
                    }
                }
            }
            if (id != 300 && (id < 222 || id > 224)) SetFocus(hwnd); 
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            if (shapes) {
                int imgBottom = canvasSize + 5;
                DrawGrid(hdc); RenderShapes(hdc, shapes, shapeCount, scaleFactor, 0, 0, &currentShape, isDrawing);
                DrawReferenceImage(hdc);
                if (currentMode == 0 || currentMode == 1 || currentMode == 2) DrawNodes(hdc);
                if (currentMode == 7 && textCursorActive) {
                    HPEN pCur = CreatePen(PS_SOLID, 2, RGB(255,165,0)); 
                    HGDIOBJ old = SelectObject(hdc, pCur);
                    MoveTo(hdc, (int)(textCursorX*scaleFactor), (int)(textCursorY*scaleFactor)); 
                    LineTo(hdc, (int)(textCursorX*scaleFactor), (int)((textCursorY+5)*scaleFactor));
                    SelectObject(hdc, old); DeleteObject(pCur);
                }
                DrawPalette(hdc); DrawPreview(hdc); 
                
                SetBkMode(hdc, TRANSPARENT);
                TextOut(hdc, 5, imgBottom - 2, "Icon", 4);
                TextOut(hdc, 5, imgBottom + 16, "Alph", 4);
                TextOut(hdc, 5 + canvasSize/2, imgBottom + 16, "Zoom", 4);
                TextOut(hdc, 5, imgBottom + 34, "PanX", 4);
                TextOut(hdc, 5 + canvasSize/2, imgBottom + 34, "PanY", 4);
                TextOut(hdc, 5, imgBottom + 52, "StrX", 4);
                TextOut(hdc, 5 + canvasSize/2, imgBottom + 52, "StrY", 4);
            }
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_DESTROY: {
            int i;
            if (shapes) free(shapes);
            if (dragStartSnapshot) free(dragStartSnapshot);
            if (history) {
                for(i=0; i<MAX_UNDO; i++) if (history[i]) free(history[i]);
                free(history);
            }
            for(i=0; i<MAX_ICONS; i++) if (parsedIcons[i].shapes) { free(parsedIcons[i].shapes); parsedIcons[i].shapes = NULL; }
            if (hRefBmp) DeleteObject(hRefBmp);
            if (subclassThunk) FreeProcInstance(subclassThunk);
            PostQuitMessage(0); 
            break;
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
    hMain = CreateWindow("IconEditClass", "Win16 C Pro Vector Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 680, 560, NULL, NULL, hInstance, NULL);
    if (!hMain) return FALSE; 
    ShowWindow(hMain, nCmdShow); UpdateWindow(hMain);
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return msg.wParam;
}