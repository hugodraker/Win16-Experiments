/* ============================================================================
 * Vector Icon Editor for Win16 - Saves icons as GDI Polygons
 * ============================================================================
 * OPENWATCOM WIN16 C PORT (Windows 3.1x / 16-bit Target)
 * wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s icoedtv.c commdlg.lib shell.lib
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
#include <direct.h>
#include <shellapi.h>
#include <stdarg.h>

void WriteLog(const char* fmt, ...) {
return;
    FILE* f = fopen("debug.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }
}

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
#define MAX_ICONS 20
#define PANEL_WIDTH 340
#define MAX_TAGS 8
#define MAX_DIMS 32
#define MAX_REFS 10
#define MAX_EDIT_TAGS 64

/* C89 Math & Color Macros */
#define fmax(a,b) (((a)>(b))?(a):(b))
#define fmin(a,b) (((a)<(b))?(a):(b))
#define round(x) ((double)((long)((x) + ((x)>=0 ? 0.5 : -0.5))))

typedef struct { 
    int type; 
    double ptsX[MAX_POINTS]; 
    double ptsY[MAX_POINTS]; 
    int ptCount; 
    COLORREF fill; 
    COLORREF stroke; 
    int useFill; 
    int useStroke;
    int strokeWidth;
    int fontSize;   
    char text[128]; 
    char tagData[128]; 
} Shape;

typedef struct {
    int isRef;
    int shapeIdx;
    int tagIdx;
    char label[64];
    char value[128];
} TagEditMap;

typedef struct { double x1, y1, x2, y2; } Edge;

typedef struct {
    int s1, p1, s2, p2; 
    double offset; 
    double textPos; 
    int mode; 
} Dimension;

typedef struct { 
    int caseId; 
    char name[64]; 
    Shape* shapes; 
    int shapeCount; 
    char tNames[MAX_TAGS][32];  
    char tVals[MAX_TAGS][128];  
    int tCount;                 
    Dimension dims[MAX_DIMS];
    int dimCount;
    char pageName[64];
    char unitName[32];
    double pageScale;
    char iconTitle[64];
    int gridW;
    int gridH;
} IconDef;

char activePageName[64] = "A4 (210x297)";
char activeUnitName[32] = "mm";
double activePageScale = 1.0;

char customPageSizes[10][64] = {0};
char activePageOrient[32] = "Landscape";

int customPageCount = 0;
int g_RenderRefDepth = 0;
int previewDirty = 1;

Shape* g_tempRef = NULL;
POINT* g_subPts = NULL;
POINT* g_pA = NULL;

POINT g_boxPts[5][5];
char g_refPath[5][260];
char g_absPath[5][260];
char g_subPath[5][260];
char g_tagBuf[5][128];
char g_instVal[5][128];

typedef struct {
    char path[260];
    Shape* shapes;
    int shapeCount;
    double minX, minY, maxX, maxY;
    char tNames[MAX_TAGS][32];  /* Restored */
    char tVals[MAX_TAGS][128];  /* Restored */
    int tCount;                 /* Restored */
    Dimension dims[MAX_DIMS]; 
    int dimCount;             
} RefCache;

typedef struct { 
    int shapeIdx; 
    int isRef; 
    int tagIdx; 
    RECT box; 
} TextHitBox;

Shape* shapes = NULL; 
Shape* dragStartSnapshot = NULL;
Shape* history[MAX_UNDO]; 

RefCache* refCache = NULL;
int refCacheCount = 0;
int g_RenderRefIdx = -1;

IconDef* parsedIcons = NULL;
int parsedCount = 0, currentIconIdx = -1, currentCaseId = 1;
char loadedCFile[260] = "";

TagEditMap* editMap = NULL;
int editMapCount = 0;

TextHitBox textHits[128];
int textHitCount = 0;

Dimension dims[MAX_DIMS];
int dimCount = 0;
int dragDimIdx = -1;
int editDimIdx = -1; /* For inline editing */
char pendingRefFile[260] = "";

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
void RedrawCanvas(HWND hwnd);
void SaveState(void);
void UpdateStatusBar(void);
void ShowStatus(const char* msg);
void ClearSelection(void);
void ToggleSelection(int s, int p);

void EscapeCString(const char* in, char* out, int maxLen);
void UnescapeCString(const char* in, char* out, int maxLen);
void SetPipeValue(char* tagData, int idx, const char* newVal);
void GetPipeValue(const char* pipeStr, int idx, char* out, int maxLen);
char* FindQuote(char* start);

void WriteShapeToC(FILE* f, Shape* s, int j);
void SilentLoadSVG(const char* path, RefCache* ref);
void SilentLoadC(const char* path, RefCache* ref);
int EnsureRefLoaded(const char* path);
int IsRefFile(const char* path);
void ResolvePath(const char* base, const char* rel, char* out);
void CommitCurrentIcon(void);
void SwitchToIcon(int idx);
void InitFont(void);
void GenShape(double eX, double eY);
int PointInPolyShape(double px, double py, Shape* s);

int shapeCount = 0;
int historyShapeCount[MAX_UNDO]; 
int undoIndex = -1;
Shape currentShape;
int isDrawing = 0, currentMode = 3; 
int selectedShape = -1;

int hoverShape = -1, hoverPt = -1, hoverSegShape = -1, hoverSegPt = -1;
double hoverProjX = 0, hoverProjY = 0;
int ptSelected[MAX_SHAPES][MAX_POINTS];
int selOrderS[MAX_POINTS], selOrderP[MAX_POINTS], selOrderCount = 0;

int startX = 0, startY = 0;
double currentEndX = 0, currentEndY = 0;
int snapToGrid = 1;
int currentStrokeWidth = 1; /* NEW Global */

char tagNames[MAX_TAGS][32];
char tagValues[MAX_TAGS][128];
int tagCount = 0;

HWND hPageSizeDlg = NULL;
HWND hEditW = NULL, hEditH = NULL;
HWND hMultiEdit = NULL;
FARPROC oldMultiEditProc = NULL;
FARPROC multiSubclassThunk = NULL;

int editShapeIdx = -1;
int editTagIdx = -1;
int gridW = 32, gridH = 32;
int canvasW_px = 320, canvasH_px = 320;
double extScale = 1.0;

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
HWND hMain, hStatus;
HWND hBtn[33];
HWND hBtnThickPlus, hBtnThickMinus; /* NEW Buttons */
HWND hScrlSides, hScrlDepth, hScrlIcon;
HWND hBtnAddIcon, hBtnDelIcon;
HWND hDistEdit = NULL; 
FARPROC oldEditProc = NULL;
FARPROC subclassThunk = NULL;
int distEditMode = 0;

int paramSides = 4, paramStar = 100;
int canvasSize = 320, scaleFactor = 10, clientW = 0, clientH = 0;

const char* const bT[33] = {
    "Select/Edit", "Rotate", "Scale", "Polygon", "Line",
    "Polyline", "Revert", "Pan", "Flood Fill", "Undo",
    "Clear", "Delete", "Import SVG", "Import Ref", "Open .C",
    "Save .C", "P<->L", "Merge", "Move Up", "Move Down",
    "Align Vert", "Align Horz", "Set Dist", "Set Width", "Set Height", 
    "Duplicate", "Set Angle", "Dimension", "Page Size", "Tag Editor", 
    "Add Tag", "(Lock Axis)", "Show All"
};
int lockAxis = 0;

HWND hScrlZoom;
double viewZoom = 1.0;
double viewPanX = 0.0;
double viewPanY = 0.0;
int panStartX = 0, panStartY = 0;

char pendingSvgFile[260] = "";

/* --- Utilities & Initialization --- */
double CLAMP(double v, double minv, double maxv) {
    if (v < minv) return minv;
    if (v > maxv) return maxv;
    return v;
}
void DoSaveRef(HWND hwnd) {
    static OPENFILENAME ofn; static char szFile[260]; FILE* f; int j; 
    
    memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "C Data Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST; ofn.lpstrDefExt = "c";
    
    if (GetSaveFileName(&ofn)) {
        int idx = currentIconIdx >= 0 ? currentIconIdx : 0;
        Shape* iconShapes = shapes; 
        int iconShapeCount = shapeCount; 
        
        if (IsRefFile(szFile)) { MessageBox(hwnd, "Cannot overwrite a source reference file!", "Error", MB_ICONHAND); return; }
        CommitCurrentIcon();
        if (idx != currentIconIdx) { iconShapes = parsedIcons[idx].shapes; iconShapeCount = parsedIcons[idx].shapeCount; }

        f = fopen(szFile, "w");
        if (f) {
            fprintf(f, "case 1: {\n");
            if (iconShapes) {
                for (j = 0; j < iconShapeCount; j++) {
                    if (iconShapes[j].type == 3) continue; /* Skip nested refs */
                    WriteShapeToC(f, &iconShapes[j], j);
                }
            }
            fprintf(f, "    break;\n}\n");
            fclose(f); ShowStatus(" Reference saved successfully.");
        }
    }
}
void EscapeCString(const char* in, char* out, int maxLen) {
    int len = 0;
    if (!in || !out) return;
    while (*in && len < maxLen - 2) {
        if (*in == '\r' && *(in+1) == '\n') { *out++ = '\\'; *out++ = 'n'; in += 2; len += 2; } 
        else if (*in == '\n') { *out++ = '\\'; *out++ = 'n'; in++; len += 2; } 
        else if (*in == '"') { *out++ = '\\'; *out++ = '"'; in++; len += 2; } 
        else if (*in == '\\') { *out++ = '\\'; *out++ = '\\'; in++; len += 2; } 
        else { *out++ = *in++; len++; }
    }
    *out = '\0';
}

#pragma code_seg ( "IO_TEXT" );
int EnsureRefLoaded(const char* path) {
    int i, j;
    double minX = 99999.0, minY = 99999.0;
    double maxX = -99999.0, maxY = -99999.0;

    if (!path || !path[0]) return -1;

    for (i = 0; i < refCacheCount; i++) {
        if (stricmp(refCache[i].path, path) == 0) return i;
    }
    if (refCacheCount >= MAX_REFS) return -1;

    if (!refCache[refCacheCount].shapes) {
        refCache[refCacheCount].shapes = (Shape*)GlobalAllocPtr(GHND, MAX_SHAPES * sizeof(Shape));
        if (!refCache[refCacheCount].shapes) return -1;
    }

    strncpy(refCache[refCacheCount].path, path, 259);
    refCache[refCacheCount].path[259] = '\0';
    refCache[refCacheCount].shapeCount = 0;
    refCache[refCacheCount].tCount = 0;

    if (strstr(path, ".svg") || strstr(path, ".SVG")) {
        SilentLoadSVG(path, &refCache[refCacheCount]);
    } else {
        SilentLoadC(path, &refCache[refCacheCount]);
    }

    for (i = 0; i < refCache[refCacheCount].shapeCount; i++) {
        Shape* sh = &refCache[refCacheCount].shapes[i];
        for (j = 0; j < sh->ptCount; j++) {
            if (sh->ptsX[j] < minX) minX = sh->ptsX[j];
            if (sh->ptsX[j] > maxX) maxX = sh->ptsX[j];
            if (sh->ptsY[j] < minY) minY = sh->ptsY[j];
            if (sh->ptsY[j] > maxY) maxY = sh->ptsY[j];
        }
    }

    if (minX > maxX) { minX = 0; minY = 0; maxX = GRID_SIZE; maxY = GRID_SIZE; }

    refCache[refCacheCount].minX = minX;
    refCache[refCacheCount].minY = minY;
    refCache[refCacheCount].maxX = maxX;
    refCache[refCacheCount].maxY = maxY;

    return refCacheCount++;
}
void EscapeNewlines(const char* in, char* out) {
    int outLen = 0;
    if (!in || !out) return;
    while (*in && outLen < 500) {
        if (*in == '\r' && *(in+1) == '\n') { *out++ = '\\'; *out++ = 'n'; in += 2; outLen += 2; }
        else if (*in == '\n') { *out++ = '\\'; *out++ = 'n'; in++; outLen += 2; }
        else { *out++ = *in++; outLen++; }
    }
    *out = '\0';
}
void FormatDimension(double val, const char* unit, char* outBuf) {
    if (stricmp(unit, "feet-inches") == 0) {
        int feet = (int)(val / 12.0);
        double inches = val - (feet * 12.0);
        if (fabs(inches - round(inches)) < 0.001) {
            sprintf(outBuf, "%d'-%d\"", feet, (int)round(inches));
        } else if (fabs(inches * 10.0 - round(inches * 10.0)) < 0.001) {
            sprintf(outBuf, "%d'-%.1f\"", feet, inches);
        } else {
            sprintf(outBuf, "%d'-%.2f\"", feet, inches);
        }
    } else if (stricmp(unit, "inches") == 0) {
        if (fabs(val - round(val)) < 0.001) sprintf(outBuf, "%d\"", (int)round(val));
        else sprintf(outBuf, "%.2f\"", val);
    } else if (stricmp(unit, "None") == 0 || unit[0] == '\0') {
        if (fabs(val - round(val)) < 0.001) sprintf(outBuf, "%d", (int)round(val));
        else sprintf(outBuf, "%.2f", val);
    } else {
        if (fabs(val - round(val)) < 0.001) sprintf(outBuf, "%d %s", (int)round(val), unit);
        else sprintf(outBuf, "%.2f %s", val, unit);
    }
}
char* FindQuote(char* start) {
    char* p = start;
    while (*p) {
        if (*p == '\\' && *(p+1) == '"') { p += 2; continue; }
        if (*p == '"') return p;
        p++;
    }
    return NULL;
}
void GetResolveBase(char* base) {
    if (loadedCFile[0]) {
        strcpy(base, loadedCFile);
    } else {
        if (getcwd(base, 260)) strcat(base, "\\dummy.c");
        else strcpy(base, "");
    }
}
void GetPipeValue(const char* pipeStr, int idx, char* out, int maxLen) {
    int curr = 0;
    const char* start = pipeStr;
    const char* p = pipeStr;
    out[0] = '\0';
    if (!pipeStr) return;
    while (*p) {
        if (*p == '|') {
            if (curr == idx) break;
            curr++;
            start = p + 1;
        }
        p++;
    }
    if (curr == idx) {
        int len = p - start;
        if (len > maxLen - 1) len = maxLen - 1;
        strncpy(out, start, len);
        out[len] = '\0';
    }
}
void GetRelativePath(const char* base, const char* target, char* out) {
    int i = 0, j = 0, bCount = 0, tCount = 0;
    static char bDirs[20][64]; static char tDirs[20][64];
    char temp[128], bDrive, tDrive; const char *bPtr, *tPtr;

    if (!base || !target || !base[0] || !target[0]) { strcpy(out, target); return; }
    bDrive = toupper(base[0]); tDrive = toupper(target[0]);
    if (bDrive != tDrive || base[1] != ':' || target[1] != ':') {
        MessageBox(hMain, "Files must be on the same drive. Using absolute path.", "Drive Mismatch", MB_ICONEXCLAMATION);
        strcpy(out, target); return; 
    }
    
    bPtr = base + 3;
    while (*bPtr) {
        int k = 0;
        while (*bPtr && *bPtr != '\\' && *bPtr != '/') temp[k++] = *bPtr++;
        temp[k] = '\0';
        if (k > 0 && bCount < 20) strcpy(bDirs[bCount++], temp);
        if (*bPtr) bPtr++;
    }
    if (bCount > 0) bCount--; 
    
    tPtr = target + 3;
    while (*tPtr) {
        int k = 0;
        while (*tPtr && *tPtr != '\\' && *tPtr != '/') temp[k++] = *tPtr++;
        temp[k] = '\0';
        if (k > 0 && tCount < 20) strcpy(tDirs[tCount++], temp);
        if (*tPtr) tPtr++;
    }
    
    while (i < bCount && i < tCount && stricmp(bDirs[i], tDirs[i]) == 0) i++;
    
    out[0] = '\0';
    for (j = i; j < bCount; j++) strcat(out, "..\\");
    for (j = i; j < tCount; j++) {
        strcat(out, tDirs[j]);
        if (j < tCount - 1) strcat(out, "\\");
    }
}
int IsRefFile(const char* path) {
    int i;
    for (i = 0; i < refCacheCount; i++) {
        if (stricmp(refCache[i].path, path) == 0) return 1;
    }
    return 0;
}
LRESULT CALLBACK _export MultiEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); SetFocus(hMain); return 0; }
    if (msg == WM_KILLFOCUS) {
        char buf[512], unesc[512]; 
        GetWindowText(hwnd, buf, 512);
        UnescapeCString(buf, unesc, 512);
        if (editShapeIdx != -1) {
            SaveState();
            if (editTagIdx == -1) {
                strncpy(shapes[editShapeIdx].text, unesc, 127);
                shapes[editShapeIdx].text[127] = '\0';
            } else {
                SetPipeValue(shapes[editShapeIdx].tagData, editTagIdx, unesc);
            }
        }
        ShowWindow(hwnd, SW_HIDE); editShapeIdx = -1; RedrawCanvas(hMain);
    }
    return CallWindowProc((FARPROC)oldMultiEditProc, hwnd, msg, wParam, lParam);
}
void ProcessDropFile(const char* filePath, HWND hwnd) {
    if (filePath && strstr(filePath, ".c") && !loadedCFile[0]) {
        LoadCFile(filePath, hwnd);
        ShowStatus(" File opened via drag-drop.");
    } else if (filePath && strstr(filePath, ".c")) {
        ShowRefListDialog(filePath, hwnd);
    } else if (filePath && strstr(filePath, ".svg")) {
        char relPath[260];
        if (loadedCFile[0]) {
            GetRelativePath(loadedCFile, filePath, relPath);
        } else {
            strcpy(relPath, filePath);
        }
        if (shapeCount < MAX_SHAPES) {
            SaveState();
            memset(&shapes[shapeCount], 0, sizeof(Shape));
            shapes[shapeCount].type = 3; 
            shapes[shapeCount].ptCount = 1; 
            shapes[shapeCount].stroke = currentStroke;
            sprintf(shapes[shapeCount].text, "{{EXT_REF=%s scale=1.00 rot=0.00}}", relPath);
            shapeCount++;
            RedrawCanvas(hwnd);
            ShowStatus(" SVG Reference added.");
        }
    }
}
LRESULT CALLBACK _export PageSizeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hComboPage = NULL, hComboUnits = NULL, hComboScale = NULL, hComboOrient = NULL;
    static HWND hEditTitle = NULL;
    static int inOrientSync = 0;
    switch(msg) {
        case WM_CREATE: {
            char buf[32]; int i;
            double pW = 0, pH = 0;
            char *p1, *p2;

            CreateWindow("STATIC", "Title:", WS_CHILD|WS_VISIBLE, 10, 10, 120, 20, hwnd, NULL, hInst, NULL);
            hEditTitle = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 130, 10, 150, 20, hwnd, (HMENU)108, hInst, NULL);

            CreateWindow("STATIC", "Width (Grid Units):", WS_CHILD|WS_VISIBLE, 10, 35, 120, 20, hwnd, NULL, hInst, NULL);
            hEditW = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 130, 35, 150, 20, hwnd, (HMENU)105, hInst, NULL);
            CreateWindow("STATIC", "Height (Grid Units):", WS_CHILD|WS_VISIBLE, 10, 60, 120, 20, hwnd, NULL, hInst, NULL);
            hEditH = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 130, 60, 150, 20, hwnd, (HMENU)106, hInst, NULL);
            
            CreateWindow("STATIC", "Page Size:", WS_CHILD|WS_VISIBLE, 10, 85, 120, 20, hwnd, NULL, hInst, NULL);
            hComboPage = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWN|WS_VSCROLL, 130, 85, 150, 120, hwnd, (HMENU)101, hInst, NULL);
            {
                const char* sizes[] = {"A4 (210x297)", "Letter (215.9x279.4)", "A3 (297x420)", "A2 (420x594)", "A1 (594x841)", "A0 (841x1189)", "A5 (148x210)", "Legal (215.9x355.6)", "Tabloid (279.4x431.8)", "Arch D (609.6x914.4)", "Custom (100x100)"};
                for(i=0; i<11; i++) SendMessage(hComboPage, CB_ADDSTRING, 0, (LPARAM)sizes[i]);
            }
            for(i = 0; i < customPageCount; i++) SendMessage(hComboPage, CB_ADDSTRING, 0, (LPARAM)customPageSizes[i]);
            SetWindowText(hComboPage, activePageName);

            p1 = strchr(activePageName, '(');
            if (p1) {
                p2 = strchr(p1, 'x');
                if (p2) { pW = atof(p1 + 1); pH = atof(p2 + 1); }
            }
            if (pW > 0 && pH > 0) {
                if (pW >= pH) strcpy(activePageOrient, "Landscape");
                else strcpy(activePageOrient, "Portrait");
            } else if (activePageOrient[0] == '\0') {
                strcpy(activePageOrient, "Landscape");
            }

            CreateWindow("STATIC", "Orientation:", WS_CHILD|WS_VISIBLE, 10, 110, 120, 20, hwnd, NULL, hInst, NULL);
            hComboOrient = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL, 130, 110, 150, 60, hwnd, (HMENU)107, hInst, NULL);
            SendMessage(hComboOrient, CB_ADDSTRING, 0, (LPARAM)"Landscape");
            SendMessage(hComboOrient, CB_ADDSTRING, 0, (LPARAM)"Portrait");
            if (strcmp(activePageOrient, "Portrait") == 0) {
                SendMessage(hComboOrient, CB_SETCURSEL, 1, 0);
            } else {
                SendMessage(hComboOrient, CB_SETCURSEL, 0, 0);
            }
            SetWindowText(hComboOrient, activePageOrient);

            CreateWindow("STATIC", "Units:", WS_CHILD|WS_VISIBLE, 10, 135, 120, 20, hwnd, NULL, hInst, NULL);
            hComboUnits = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWN|WS_VSCROLL, 130, 135, 150, 120, hwnd, (HMENU)102, hInst, NULL);
            SendMessage(hComboUnits, CB_ADDSTRING, 0, (LPARAM)"None");
            SendMessage(hComboUnits, CB_ADDSTRING, 0, (LPARAM)"mm");
            SendMessage(hComboUnits, CB_ADDSTRING, 0, (LPARAM)"cm");
            SendMessage(hComboUnits, CB_ADDSTRING, 0, (LPARAM)"m");
            SendMessage(hComboUnits, CB_ADDSTRING, 0, (LPARAM)"inches");
            SendMessage(hComboUnits, CB_ADDSTRING, 0, (LPARAM)"feet");
            SendMessage(hComboUnits, CB_ADDSTRING, 0, (LPARAM)"feet-inches");
            SetWindowText(hComboUnits, activeUnitName);

            CreateWindow("STATIC", "Scale Ratio:", WS_CHILD|WS_VISIBLE|0x0100L, 10, 160, 120, 20, hwnd, (HMENU)104, hInst, NULL);
            hComboScale = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWN|WS_VSCROLL, 130, 160, 150, 120, hwnd, (HMENU)103, hInst, NULL);
            {
                const char* scales[] = {"1:1", "1:2", "1:4", "1:5", "1:10", "1:20", "1:25", "1:40", "1:50", "1:100", "1:200", "1:500", "1:1000"};
                for(i=0; i<13; i++) SendMessage(hComboScale, CB_ADDSTRING, 0, (LPARAM)scales[i]);
            }
            if (activePageScale > 0 && activePageScale <= 1.0) {
                sprintf(buf, "1:%g", 1.0 / activePageScale);
            } else {
                sprintf(buf, "%g", activePageScale);
            }
            SetWindowText(hComboScale, buf);

            CreateWindow("BUTTON", "Apply", WS_CHILD|WS_VISIBLE, 100, 195, 80, 25, hwnd, (HMENU)1, hInst, NULL);
            
            sprintf(buf, "%d", gridW); SetWindowText(hEditW, buf);
            sprintf(buf, "%d", gridH); SetWindowText(hEditH, buf);
            
            if (currentIconIdx >= 0 && parsedIcons[currentIconIdx].iconTitle[0]) {
                SetWindowText(hEditTitle, parsedIcons[currentIconIdx].iconTitle);
            } else if (activePageName[0]) {
                SetWindowText(hEditTitle, activePageName);
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {
                char buf[64]; char* colon;
                int i, isNew = 1;
                const char* stdSizes[] = {"A4", "Letter", "A3", "A2", "A1", "A0", "A5", "Legal", "Tabloid", "Arch D", "Custom"};
                
                GetWindowText(hEditW, buf, 64); gridW = atoi(buf);
                GetWindowText(hEditH, buf, 64); gridH = atoi(buf);
                GetWindowText(hComboPage, activePageName, 64);
                GetWindowText(hComboUnits, activeUnitName, 32);
                GetWindowText(hComboOrient, activePageOrient, 32);
                GetWindowText(hComboScale, buf, 64);
                
                char newTitle[64];
                GetWindowText(hEditTitle, newTitle, 64);
                if (strlen(newTitle) == 0) strcpy(newTitle, activePageName);
                
                colon = strchr(buf, ':');
                if (colon) {
                    double v1 = atof(buf);
                    double v2 = atof(colon + 1);
                    if (v2 != 0) activePageScale = v1 / v2;
                } else {
                    activePageScale = atof(buf);
                }
                
                for (i = 0; i < 11; i++) {
                    if (strncmp(activePageName, stdSizes[i], strlen(stdSizes[i])) == 0) isNew = 0;
                }
                for (i = 0; i < customPageCount; i++) if (strcmp(customPageSizes[i], activePageName) == 0) isNew = 0;
                if (isNew && customPageCount < 10) strcpy(customPageSizes[customPageCount++], activePageName);

                if (gridW < 5) gridW = 5; if (gridH < 5) gridH = 5;

                if (currentIconIdx >= 0 && currentIconIdx < parsedCount) {
                    strcpy(parsedIcons[currentIconIdx].pageName, activePageName);
                    strcpy(parsedIcons[currentIconIdx].unitName, activeUnitName);
                    parsedIcons[currentIconIdx].pageScale = activePageScale;
                    parsedIcons[currentIconIdx].gridW = gridW;
                    parsedIcons[currentIconIdx].gridH = gridH;
                    strcpy(parsedIcons[currentIconIdx].iconTitle, newTitle);
                }

                SendMessage(hMain, WM_SIZE, 0, MAKELPARAM(clientW, clientH));
                InvalidateRect(hMain, NULL, TRUE); DestroyWindow(hwnd);
            } else {
                int cmd = LOWORD(wParam);
                int evt = HIWORD(lParam);

                if (inOrientSync) break;

                if (cmd == 107 && evt == CBN_SELCHANGE) {
                    char bufPage[64], bufOrient[32], prefix[32];
                    double pW = 0, pH = 0;
                    char *p1, *p2;

                    int idx = SendMessage(hComboOrient, CB_GETCURSEL, 0, 0);
                    if (idx != CB_ERR) SendMessage(hComboOrient, CB_GETLBTEXT, idx, (LPARAM)bufOrient);
                    else GetWindowText(hComboOrient, bufOrient, 32);

                    GetWindowText(hComboPage, bufPage, 64);
                    p1 = strchr(bufPage, '(');
                    if (p1) {
                        p2 = strchr(p1, 'x');
                        if (p2) {
                            int len = (int)(p1 - bufPage);
                            if (len > 31) len = 31;
                            strncpy(prefix, bufPage, len);
                            prefix[len] = '\0';
                            pW = atof(p1 + 1);
                            pH = atof(p2 + 1);

                            if ((strcmp(bufOrient, "Landscape") == 0 && pW < pH) ||
                                (strcmp(bufOrient, "Portrait") == 0 && pW > pH)) {
                                inOrientSync = 1;
                                sprintf(bufPage, "%s(%g%s%g)", prefix, pH, "x", pW);
                                SetWindowText(hComboPage, bufPage);
                                inOrientSync = 0;
                            }
                        }
                    }
                }

                if (cmd == 101 && (evt == CBN_SELCHANGE || evt == CBN_EDITCHANGE || evt == CBN_KILLFOCUS)) {
                    char bufPage[64];
                    double pW = 0, pH = 0;
                    char *p1, *p2;

                    if (evt == CBN_SELCHANGE) {
                        int idx = SendMessage(hComboPage, CB_GETCURSEL, 0, 0);
                        if (idx != CB_ERR) SendMessage(hComboPage, CB_GETLBTEXT, idx, (LPARAM)bufPage);
                        else GetWindowText(hComboPage, bufPage, 64);
                    } else {
                        GetWindowText(hComboPage, bufPage, 64);
                    }

                    p1 = strchr(bufPage, '(');
                    if (p1) {
                        p2 = strchr(p1, 'x');
                        if (p2) {
                            pW = atof(p1 + 1);
                            pH = atof(p2 + 1);
                            if (pW > 0 && pH > 0) {
                                inOrientSync = 1;
                                if (pW >= pH) {
                                    SetWindowText(hComboOrient, "Landscape");
                                    SendMessage(hComboOrient, CB_SETCURSEL, 0, 0);
                                } else {
                                    SetWindowText(hComboOrient, "Portrait");
                                    SendMessage(hComboOrient, CB_SETCURSEL, 1, 0);
                                }
                                inOrientSync = 0;
                            }
                        }
                    }
                }

                if (cmd == 104 || (cmd == 101 && (evt == CBN_SELCHANGE || evt == CBN_EDITCHANGE || evt == CBN_KILLFOCUS)) || 
                    (cmd == 102 && evt == CBN_SELCHANGE) || (cmd == 107 && evt == CBN_SELCHANGE) || 
                    ((cmd == 105 || cmd == 106) && (evt == EN_CHANGE || evt == EN_KILLFOCUS))) {
                    
                    char bufPage[64], bufUnits[32], bufOrient[32], bufW[32], bufH[32];
                    double pW = 0, pH = 0, exact_scale;
                    char *p1, *p2;
                    int i, gW = gridW, gH = gridH;
                    double std_scales[] = {1.0, 0.5, 0.25, 0.2, 0.1, 0.05, 0.04, 0.025, 0.02, 0.01, 0.005, 0.002, 0.001};
                    const char* std_scale_strs[] = {"1:1", "1:2", "1:4", "1:5", "1:10", "1:20", "1:25", "1:40", "1:50", "1:100", "1:200", "1:500", "1:1000"};
                    int chosen_idx = 12;
                    double unitFactor = 1.0;

                    if (cmd == 101 && evt == CBN_SELCHANGE) {
                        int idx = SendMessage(hComboPage, CB_GETCURSEL, 0, 0);
                        if (idx != CB_ERR) SendMessage(hComboPage, CB_GETLBTEXT, idx, (LPARAM)bufPage);
                        else GetWindowText(hComboPage, bufPage, 64);
                    } else { GetWindowText(hComboPage, bufPage, 64); }
                    
                    if (cmd == 107 && evt == CBN_SELCHANGE) {
                        int idx = SendMessage(hComboOrient, CB_GETCURSEL, 0, 0);
                        if (idx != CB_ERR) SendMessage(hComboOrient, CB_GETLBTEXT, idx, (LPARAM)bufOrient);
                        else GetWindowText(hComboOrient, bufOrient, 32);
                    } else { GetWindowText(hComboOrient, bufOrient, 32); }

                    if (cmd == 102 && evt == CBN_SELCHANGE) {
                        int idx = SendMessage(hComboUnits, CB_GETCURSEL, 0, 0);
                        if (idx != CB_ERR) SendMessage(hComboUnits, CB_GETLBTEXT, idx, (LPARAM)bufUnits);
                        else GetWindowText(hComboUnits, bufUnits, 32);
                    } else { GetWindowText(hComboUnits, bufUnits, 32); }

                    GetWindowText(hEditW, bufW, 32); if (atoi(bufW) > 0) gW = atoi(bufW);
                    GetWindowText(hEditH, bufH, 32); if (atoi(bufH) > 0) gH = atoi(bufH);

                    if (strcmp(bufUnits, "cm") == 0) unitFactor = 10.0;
                    else if (strcmp(bufUnits, "m") == 0) unitFactor = 1000.0;
                    else if (strcmp(bufUnits, "inches") == 0 || strcmp(bufUnits, "feet-inches") == 0) unitFactor = 25.4;
                    else if (strcmp(bufUnits, "feet") == 0) unitFactor = 304.8;
                    else unitFactor = 1.0;

                    p1 = strchr(bufPage, '(');
                    if (p1) {
                        p2 = strchr(p1, 'x');
                        if (p2) { pW = atof(p1 + 1); pH = atof(p2 + 1); }
                    }

                    if (pW > 0 && pH > 0 && gW > 0 && gH > 0) {
                        exact_scale = fmin(pW / (gW * unitFactor), pH / (gH * unitFactor));
                        for (i = 0; i < 13; i++) {
                            if (std_scales[i] <= exact_scale) { chosen_idx = i; break; }
                        }
                        SetWindowText(hComboScale, std_scale_strs[chosen_idx]);
                    }
                }
            }
            break;
        case WM_DESTROY: hPageSizeDlg = NULL; hComboPage = NULL; hComboUnits = NULL; hComboScale = NULL; hComboOrient = NULL; hEditTitle = NULL; hEditW = NULL; hEditH = NULL; break;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}
LRESULT CALLBACK _export RefListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hList = NULL;
    switch(msg) {
        case WM_CREATE: {
            hList = CreateWindow("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
                                 10, 10, 360, 200, hwnd, (HMENU)101, hInst, NULL);
            CreateWindow("BUTTON", "Select", WS_CHILD | WS_VISIBLE, 140, 220, 100, 30, hwnd, (HMENU)1, hInst, NULL);
            
            FILE* f = fopen(pendingRefFile, "rb");
            if (f) {
                fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
                if (sz > 0 && sz <= 60000L) {
                    char* d = (char*)GlobalAllocPtr(GHND, sz + 1);
                    if (d) {
                        fread(d, 1, (size_t)sz, f); d[sz] = '\0';
                        char* cur = d; int idx = 0;
                        while ((cur = strstr(cur, "case ")) != NULL) {
                            char* endBlock = strstr(cur, "break;");
                            if (!endBlock) endBlock = cur + strlen(cur);
                            
                            char title[64] = "Untitled";
                            char* pgDef = strstr(cur, "PAGE_DEF(");
                            if (pgDef && pgDef < endBlock) {
                                char* q1 = FindQuote(pgDef);
                                char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                                if (q1 && q2) {
                                    int len = q2 - (q1 + 1); if (len > 63) len = 63;
                                    strncpy(title, q1 + 1, len); title[len] = '\0';
                                }
                            }
                            
                            char buf[128];
                            sprintf(buf, "Index: %d - Title: %s", idx, title);
                            int li = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
                            SendMessage(hList, LB_SETITEMDATA, li, idx);
                            idx++;
                            cur = endBlock;
                        }
                        GlobalFreePtr(d);
                    }
                }
                fclose(f);
            }
            SendMessage(hList, LB_SETCURSEL, 0, 0);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 1 || (LOWORD(wParam) == 101 && HIWORD(wParam) == LBN_DBLCLK)) {
                int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    int refIdx = SendMessage(hList, LB_GETITEMDATA, sel, 0);
                    
                    if (shapeCount < MAX_SHAPES) {
                        char relPath[260];
                        if (loadedCFile[0]) {
                            GetRelativePath(loadedCFile, pendingRefFile, relPath);
                        } else {
                            strcpy(relPath, pendingRefFile);
                        }
                        
                        SaveState();
                        memset(&shapes[shapeCount], 0, sizeof(Shape));
                        shapes[shapeCount].type = 3; 
                        shapes[shapeCount].ptCount = 1; 
                        shapes[shapeCount].stroke = currentStroke;
                        sprintf(shapes[shapeCount].text, "{{EXT_REF=%s scale=1.00 rot=0.00}}", relPath);
                        shapeCount++;
                        RedrawCanvas(hMain);
                        ShowStatus(" Reference added from list.");
                    }
                }
                DestroyWindow(hwnd);
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ResolvePath(const char* base, const char* rel, char* out) {
    char tempRel[260];
    char* p;
    char* lastSlash;

    if (!rel || !rel[0]) {
        if (out) out[0] = '\0';
        return;
    }

    /* Do not prepend base if rel is already an absolute path */
    if ((rel[0] && rel[1] == ':') || rel[0] == '\\' || rel[0] == '/') {
        strncpy(out, rel, 259);
        out[259] = '\0';
        return;
    }

    if (!base || !base[0]) {
        strncpy(out, rel, 259);
        out[259] = '\0';
        return;
    }

    strncpy(out, base, 259);
    out[259] = '\0';

    lastSlash = strrchr(out, '\\');
    if (!lastSlash) lastSlash = strrchr(out, '/');
    if (lastSlash) *(lastSlash + 1) = '\0';
    else out[0] = '\0';

    strncpy(tempRel, rel, 259);
    tempRel[259] = '\0';
    p = tempRel;

    while (strncmp(p, "..\\", 3) == 0 || strncmp(p, "../", 3) == 0) {
        p += 3;
        if (strlen(out) > 0) {
            out[strlen(out) - 1] = '\0';
            lastSlash = strrchr(out, '\\');
            if (!lastSlash) lastSlash = strrchr(out, '/');
            if (lastSlash) *(lastSlash + 1) = '\0';
            else out[0] = '\0';
        }
    }
    strncat(out, p, 259 - strlen(out));
}
void ShowRefListDialog(const char* filePath, HWND hwnd) {
    strcpy(pendingRefFile, filePath);
    CreateWindow("RefListClass", "Select Reference Icon", 
                 WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
                 100, 100, 400, 300, hwnd, NULL, hInst, NULL);
}
void SetPipeValue(char* tagData, int idx, const char* newVal) {
    /* Use static buffer to prevent Win16 stack overflow crash! */
    static char segs[16][128];
    int count = 0, i; char* p = tagData; char* start = tagData;
    
    memset(segs, 0, sizeof(segs));
    if (strlen(tagData) > 0) {
        while (*p) {
            if (*p == '|') {
                int len = p - start; if (len > 127) len = 127;
                strncpy(segs[count], start, len); segs[count][len] = '\0';
                count++; start = p + 1;
                if (count >= 16) break;
            }
            p++;
        }
        if (count < 16) {
            int len = p - start; if (len > 127) len = 127;
            strncpy(segs[count], start, len); segs[count][len] = '\0';
            count++;
        }
    }
    if (idx >= count) count = idx + 1;
    if (idx < 16) { strncpy(segs[idx], newVal, 127); segs[idx][127] = '\0'; }
    
    tagData[0] = '\0';
    for (i = 0; i < count; i++) {
        if (i > 0) strcat(tagData, "|");
        strncat(tagData, segs[i], 127);
    }
}
void SilentLoadC(const char* path, RefCache* ref) {
    FILE* f; long sz; char *d, *cur;
    ref->shapeCount = 0;

    f = fopen(path, "rb"); if (!f) return;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 60000L) { fclose(f); return; }
    d = (char*)GlobalAllocPtr(GHND, sz + 1);
    if (!d) { fclose(f); return; }
    fread(d, 1, (size_t)sz, f); d[sz] = '\0'; fclose(f);

    cur = strstr(d, "case ");
    if (cur) {
        char* endBlock = strstr(cur, "break;");
        char* st = cur; 
        if (!endBlock) endBlock = cur + strlen(cur);

        while (st < endBlock && ref->shapeCount < MAX_SHAPES) {
            char *pt = strstr(st, "POINT "), *lStr = strstr(st, "L(");
            char *ext = strstr(st, "EXT_REF("); 
            char *tag = strstr(st, "TAG_TEXT("), *dim = strstr(st, "DIMENSION(");
            char *next = pt ? pt : lStr;
            Shape* s = &ref->shapes[ref->shapeCount];

            if (lStr && (!next || lStr < next)) next = lStr;
            if (ext && (!next || ext < next)) next = ext; 
            if (tag && (!next || tag < next)) next = tag;
            if (dim && (!next || dim < next)) next = dim;
            if (!next || next >= endBlock) break;

            if (next == dim) { 
                char* pOpen = strchr(next, '(');
                if (pOpen) {
                    char* c1 = strchr(pOpen, ','); char* c2 = c1 ? strchr(c1 + 1, ',') : NULL;
                    char* c3 = c2 ? strchr(c2 + 1, ',') : NULL; char* c4 = c3 ? strchr(c3 + 1, ',') : NULL;
                    char* c5 = c4 ? strchr(c4 + 1, ',') : NULL; char* c6 = c5 ? strchr(c5 + 1, ',') : NULL;
                    if (c1 && c2 && c3 && c4 && c5 && c6 && ref->dimCount < MAX_DIMS) {
                        int vS1 = atoi(pOpen + 1); int vP1 = atoi(c1 + 1);
                        int vS2 = atoi(c2 + 1); int vP2 = atoi(c3 + 1);
                        double vOff = atof(c4 + 1); double vTPos = atof(c5 + 1); int vMd = atoi(c6 + 1);
                        
                        ref->dims[ref->dimCount].s1 = vS1; ref->dims[ref->dimCount].p1 = vP1;
                        ref->dims[ref->dimCount].s2 = vS2; ref->dims[ref->dimCount].p2 = vP2;
                        ref->dims[ref->dimCount].offset = vOff; ref->dims[ref->dimCount].textPos = vTPos;
                        ref->dims[ref->dimCount].mode = vMd; ref->dimCount++;
                    }
                }
                st = strchr(next, ';'); if (!st) st = next + 1; continue; 
            }

            memset(s, 0, sizeof(Shape));
            s->useFill = 1; s->useStroke = 1; s->fill = RGB(128, 128, 128); s->stroke = RGB(0, 0, 0); s->strokeWidth = 1; s->fontSize = 24;

            {
                char savedChar = *next; *next = '\0';
                char* brushSearch = strstr(st, "CreateSolidBrush(RGB(");
                if (brushSearch) {
                    char* rgb = strstr(brushSearch, "RGB(");
                    if (rgb) {
                        int cr = atoi(rgb + 4); char* comma1 = strchr(rgb + 4, ',');
                        if (comma1) {
                            int cg = atoi(comma1 + 1); char* comma2 = strchr(comma1 + 1, ',');
                            if (comma2) { int cb = atoi(comma2 + 1); s->fill = RGB(cr, cg, cb); s->useFill = 1; }
                        }
                    }
                }
                char* penSearch = strstr(st, "CreatePen(PS_SOLID, ");
                if (penSearch) {
                    int sw = atoi(penSearch + 20); char* rgb = strstr(penSearch, "RGB(");
                    if (rgb) {
                        int cr = atoi(rgb + 4); char* comma1 = strchr(rgb + 4, ',');
                        if (comma1) {
                            int cg = atoi(comma1 + 1); char* comma2 = strchr(comma1 + 1, ',');
                            if (comma2) { int cb = atoi(comma2 + 1); s->stroke = RGB(cr, cg, cb); s->useStroke = 1; s->strokeWidth = sw > 0 ? sw : 1; }
                        }
                    }
                }
                *next = savedChar;
            }

            if (next == pt) {
                char* searchLimit = endBlock;
                char* nPt = strstr(next + 1, "POINT "); if (nPt && nPt < searchLimit) searchLimit = nPt;
                char* nL = strstr(next + 1, "L("); if (nL && nL < searchLimit) searchLimit = nL;
                char* nEx = strstr(next + 1, "EXT_REF("); if (nEx && nEx < searchLimit) searchLimit = nEx;
                char* nTg = strstr(next + 1, "TAG_TEXT("); if (nTg && nTg < searchLimit) searchLimit = nTg;
                char* nDim = strstr(next + 1, "DIMENSION("); if (nDim && nDim < searchLimit) searchLimit = nDim;

                char saved = *searchLimit; *searchLimit = '\0';
                s->type = strstr(next, "Polyline(") ? 2 : 0; *searchLimit = saved;

                char *pC = next, *bracket = strchr(next, '}');
                while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) {
                    char* comma = strchr(pC + 3, ',');
                    if (comma && comma < bracket && s->ptCount < MAX_POINTS) {
                        double vX = atof(pC + 3); double vY = atof(comma + 1);
                        s->ptsX[s->ptCount] = vX; s->ptsY[s->ptCount] = vY; s->ptCount++;
                    }
                    pC += 3;
                }
                if (s->ptCount > 0) ref->shapeCount++;
            } else if (next == lStr) {
                char* pOpen = strchr(next, '(');
                if (pOpen) {
                    char* c1 = strchr(pOpen, ','); char* c2 = c1 ? strchr(c1 + 1, ',') : NULL; char* c3 = c2 ? strchr(c2 + 1, ',') : NULL;
                    if (c1 && c2 && c3) {
                        double x1 = atof(pOpen + 1); double y1 = atof(c1 + 1);
                        double x2 = atof(c2 + 1); double y2 = atof(c3 + 1);
                        s->type = 1; s->ptCount = 2;
                        s->ptsX[0] = x1; s->ptsY[0] = y1; s->ptsX[1] = x2; s->ptsY[1] = y2;
                        ref->shapeCount++;
                    }
                }
            } else if (next == ext) {
                static char refStr[128]; static char tagStr[256]; tagStr[0] = '\0';
                char* pOpen = strchr(next, '('); char* q1 = pOpen ? FindQuote(pOpen) : NULL; char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                if (pOpen && q1 && q2) {
                    char* comma1 = strchr(pOpen + 1, ','); int rLen = q2 - (q1 + 1); if (rLen > 127) rLen = 127;
                    strncpy(refStr, q1 + 1, rLen); refStr[rLen] = '\0';
                    char* comma3 = q2 ? strchr(q2 + 1, ',') : NULL;
                    if (comma3) {
                        char* q3 = FindQuote(comma3); char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                        if (q3 && q4) {
                            int tLen = q4 - (q3 + 1); if (tLen > 255) tLen = 255;
                            strncpy(tagStr, q3 + 1, tLen); tagStr[tLen] = '\0';
                        }
                    }
                    double px = atof(pOpen + 1); double py = comma1 ? atof(comma1 + 1) : 0.0;
                    s->type = 3; s->ptsX[0] = px; s->ptsY[0] = py; s->ptCount = 1;
                    strcpy(s->text, refStr); UnescapeCString(tagStr, s->tagData, 128);
                    ref->shapeCount++;
                }
            } else if (next == tag) {
                char* pOpen = strchr(next, '('); char* q1 = pOpen ? FindQuote(pOpen) : NULL; char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                if (pOpen) {
                    char* comma1 = strchr(pOpen, ',');
                    if (comma1 && (!q1 || comma1 < q1)) {
                        double px = atof(pOpen + 1); double py = atof(comma1 + 1);
                        s->type = 4; s->ptsX[0] = px; s->ptsY[0] = py; s->ptCount = 1;
                        char* comma2 = strchr(comma1 + 1, ','); char* pClose = strchr(comma2 ? comma2 : pOpen, ')');

                        if (q1 && q2 && q1 < pClose) {
                            int tLen = q2 - (q1 + 1); if (tLen > 127) tLen = 127;
                            static char rawTag[256]; strncpy(rawTag, q1 + 1, tLen); rawTag[tLen] = '\0'; UnescapeCString(rawTag, s->text, 128);
                            char* commaAfter = strchr(q2, ',');
                            if (commaAfter && commaAfter < pClose) {
                                char jChar = 'L'; char* jPtr = commaAfter + 1; while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                char* comma4 = strchr(commaAfter + 1, ','); if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                            } else s->useFill = 0;
                        } else if (comma2) {
                            char* comma3 = strchr(comma2 + 1, ','); char* endMarker = comma3 ? comma3 : pClose;
                            if (endMarker) {
                                int tLen = endMarker - (comma2 + 1); if (tLen > 127) tLen = 127;
                                static char rawTag[256]; strncpy(rawTag, comma2 + 1, tLen); rawTag[tLen] = '\0';
                                char* start = rawTag; while(*start && isspace((unsigned char)*start)) start++;
                                char* end = start + strlen(start) - 1; while(end > start && isspace((unsigned char)*end)) *end-- = '\0';
                                UnescapeCString(start, s->text, 128);
                                if (comma3) {
                                    char jChar = 'L'; char* jPtr = comma3 + 1; while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                    if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                    char* comma4 = strchr(comma3 + 1, ','); if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                                } else s->useFill = 0;
                            }
                        }
                        ref->shapeCount++;
                    }
                }
            }
            st = strchr(next, ';'); if (!st) st = next + 1;
        }
    }
    GlobalFreePtr(d);
}
void SilentLoadSVG(const char* path, RefCache* ref) {
    FILE* f;
    long sz;
    char *d, *p;

    ref->shapeCount = 0;
    ref->tCount = 0;

    f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 60000L) {
        fclose(f);
        return;
    }

    d = (char*)GlobalAllocPtr(GHND, (size_t)sz + 1);
    if (!d) {
        fclose(f);
        return;
    }

    fread(d, 1, (size_t)sz, f);
    d[sz] = '\0';
    fclose(f);

    p = d;
    while ((p = strchr(p, '<')) != NULL && ref->shapeCount < MAX_SHAPES) {
        char* tagEnd;
        p++;
        tagEnd = strchr(p, '>');
        if (!tagEnd) break;

        if (strncmp(p, "rect", 4) == 0) {
            double rx = AttrF(p, tagEnd, "x", 0.0);
            double ry = AttrF(p, tagEnd, "y", 0.0);
            double rw = AttrF(p, tagEnd, "width", 0.0);
            double rh = AttrF(p, tagEnd, "height", 0.0);
            if (rw > 0 && rh > 0) {
                Shape* s = &ref->shapes[ref->shapeCount];
                memset(s, 0, sizeof(Shape));
                s->type = 0; s->ptCount = 4;
                GetSVGColors(p, tagEnd, &s->fill, &s->stroke, &s->useFill, &s->useStroke, 1);
                s->ptsX[0] = rx;      s->ptsY[0] = ry;
                s->ptsX[1] = rx + rw; s->ptsY[1] = ry;
                s->ptsX[2] = rx + rw; s->ptsY[2] = ry + rh;
                s->ptsX[3] = rx;      s->ptsY[3] = ry + rh;
                ref->shapeCount++;
            }
        } else if (strncmp(p, "poly", 4) == 0) {
            int isPolyline = (strncmp(p, "polyline", 8) == 0);
            char* ptsStr = Attr(p, tagEnd, "points");
            if (ptsStr) {
                Shape* s = &ref->shapes[ref->shapeCount];
                memset(s, 0, sizeof(Shape));
                s->type = isPolyline ? 2 : 0;
                GetSVGColors(p, tagEnd, &s->fill, &s->stroke, &s->useFill, &s->useStroke, !isPolyline);
                while (s->ptCount < MAX_POINTS) {
                    double px, py;
                    if (!GetNextSVGFloat(&ptsStr, &px)) break;
                    if (!GetNextSVGFloat(&ptsStr, &py)) break;
                    s->ptsX[s->ptCount] = px; s->ptsY[s->ptCount] = py; s->ptCount++;
                }
                if (s->ptCount >= 2) ref->shapeCount++;
            }
        } else if (strncmp(p, "path", 4) == 0) {
            char* dStr = Attr(p, tagEnd, "d");
            if (dStr) {
                Shape* s = &ref->shapes[ref->shapeCount];
                double curX = 0, curY = 0;
                char cmd = 'M';
                memset(s, 0, sizeof(Shape));
                s->type = 0;
                GetSVGColors(p, tagEnd, &s->fill, &s->stroke, &s->useFill, &s->useStroke, 1);
                while (*dStr && *dStr != '"' && s->ptCount < MAX_POINTS) {
                    double argX, argY;
                    while (*dStr && isspace((unsigned char)*dStr)) dStr++;
                    if (isalpha((unsigned char)*dStr)) { cmd = *dStr; dStr++; }
                    if (toupper(cmd) == 'Z') break;
                    if (toupper(cmd) == 'M' || toupper(cmd) == 'L') {
                        if (!GetNextSVGFloat(&dStr, &argX) || !GetNextSVGFloat(&dStr, &argY)) break;
                        if (cmd == 'm' || cmd == 'l') { argX += curX; argY += curY; }
                        curX = argX; curY = argY;
                        s->ptsX[s->ptCount] = curX; s->ptsY[s->ptCount] = curY; s->ptCount++;
                        if (toupper(cmd) == 'M') cmd = (cmd == 'M') ? 'L' : 'l';
                    } else {
                        GetNextSVGFloat(&dStr, &argX);
                    }
                }
                if (s->ptCount >= 2) ref->shapeCount++;
            }
        }
    }
    GlobalFreePtr(d);
}
LRESULT CALLBACK _export ScaleDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateWindow("STATIC", "SVG Scale:", WS_CHILD|WS_VISIBLE, 10, 10, 80, 20, hwnd, NULL, hInst, NULL);
            CreateWindow("EDIT", "1.0", WS_CHILD|WS_VISIBLE|WS_BORDER, 100, 10, 60, 20, hwnd, (HMENU)101, hInst, NULL);
            CreateWindow("BUTTON", "Apply", WS_CHILD|WS_VISIBLE, 50, 40, 80, 25, hwnd, (HMENU)1, hInst, NULL);
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {
                char buf[32]; GetWindowText(GetDlgItem(hwnd, 101), buf, 32);
                extScale = atof(buf); DestroyWindow(hwnd);
            }
            break;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
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

#pragma code_seg ( "DLG_TEXT" );
LRESULT CALLBACK _export TagEditorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEd[MAX_EDIT_TAGS];
    static HWND hLbl[MAX_EDIT_TAGS];
    static HWND hBtnUpdate;
    static int scrollY = 0, maxScroll = 0;
    
    switch (msg) {
        case WM_CREATE: {
            int i, k; char esc[256]; scrollY = 0;
            editMapCount = 0;
            
            for (i=0; i<MAX_EDIT_TAGS; i++) { hLbl[i] = NULL; hEd[i] = NULL; }
            
            for (i=0; i<shapeCount && editMapCount < MAX_EDIT_TAGS; i++) {
                if (shapes[i].type == 4) {
                    editMap[editMapCount].isRef = 0;
                    editMap[editMapCount].shapeIdx = i;
                    editMap[editMapCount].tagIdx = -1;
                    sprintf(editMap[editMapCount].label, "TAG%d", editMapCount + 1);
                    strcpy(editMap[editMapCount].value, shapes[i].text);
                    editMapCount++;
                } else if (shapes[i].type == 3) {
                    char refPath[260];
                    char *pScale = strstr(shapes[i].text, " scale=");
                    char *pEnd = strstr(shapes[i].text, "}}");
                    int pathLen;
                    
                    if (pScale) pathLen = (int)(pScale - (shapes[i].text + 10));
                    else if (pEnd) pathLen = (int)(pEnd - (shapes[i].text + 10));
                    else pathLen = strlen(shapes[i].text + 10);
                    
                    if (pathLen > 0 && pathLen < 260 && strncmp(shapes[i].text, "{{EXT_REF=", 10) == 0) {
                        char absPath[260]; int rIdx; char baseForResolve[260];
                        char fileName[64]; char* slash;

                        strncpy(refPath, shapes[i].text + 10, pathLen);
                        refPath[pathLen] = '\0';
                        while(pathLen > 0 && isspace((unsigned char)refPath[pathLen-1])) refPath[--pathLen] = '\0';
                        
                        slash = strrchr(refPath, '\\');
                        if (!slash) slash = strrchr(refPath, '/');
                        strncpy(fileName, slash ? slash + 1 : refPath, 63);
                        fileName[63] = '\0';
                        
                        GetResolveBase(baseForResolve);
                        ResolvePath(baseForResolve, refPath, absPath);
                        rIdx = EnsureRefLoaded(absPath);
                        if (rIdx != -1) {
                            int refTagIdx = 0;
                            for(k=0; k<refCache[rIdx].shapeCount && editMapCount < MAX_EDIT_TAGS; k++) {
                                if (refCache[rIdx].shapes[k].type == 4) {
                                    editMap[editMapCount].isRef = 1;
                                    editMap[editMapCount].shapeIdx = i;
                                    editMap[editMapCount].tagIdx = refTagIdx;
                                    sprintf(editMap[editMapCount].label, "[%s] TAG%d", fileName, refTagIdx + 1);
                                    GetPipeValue(shapes[i].tagData, refTagIdx, editMap[editMapCount].value, 128);
                                    editMapCount++;
                                    refTagIdx++;
                                }
                            }
                        }
                    }
                }
            }

            for (i=0; i<editMapCount; i++) {
                hLbl[i] = CreateWindow("STATIC", editMap[i].label, WS_CHILD|WS_VISIBLE, 10, 10 + i*30, 80, 20, hwnd, NULL, hInst, NULL);
                hEd[i] = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 100, 10 + i*30, 160, 20, hwnd, NULL, hInst, NULL);
                EscapeCString(editMap[i].value, esc, 256); SetWindowText(hEd[i], esc);
            }
            hBtnUpdate = CreateWindow("BUTTON", "Update Tags", WS_CHILD|WS_VISIBLE, 100, 10 + editMapCount*30, 100, 30, hwnd, (HMENU)1, hInst, NULL);
            break;
        }
        case WM_SIZE: {
            int ch = HIWORD(lParam); int i; HDWP hdwp;
            maxScroll = (editMapCount * 30 + 60) > ch ? (editMapCount * 30 + 60) - ch : 0;
            SetScrollRange(hwnd, SB_VERT, 0, maxScroll, TRUE);
            if (scrollY > maxScroll) scrollY = maxScroll;
            
            hdwp = BeginDeferWindowPos(editMapCount * 2 + 1);
            for(i=0; i<editMapCount; i++) {
                if (hLbl[i]) hdwp = DeferWindowPos(hdwp, hLbl[i], NULL, 10, 10 + i*30 - scrollY, 80, 20, SWP_NOZORDER|SWP_NOACTIVATE);
                if (hEd[i])  hdwp = DeferWindowPos(hdwp, hEd[i],  NULL, 100, 10 + i*30 - scrollY, 160, 20, SWP_NOZORDER|SWP_NOACTIVATE);
            }
            if (hBtnUpdate) hdwp = DeferWindowPos(hdwp, hBtnUpdate, NULL, 100, 10 + editMapCount*30 - scrollY, 100, 30, SWP_NOZORDER|SWP_NOACTIVATE);
            EndDeferWindowPos(hdwp);
            break;
        }
        case WM_VSCROLL: {
            int pos = scrollY, i; HDWP hdwp;
            switch(wParam) {
                case SB_LINEUP: pos -= 15; break;
                case SB_LINEDOWN: pos += 15; break;
                case SB_PAGEUP: pos -= 60; break;
                case SB_PAGEDOWN: pos += 60; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = LOWORD(lParam); break;
            }
            if (pos < 0) pos = 0; if (pos > maxScroll) pos = maxScroll;
            if (pos != scrollY) {
                scrollY = pos; SetScrollPos(hwnd, SB_VERT, scrollY, TRUE);
                hdwp = BeginDeferWindowPos(editMapCount * 2 + 1);
                for(i=0; i<editMapCount; i++) {
                    if (hLbl[i]) hdwp = DeferWindowPos(hdwp, hLbl[i], NULL, 10, 10 + i*30 - scrollY, 80, 20, SWP_NOZORDER|SWP_NOACTIVATE);
                    if (hEd[i])  hdwp = DeferWindowPos(hdwp, hEd[i],  NULL, 100, 10 + i*30 - scrollY, 160, 20, SWP_NOZORDER|SWP_NOACTIVATE);
                }
                if (hBtnUpdate) hdwp = DeferWindowPos(hdwp, hBtnUpdate, NULL, 100, 10 + editMapCount*30 - scrollY, 100, 30, SWP_NOZORDER|SWP_NOACTIVATE);
                EndDeferWindowPos(hdwp);
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {
                int i; char buf[256];
                SaveState();
                for (i=0; i<editMapCount; i++) {
                    GetWindowText(hEd[i], buf, 256); 
                    UnescapeCString(buf, editMap[i].value, 128);
                    if (!editMap[i].isRef) {
                        strncpy(shapes[editMap[i].shapeIdx].text, editMap[i].value, 127);
                        shapes[editMap[i].shapeIdx].text[127] = '\0';
                    } else {
                        SetPipeValue(shapes[editMap[i].shapeIdx].tagData, editMap[i].tagIdx, editMap[i].value);
                    }
                }
                DestroyWindow(hwnd); RedrawCanvas(hMain);
            }
            break;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}
void UnescapeCString(const char* in, char* out, int maxLen) {
    int len = 0;
    if (!in || !out) return;
    while (*in && len < maxLen - 2) {
        if (*in == '\\' && *(in+1) == 'n') { *out++ = '\n'; in += 2; len++; } 
        else if (*in == '\\' && *(in+1) == '"') { *out++ = '"'; in += 2; len++; } 
        else if (*in == '\\' && *(in+1) == '\\') { *out++ = '\\'; in += 2; len++; } 
        else { *out++ = *in++; len++; }
    }
    *out = '\0';
}
void UnescapeNewlines(const char* in, char* out) {
    while (*in) {
        if (*in == '\\' && *(in+1) == 'n') { *out++ = '\r'; *out++ = '\n'; in += 2; }
        else { *out++ = *in++; }
    }
    *out = '\0';
}

void UpdateStatusBar(void) {
    char sb[128]; 
    sprintf(sb, " [ID: %d Mode: %s] Shapes: %d | Nodes: %d | Depth: %d%%", currentCaseId, bT[currentMode], shapeCount, selOrderCount, paramStar);
    ShowStatus(sb);
}

void RedrawCanvas(HWND hwnd) {
    RECT r;
    r.left = 0; r.top = 0;
    r.right = clientW - PANEL_WIDTH; 
    r.bottom = clientH - 20; 
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
    int i;
    if (currentIconIdx >= 0 && currentIconIdx < parsedCount) {
        if (parsedIcons[currentIconIdx].shapes) { GlobalFreePtr(parsedIcons[currentIconIdx].shapes); parsedIcons[currentIconIdx].shapes = NULL; }
        if (shapeCount > 0) {
            parsedIcons[currentIconIdx].shapes = (Shape*)GlobalAllocPtr(GHND, sizeof(Shape) * shapeCount);
            if (parsedIcons[currentIconIdx].shapes) memcpy(parsedIcons[currentIconIdx].shapes, shapes, sizeof(Shape) * shapeCount);
        } else {
            parsedIcons[currentIconIdx].shapes = NULL;
        }
        parsedIcons[currentIconIdx].shapeCount = shapeCount;
        
        parsedIcons[currentIconIdx].tCount = tagCount;
        for(i=0; i<tagCount; i++) {
            strcpy(parsedIcons[currentIconIdx].tNames[i], tagNames[i]);
            strcpy(parsedIcons[currentIconIdx].tVals[i], tagValues[i]);
        }
        
        parsedIcons[currentIconIdx].dimCount = dimCount;
        for(i=0; i<dimCount; i++) parsedIcons[currentIconIdx].dims[i] = dims[i];

        strcpy(parsedIcons[currentIconIdx].pageName, activePageName);
        strcpy(parsedIcons[currentIconIdx].unitName, activeUnitName);
        parsedIcons[currentIconIdx].pageScale = activePageScale;
        parsedIcons[currentIconIdx].gridW = gridW;
        parsedIcons[currentIconIdx].gridH = gridH;
    }
}

void SwitchToIcon(int idx) {
    int i;
    if (currentIconIdx == idx) return;
    CommitCurrentIcon();
    
    currentIconIdx = idx;
    dimCount = 0; dragDimIdx = -1; tagCount = 0;
    if (idx >= 0 && idx < parsedCount) {
        currentCaseId = parsedIcons[idx].caseId; shapeCount = parsedIcons[idx].shapeCount;
        if (parsedIcons[idx].shapes) memcpy(shapes, parsedIcons[idx].shapes, sizeof(Shape) * shapeCount); else shapeCount = 0;
        
        tagCount = parsedIcons[idx].tCount;
        for(i=0; i<tagCount; i++) {
            strcpy(tagNames[i], parsedIcons[idx].tNames[i]);
            strcpy(tagValues[i], parsedIcons[idx].tVals[i]);
        }
        
        dimCount = parsedIcons[idx].dimCount;
        for(i=0; i<dimCount; i++) dims[i] = parsedIcons[idx].dims[i];
        
        if (parsedIcons[idx].pageName[0] == '\0') {
            strcpy(activePageName, "A4 (210x297)");
            strcpy(activeUnitName, "mm");
            activePageScale = 1.0;
        } else {
            strcpy(activePageName, parsedIcons[idx].pageName);
            strcpy(activeUnitName, parsedIcons[idx].unitName);
            activePageScale = parsedIcons[idx].pageScale;
        }
        if (parsedIcons[idx].gridW > 0) gridW = parsedIcons[idx].gridW; else gridW = 32;
        if (parsedIcons[idx].gridH > 0) gridH = parsedIcons[idx].gridH; else gridH = 32;
    } else { 
        shapeCount = 0; dimCount = 0; tagCount = 0; 
        strcpy(activePageName, "A4 (210x297)");
        strcpy(activeUnitName, "mm");
        activePageScale = 1.0;
        gridW = 32; gridH = 32;
    }
    
    if (hEditW && IsWindow(hEditW)) {
        char buf[16];
        sprintf(buf, "%d", gridW); SetWindowText(hEditW, buf);
        sprintf(buf, "%d", gridH); SetWindowText(hEditH, buf);
    }
    
    undoIndex = -1; ClearSelection(); selectedShape = -1;
    if (shapeCount == 0) currentMode = 3; else currentMode = 0;
    if (hScrlIcon) SetScrollPos(hScrlIcon, SB_CTL, currentIconIdx >= 0 ? currentIconIdx : 0, TRUE);
    UpdateStatusBar(); 
    SendMessage(hMain, WM_SIZE, 0, MAKELPARAM(clientW, clientH));
    RedrawCanvas(hMain);
}

void LoadCFile(const char* path, HWND hwnd) {
    FILE* f; long sz; char *d, *cur, *endBlock, *st, *pt, *lStr, *ext, *tag, *next;
    int i, cId, tmpCount, tmpDimCount; Shape* exact;
    static Dimension tmpDims[MAX_DIMS];

    f = fopen(path, "rb"); if (!f) return;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 60000L) { fclose(f); ShowStatus(" Error: File invalid or exceeds 60KB limit."); return; }

    for (i = 0; i < MAX_ICONS; i++) {
        if (parsedIcons[i].shapes) { GlobalFreePtr(parsedIcons[i].shapes); parsedIcons[i].shapes = NULL; }
        parsedIcons[i].shapeCount = 0; parsedIcons[i].dimCount = 0;
    }
    parsedCount = 0; refCacheCount = 0; currentIconIdx = -1; shapeCount = 0; dimCount = 0;

    d = (char*)GlobalAllocPtr(GHND, (size_t)sz + 1);
    if (d) {
        fread(d, 1, (size_t)sz, f); d[sz] = '\0'; fclose(f); cur = d;

        while ((cur = strstr(cur, "case ")) != NULL && parsedCount < MAX_ICONS) {
            char tmpPageName[64]; char tmpUnitName[32]; double tmpPageScale;
            int tH = 32, tW = 32;
            cId = atoi(cur + 5); endBlock = strstr(cur, "break;");
            tmpCount = 0; tmpDimCount = 0; st = cur;
            strcpy(tmpPageName, "A4 (210x297)"); strcpy(tmpUnitName, "mm"); tmpPageScale = 1.0;
            strcpy(parsedIcons[parsedCount].iconTitle, "Untitled");
            if (!endBlock) endBlock = cur + strlen(cur);

            while (st < endBlock && tmpCount < MAX_SHAPES) {
                Shape* s = &dragStartSnapshot[tmpCount];
                char *dim = strstr(st, "DIMENSION(");
                char *pgDef = strstr(st, "PAGE_DEF(");
                pt = strstr(st, "POINT "); lStr = strstr(st, "L(");
                ext = strstr(st, "EXT_REF("); tag = strstr(st, "TAG_TEXT(");

                next = pt ? pt : lStr;
                if (lStr && (!next || lStr < next)) next = lStr;
                if (ext && (!next || ext < next)) next = ext;
                if (tag && (!next || tag < next)) next = tag;
                if (dim && (!next || dim < next)) next = dim;
                if (pgDef && (!next || pgDef < next)) next = pgDef;
                if (!next || next >= endBlock) break;

                if (next == pgDef) {
                    char* pOpen = strchr(next, '(');
                    char* qTStart = pOpen ? FindQuote(pOpen) : NULL;
                    char* qTEnd = qTStart ? FindQuote(qTStart + 1) : NULL;

                    if (qTStart && qTEnd) {
                        int len = qTEnd - (qTStart + 1); if (len > 63) len = 63;
                        strncpy(parsedIcons[parsedCount].iconTitle, qTStart + 1, len);
                        parsedIcons[parsedCount].iconTitle[len] = '\0';
                        
                        char* comma1 = strchr(qTEnd + 1, ',');
                        if (comma1) {
                            char* c1_val = comma1 + 1;
                            while (*c1_val && isspace((unsigned char)*c1_val)) c1_val++;
                            
                            if (*c1_val == '"') {
                                tH = 32; tW = 32;
                                char* qPStart = FindQuote(comma1);
                                char* qPEnd = qPStart ? FindQuote(qPStart + 1) : NULL;
                                if (qPStart && qPEnd) {
                                    len = qPEnd - (qPStart + 1); if (len > 63) len = 63;
                                    strncpy(tmpPageName, qPStart + 1, len); tmpPageName[len] = '\0';
                                    
                                    char* qUStart = FindQuote(qPEnd + 1);
                                    char* qUEnd = qUStart ? FindQuote(qUStart + 1) : NULL;
                                    if (qUStart && qUEnd) {
                                        len = qUEnd - (qUStart + 1); if (len > 31) len = 31;
                                        strncpy(tmpUnitName, qUStart + 1, len); tmpUnitName[len] = '\0';
                                        
                                        char* commaScale = strchr(qUEnd + 1, ',');
                                        if (commaScale) tmpPageScale = atof(commaScale + 1);
                                    }
                                }
                            } else {
                                tH = atoi(c1_val);
                                char* comma2 = strchr(c1_val, ',');
                                if (comma2) {
                                    tW = atoi(comma2 + 1);
                                    char* qPStart = FindQuote(comma2 + 1);
                                    char* qPEnd = qPStart ? FindQuote(qPStart + 1) : NULL;
                                    if (qPStart && qPEnd) {
                                        len = qPEnd - (qPStart + 1); if (len > 63) len = 63;
                                        strncpy(tmpPageName, qPStart + 1, len); tmpPageName[len] = '\0';
                                        
                                        char* qUStart = FindQuote(qPEnd + 1);
                                        char* qUEnd = qUStart ? FindQuote(qUStart + 1) : NULL;
                                        if (qUStart && qUEnd) {
                                            len = qUEnd - (qUStart + 1); if (len > 31) len = 31;
                                            strncpy(tmpUnitName, qUStart + 1, len); tmpUnitName[len] = '\0';
                                            
                                            char* commaScale = strchr(qUEnd + 1, ',');
                                            if (commaScale) tmpPageScale = atof(commaScale + 1);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (tH <= 0) tH = 32; if (tW <= 0) tW = 32;
                    st = strchr(next, ';'); if (!st) st = next + 1; continue;
                }

                if (next == dim) { 
                    char* pOpen = strchr(next, '(');
                    if (pOpen) {
                        char* c1 = strchr(pOpen, ','); char* c2 = c1 ? strchr(c1 + 1, ',') : NULL;
                        char* c3 = c2 ? strchr(c2 + 1, ',') : NULL; char* c4 = c3 ? strchr(c3 + 1, ',') : NULL;
                        char* c5 = c4 ? strchr(c4 + 1, ',') : NULL; char* c6 = c5 ? strchr(c5 + 1, ',') : NULL;
                        if (c1 && c2 && c3 && c4 && c5 && c6 && tmpDimCount < MAX_DIMS) {
                            int vS1 = atoi(pOpen + 1); int vP1 = atoi(c1 + 1);
                            int vS2 = atoi(c2 + 1); int vP2 = atoi(c3 + 1);
                            double vOff = atof(c4 + 1); double vTPos = atof(c5 + 1); int vMd = atoi(c6 + 1);
                            
                            tmpDims[tmpDimCount].s1 = vS1; tmpDims[tmpDimCount].p1 = vP1;
                            tmpDims[tmpDimCount].s2 = vS2; tmpDims[tmpDimCount].p2 = vP2;
                            tmpDims[tmpDimCount].offset = vOff; tmpDims[tmpDimCount].textPos = vTPos;
                            tmpDims[tmpDimCount].mode = vMd; tmpDimCount++;
                        }
                    }
                    st = strchr(next, ';'); if (!st) st = next + 1; continue; 
                }

                memset(s, 0, sizeof(Shape));
                s->useFill = 1; s->useStroke = 1; s->fill = RGB(128, 128, 128); s->stroke = RGB(0, 0, 0); s->strokeWidth = 1; s->fontSize = 24;

                {
                    char savedChar = *next; *next = '\0';
                    char* brushSearch = strstr(st, "CreateSolidBrush(RGB(");
                    if (brushSearch) {
                        char* rgb = strstr(brushSearch, "RGB(");
                        if (rgb) {
                            int cr = atoi(rgb + 4); char* comma1 = strchr(rgb + 4, ',');
                            if (comma1) {
                                int cg = atoi(comma1 + 1); char* comma2 = strchr(comma1 + 1, ',');
                                if (comma2) { int cb = atoi(comma2 + 1); s->fill = RGB(cr, cg, cb); s->useFill = 1; }
                            }
                        }
                    }
                    char* penSearch = strstr(st, "CreatePen(PS_SOLID, ");
                    if (penSearch) {
                        int sw = atoi(penSearch + 20); char* rgb = strstr(penSearch, "RGB(");
                        if (rgb) {
                            int cr = atoi(rgb + 4); char* comma1 = strchr(rgb + 4, ',');
                            if (comma1) {
                                int cg = atoi(comma1 + 1); char* comma2 = strchr(comma1 + 1, ',');
                                if (comma2) { int cb = atoi(comma2 + 1); s->stroke = RGB(cr, cg, cb); s->useStroke = 1; s->strokeWidth = sw > 0 ? sw : 1; }
                            }
                        }
                    }
                    *next = savedChar;
                }

                if (next == pt) {
                    char* searchLimit = endBlock;
                    char* nPt = strstr(next + 1, "POINT "); if (nPt && nPt < searchLimit) searchLimit = nPt;
                    char* nL = strstr(next + 1, "L("); if (nL && nL < searchLimit) searchLimit = nL;
                    char* nEx = strstr(next + 1, "EXT_REF("); if (nEx && nEx < searchLimit) searchLimit = nEx;
                    char* nTg = strstr(next + 1, "TAG_TEXT("); if (nTg && nTg < searchLimit) searchLimit = nTg;
                    char* nDim = strstr(next + 1, "DIMENSION("); if (nDim && nDim < searchLimit) searchLimit = nDim;
                    char* nPg = strstr(next + 1, "PAGE_DEF("); if (nPg && nPg < searchLimit) searchLimit = nPg;

                    char saved = *searchLimit; *searchLimit = '\0';
                    s->type = strstr(next, "Polyline(") ? 2 : 0; *searchLimit = saved;

                    char *pC = next, *bracket = strchr(next, '}');
                    while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) {
                        char* comma = strchr(pC + 3, ',');
                        if (comma && comma < bracket && s->ptCount < MAX_POINTS) {
                            double vX = atof(pC + 3); double vY = atof(comma + 1);
                            s->ptsX[s->ptCount] = vX; s->ptsY[s->ptCount] = vY; s->ptCount++;
                        }
                        pC += 3;
                    }
                    if (s->ptCount > 0) tmpCount++;
                } else if (next == lStr) {
                    char* pOpen = strchr(next, '(');
                    if (pOpen) {
                        char* c1 = strchr(pOpen, ','); char* c2 = c1 ? strchr(c1 + 1, ',') : NULL; char* c3 = c2 ? strchr(c2 + 1, ',') : NULL;
                        if (c1 && c2 && c3) {
                            double x1 = atof(pOpen + 1); double y1 = atof(c1 + 1);
                            double x2 = atof(c2 + 1); double y2 = atof(c3 + 1);
                            s->type = 1; s->ptCount = 2;
                            s->ptsX[0] = x1; s->ptsY[0] = y1; s->ptsX[1] = x2; s->ptsY[1] = y2;
                            tmpCount++;
                        }
                    }
                } else if (next == ext) {
                    static char refStr[128]; static char tagStr[256]; tagStr[0] = '\0';
                    char* pOpen = strchr(next, '('); char* q1 = pOpen ? FindQuote(pOpen) : NULL; char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                    if (pOpen && q1 && q2) {
                        char* comma1 = strchr(pOpen + 1, ','); int rLen = q2 - (q1 + 1); if (rLen > 127) rLen = 127;
                        strncpy(refStr, q1 + 1, rLen); refStr[rLen] = '\0';
                        char* comma3 = q2 ? strchr(q2 + 1, ',') : NULL;
                        if (comma3) {
                            char* q3 = FindQuote(comma3); char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                            if (q3 && q4) {
                                int tLen = q4 - (q3 + 1); if (tLen > 255) tLen = 255;
                                strncpy(tagStr, q3 + 1, tLen); tagStr[tLen] = '\0';
                            }
                        }
                        double px = atof(pOpen + 1); double py = comma1 ? atof(comma1 + 1) : 0.0;
                        s->type = 3; s->ptsX[0] = px; s->ptsY[0] = py; s->ptCount = 1;
                        strcpy(s->text, refStr); UnescapeCString(tagStr, s->tagData, 128);
                        tmpCount++;
                    }
                } else if (next == tag) {
                    char* pOpen = strchr(next, '('); char* q1 = pOpen ? FindQuote(pOpen) : NULL; char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                    if (pOpen) {
                        char* comma1 = strchr(pOpen, ',');
                        if (comma1 && (!q1 || comma1 < q1)) {
                            double px = atof(pOpen + 1); double py = atof(comma1 + 1);
                            s->type = 4; s->ptsX[0] = px; s->ptsY[0] = py; s->ptCount = 1;
                            char* comma2 = strchr(comma1 + 1, ','); char* pClose = strchr(comma2 ? comma2 : pOpen, ')');

                            if (q1 && q2 && q1 < pClose) {
                                int tLen = q2 - (q1 + 1); if (tLen > 127) tLen = 127;
                                static char rawTag[256]; strncpy(rawTag, q1 + 1, tLen); rawTag[tLen] = '\0'; UnescapeCString(rawTag, s->text, 128);
                                char* commaAfter = strchr(q2, ',');
                                if (commaAfter && commaAfter < pClose) {
                                    char jChar = 'L'; char* jPtr = commaAfter + 1; while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                    if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                    char* comma4 = strchr(commaAfter + 1, ','); if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                                } else s->useFill = 0;
                            } else if (comma2) {
                                char* comma3 = strchr(comma2 + 1, ','); char* endMarker = comma3 ? comma3 : pClose;
                                if (endMarker) {
                                    int tLen = endMarker - (comma2 + 1); if (tLen > 127) tLen = 127;
                                    static char rawTag[256]; strncpy(rawTag, comma2 + 1, tLen); rawTag[tLen] = '\0';
                                    char* start = rawTag; while(*start && isspace((unsigned char)*start)) start++;
                                    char* end = start + strlen(start) - 1; while(end > start && isspace((unsigned char)*end)) *end-- = '\0';
                                    UnescapeCString(start, s->text, 128);
                                    if (comma3) {
                                        char jChar = 'L'; char* jPtr = comma3 + 1; while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                        if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                        char* comma4 = strchr(comma3 + 1, ','); if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                                    } else s->useFill = 0;
                                }
                            }
                            tmpCount++;
                        }
                    }
                }
                st = strchr(next, ';'); if (!st) st = next + 1;
            }

            parsedIcons[parsedCount].caseId = cId;
            parsedIcons[parsedCount].dimCount = tmpDimCount;
            for(i=0; i<tmpDimCount; i++) parsedIcons[parsedCount].dims[i] = tmpDims[i];
            
            strcpy(parsedIcons[parsedCount].pageName, tmpPageName);
            strcpy(parsedIcons[parsedCount].unitName, tmpUnitName);
            parsedIcons[parsedCount].pageScale = tmpPageScale;
            parsedIcons[parsedCount].gridW = tW;
            parsedIcons[parsedCount].gridH = tH;
            
            if (tmpCount > 0) {
                exact = (Shape*)GlobalAllocPtr(GHND, tmpCount * sizeof(Shape));
                if (exact) { memcpy(exact, dragStartSnapshot, tmpCount * sizeof(Shape)); parsedIcons[parsedCount].shapes = exact; } 
                else parsedIcons[parsedCount].shapes = NULL;
            } else parsedIcons[parsedCount].shapes = NULL;
            parsedIcons[parsedCount].shapeCount = tmpCount; parsedCount++; cur = endBlock;
        }
        GlobalFreePtr(d);

        if (parsedCount > 0) {
            strcpy(loadedCFile, path);
            SetScrollRange(hScrlIcon, SB_CTL, 0, parsedCount - 1, TRUE);
            SwitchToIcon(0); ShowStatus(" C Data File Loaded.");
        } else {
            parsedCount = 1; currentCaseId = 1;
            parsedIcons[0].caseId = 1; strcpy(parsedIcons[0].name, "New");
            parsedIcons[0].shapes = NULL; parsedIcons[0].shapeCount = 0; 
            parsedIcons[0].dimCount = 0;
            parsedIcons[0].gridW = 32; parsedIcons[0].gridH = 32;
            SetScrollRange(hScrlIcon, SB_CTL, 0, 0, TRUE); SwitchToIcon(0);
            ShowStatus(" No valid icons found. Reset to default.");
        }
    } else {
        fclose(f); ShowStatus(" Error: Could not allocate buffer for file.");
        parsedCount = 1; currentCaseId = 1;
        parsedIcons[0].caseId = 1; strcpy(parsedIcons[0].name, "New");
        parsedIcons[0].shapes = NULL; parsedIcons[0].shapeCount = 0; 
        parsedIcons[0].dimCount = 0;
        parsedIcons[0].gridW = 32; parsedIcons[0].gridH = 32;
        SetScrollRange(hScrlIcon, SB_CTL, 0, 0, TRUE); SwitchToIcon(0);
    }
}

void DoSaveFile(HWND hwnd) {
    FILE* f; int i, j;
    static OPENFILENAME ofn; static char szFile[260]; 
    static char savePath[260];
    
    if (loadedCFile[0] != '\0') {
        for (i = 0; i < refCacheCount; i++) {
            if (stricmp(refCache[i].path, loadedCFile) == 0) {
                MessageBox(hwnd, "Cannot overwrite a source reference file currently in use!", "Error", MB_ICONHAND);
                return;
            }
        }
        strcpy(savePath, loadedCFile);
    } else {
        memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
        ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = "C Data Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST; ofn.lpstrDefExt = "c";
        
        if (!GetSaveFileName(&ofn)) return;
        strcpy(savePath, szFile);
        strcpy(loadedCFile, savePath);
    }

    CommitCurrentIcon();
    f = fopen(savePath, "w");
    if (f) {
        for (i = 0; i < parsedCount; i++) {
            Shape* iconShapes = (i == currentIconIdx) ? shapes : parsedIcons[i].shapes;
            int iconShapeCount = (i == currentIconIdx) ? shapeCount : parsedIcons[i].shapeCount;
            int iconDimCount = (i == currentIconIdx) ? dimCount : parsedIcons[i].dimCount;
            Dimension* iconDims = (i == currentIconIdx) ? dims : parsedIcons[i].dims;
            
            fprintf(f, "case %d: {\n", parsedIcons[i].caseId);
            
            fprintf(f, "    PAGE_DEF(\"%s\", %d, %d, \"%s\", \"%s\", %g);\n", 
                    parsedIcons[i].iconTitle[0] ? parsedIcons[i].iconTitle : (parsedIcons[i].pageName[0] ? parsedIcons[i].pageName : "Title"),
                    parsedIcons[i].gridH > 0 ? parsedIcons[i].gridH : 32,
                    parsedIcons[i].gridW > 0 ? parsedIcons[i].gridW : 32,
                    parsedIcons[i].pageName[0] ? parsedIcons[i].pageName : "A4 (210x297)", 
                    parsedIcons[i].unitName[0] ? parsedIcons[i].unitName : "mm",
                    parsedIcons[i].pageScale != 0 ? parsedIcons[i].pageScale : 1.0);

            if (iconShapes) {
                for (j = 0; j < iconShapeCount; j++) {
                    WriteShapeToC(f, &iconShapes[j], j);
                }
            }
            for (j = 0; j < iconDimCount; j++) {
                Dimension* d = &iconDims[j];
                fprintf(f, "    DIMENSION(%d, %d, %d, %d, %g, %g, %d);\n", d->s1, d->p1, d->s2, d->p2, d->offset, d->textPos, d->mode);
            }
            fprintf(f, "    break;\n}\n");
        }
        fclose(f); ShowStatus(" File saved successfully.");
    }
}

#pragma code_seg ( "SVG_TEXT" );

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

    if (s->type == 3) {
        if (n < 5) return 0;
        /* Test against the 4 bounding box corner points (indices 1..4) */
        for (i = 1, j = 4; i <= 4; j = i++) {
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

    if (s->type == 4) {
        if (px >= s->ptsX[0] && px <= s->ptsX[0] + (150.0 / scaleFactor) &&
            py >= s->ptsY[0] && py <= s->ptsY[0] + (30.0 / scaleFactor)) return 1;
        return 0;
    }

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
        ShowWindow(hwnd, SW_HIDE); distEditMode = 0; SetFocus(GetParent(hwnd)); return 0;
    }
    if (msg == WM_KILLFOCUS) {
        if (IsWindowVisible(hwnd) && distEditMode != 0) {
            SendMessage(GetParent(hwnd), WM_APP + 1, 0, 0L);
        }
    }
    return CallWindowProc((FARPROC)oldEditProc, hwnd, msg, wParam, lParam);
}

/* --- GDI Rendering Functions --- */
void DrawGrid(HDC dc) {
    int i; HPEN hPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220)); HPEN hOldPen = SelectObject(dc, hPen);
    double sc = scaleFactor * viewZoom;
    for (i = 0; i <= gridH; i++) { 
        MoveTo(dc, (int)viewPanX, (int)(viewPanY + i * sc)); 
        LineTo(dc, (int)(viewPanX + canvasW_px * viewZoom), (int)(viewPanY + i * sc)); 
    }
    for (i = 0; i <= gridW; i++) { 
        MoveTo(dc, (int)(viewPanX + i * sc), (int)viewPanY); 
        LineTo(dc, (int)(viewPanX + i * sc), (int)(viewPanY + canvasH_px * viewZoom)); 
    }
    SelectObject(dc, hOldPen); DeleteObject(hPen);
}

void DrawPalette(HDC dc) {
    int i, col, row, cx = clientW - PANEL_WIDTH + 15;
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
    TextOut(dc, cx, 466, "Sides:", 6); 
    TextOut(dc, cx, 486, "Depth:", 6);
    TextOut(dc, cx, 506, "Zoom:", 5);
}

void RenderShapes(HDC dc, Shape* sArr, int sCnt, double sc, int offX, int offY, Shape* activeShape, int isActDrawing, int isPreview) {
    int i, j, pass;
    RECT tr;
    char dispT[256];
    int depth = g_RenderRefDepth;

    if (depth > 4) return; /* Safety check */

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i <= sCnt; i++) {
            Shape* s = (i == sCnt) ? (isActDrawing ? activeShape : NULL) : &sArr[i];
            HBRUSH b; HPEN p; HGDIOBJ ob, op; 
            POINT* pA = &g_pA[depth * MAX_POINTS];

            if (!s || (s->ptCount == 0 && s->type != 3 && s->type != 4)) continue;
            
            /* Pass 0: Background REFs. Pass 1: Foreground Polygons, Lines, and Tags */
            if (pass == 0 && s->type != 3) continue;
            if (pass == 1 && s->type == 3) continue;

            for (j = 0; j < s->ptCount; j++) {
                pA[j].x = offX + (int)round(s->ptsX[j] * sc);
                pA[j].y = offY + (int)round(s->ptsY[j] * sc);
            }

            if (s->type == 3) {
                char* refPath = g_refPath[depth]; 
                double refScale = 1.0, refRot = 0.0;
                char *pScale, *pRot, *pEnd; int pathLen;

                if (strncmp(s->text, "{{EXT_REF=", 10) != 0) continue;

                pScale = strstr(s->text, " scale="); pRot = strstr(s->text, " rot="); pEnd = strstr(s->text, "}}");
                if (pScale) pathLen = (int)(pScale - (s->text + 10));
                else if (pEnd) pathLen = (int)(pEnd - (s->text + 10));
                else pathLen = strlen(s->text + 10);

                if (pathLen <= 0 || pathLen >= 260) continue;
                strncpy(refPath, s->text + 10, pathLen); refPath[pathLen] = '\0';
                while (pathLen > 0 && isspace((unsigned char)refPath[pathLen - 1])) refPath[--pathLen] = '\0';

                if (pScale) refScale = atof(pScale + 7);
                if (pRot) refRot = atof(pRot + 5);

                {
                    char* absPath = g_absPath[depth]; 
                    int rIdx;
                    ResolvePath(loadedCFile[0] ? loadedCFile : "", refPath, absPath);
                    if (loadedCFile[0] && stricmp(absPath, loadedCFile) == 0) continue;

                    rIdx = EnsureRefLoaded(absPath);
                    if (rIdx != -1) {
                        double lcx = (refCache[rIdx].minX + refCache[rIdx].maxX) / 2.0;
                        double lcy = (refCache[rIdx].minY + refCache[rIdx].maxY) / 2.0;
                        double objCx = s->ptsX[0] + lcx * refScale;
                        double objCy = s->ptsY[0] + lcy * refScale;
                        double rRad = refRot * PI / 180.0, cosR = cos(rRad), sinR = sin(rRad);
                        double bx[4], by[4]; int r, pIdx; 
                        POINT* boxPts = g_boxPts[depth]; 
                        HPEN hBoxPen; HGDIOBJ oldBoxPen;
                        
                        int currentPt = 6; 
                        int refTagIdx = 0;

                        for (r = 0; r < refCache[rIdx].shapeCount; r++) {
                            Shape* sub = &refCache[rIdx].shapes[r];
                            
                            if (sub->type == 3) {
                                if (g_RenderRefDepth < 3 && strncmp(sub->text, "{{EXT_REF=", 10) == 0) {
                                    Shape* tempRef = &g_tempRef[depth];
                                    *tempRef = *sub;
                                    char* subPath = g_subPath[depth]; 
                                    double subSc = 1.0, subRot = 0.0;
                                    char *spS = strstr(sub->text, " scale="), *spR = strstr(sub->text, " rot="), *spE = strstr(sub->text, "}}");
                                    int sLen;
                                    
                                    if (spS) sLen = (int)(spS - (sub->text + 10)); else if (spE) sLen = (int)(spE - (sub->text + 10)); else sLen = strlen(sub->text + 10);
                                    if (sLen > 0 && sLen < 260) {
                                        strncpy(subPath, sub->text + 10, sLen); subPath[sLen] = '\0';
                                        while (sLen > 0 && isspace((unsigned char)subPath[sLen - 1])) subPath[--sLen] = '\0';
                                        if (spS) subSc = atof(spS + 7);
                                        if (spR) subRot = atof(spR + 5);
                                        
                                        {
                                            double dx = (sub->ptsX[0] - lcx) * refScale;
                                            double dy = (sub->ptsY[0] - lcy) * refScale;
                                            tempRef->ptsX[0] = objCx + dx * cosR - dy * sinR;
                                            tempRef->ptsY[0] = objCy + dx * sinR + dy * cosR;
                                            
                                            if (strlen(subPath) > 80) subPath[80] = '\0';
                                            sprintf(tempRef->text, "{{EXT_REF=%s scale=%.2f rot=%.2f}}", subPath, subSc * refScale, subRot + refRot);
                                            
                                            g_RenderRefDepth++;
                                            RenderShapes(dc, tempRef, 1, sc, offX, offY, NULL, 0, isPreview);
                                            g_RenderRefDepth--;
                                        }
                                    }
                                }
                                continue;
                            }

                            if (sub->type == 4) {
                                double dx = (sub->ptsX[0] - lcx) * refScale;
                                double dy = (sub->ptsY[0] - lcy) * refScale;
                                double wx = objCx + dx * cosR - dy * sinR;
                                double wy = objCy + dx * sinR + dy * cosR;
                                
                                if (!isPreview) {
                                    char* tagBuf = g_tagBuf[depth]; 
                                    RECT rTag = {0, 0, 0, 0};
                                    int tx = offX + (int)round(wx * sc);
                                    int ty = offY + (int)round(wy * sc);
                                    
                                    char* instVal = g_instVal[depth];
                                    GetPipeValue(s->tagData, refTagIdx, instVal, 128);
                                    if (strlen(instVal) > 0) { strncpy(tagBuf, instVal, 127); tagBuf[127] = '\0'; } 
                                    else { strncpy(tagBuf, sub->text, 127); tagBuf[127] = '\0'; }

                                    if (tagBuf[0] == '{' && tagBuf[1] == '{') {
                                        char cleanTag[128]; strcpy(cleanTag, tagBuf + 2);
                                        char *pEndClean = strstr(cleanTag, "}}"); if (pEndClean) *pEndClean = '\0';
                                        strcpy(tagBuf, cleanTag);
                                    }

                                    if (strcmp(tagBuf, "SHEETSCALE") == 0) {
                                        if (activePageScale > 0 && activePageScale <= 1.0) sprintf(tagBuf, "1:%g", 1.0 / activePageScale);
                                        else sprintf(tagBuf, "%g", activePageScale);
                                    }

                                    int fSize = sub->fontSize > 0 ? sub->fontSize : 24;
                                    int fH = (int)(fSize * refScale * (sc / 10.0)); if (fH < 2) fH = 2;
                                    HFONT hFont = CreateFont(fH, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
                                    HGDIOBJ oldFont = SelectObject(dc, hFont);

                                    DrawText(dc, tagBuf, -1, &rTag, DT_CALCRECT | DT_NOPREFIX);
                                    int tw = rTag.right - rTag.left; int th = rTag.bottom - rTag.top;
                                    int dtFlags = DT_NOPREFIX;
                                    if (sub->useFill == 1) { rTag.left = tx - tw/2; rTag.right = tx + tw/2; dtFlags |= DT_CENTER; }
                                    else if (sub->useFill == 2) { rTag.left = tx - tw; rTag.right = tx; dtFlags |= DT_RIGHT; }
                                    else { rTag.left = tx; rTag.right = tx + tw; dtFlags |= DT_LEFT; }
                                    rTag.top = ty; rTag.bottom = ty + th;
                                    
                                    SetTextColor(dc, sub->stroke); SetBkMode(dc, TRANSPARENT); DrawText(dc, tagBuf, -1, &rTag, dtFlags);
                                    SelectObject(dc, oldFont); DeleteObject(hFont);

                                    if (textHitCount < 128 && sArr == shapes) {
                                        textHits[textHitCount].shapeIdx = i; textHits[textHitCount].isRef = 1; textHits[textHitCount].tagIdx = refTagIdx; textHits[textHitCount].box = rTag; textHitCount++;
                                    }
                                }
                                if (currentPt < MAX_POINTS) { s->ptsX[currentPt] = wx; s->ptsY[currentPt] = wy; currentPt++; }
                                refTagIdx++;
                            } else {
                                POINT* subPts = &g_subPts[depth * MAX_POINTS];
                                int subCount = (sub->ptCount > MAX_POINTS) ? MAX_POINTS : sub->ptCount;

                                for (pIdx = 0; pIdx < subCount; pIdx++) {
                                    double dx = (sub->ptsX[pIdx] - lcx) * refScale; double dy = (sub->ptsY[pIdx] - lcy) * refScale;
                                    double wx = objCx + (dx * cosR - dy * sinR); double wy = objCy + (dx * sinR + dy * cosR);
                                    subPts[pIdx].x = offX + (int)round(wx * sc); subPts[pIdx].y = offY + (int)round(wy * sc);
                                    if (currentPt < MAX_POINTS) { s->ptsX[currentPt] = wx; s->ptsY[currentPt] = wy; currentPt++; }
                                }

                                b = sub->useFill ? CreateSolidBrush(sub->fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
                                p = sub->useStroke ? CreatePen(PS_SOLID, sub->strokeWidth > 0 ? sub->strokeWidth : 1, sub->stroke) : (HPEN)GetStockObject(NULL_PEN);
                                ob = SelectObject(dc, b); op = SelectObject(dc, p);

                                if (sub->type == 0 && subCount >= 3) { SetPolyFillMode(dc, ALTERNATE); Polygon(dc, subPts, subCount); } 
                                else if (subCount >= 2) { Polyline(dc, subPts, subCount); }

                                SelectObject(dc, ob); SelectObject(dc, op);
                                if (sub->useFill) DeleteObject(b); if (sub->useStroke) DeleteObject(p);
                            }
                        }

                        if (!isPreview && refCache[rIdx].dimCount > 0) {
                            int dIdx;
                            HPEN hDimPen = CreatePen(PS_SOLID, 1, RGB(0, 128, 255)); HBRUSH hDimBr = CreateSolidBrush(RGB(0, 128, 255));
                            HFONT hDimFont = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
                            HGDIOBJ oldPen2 = SelectObject(dc, hDimPen), oldBr2 = SelectObject(dc, hDimBr), oldFont2 = SelectObject(dc, hDimFont);
                            SetTextColor(dc, RGB(0, 128, 255)); SetBkMode(dc, TRANSPARENT);

                            for (dIdx = 0; dIdx < refCache[rIdx].dimCount; dIdx++) {
                                Dimension* d = &refCache[rIdx].dims[dIdx];
                                if (d->s1 < refCache[rIdx].shapeCount && d->s2 < refCache[rIdx].shapeCount) {
                                    double lA1x = refCache[rIdx].shapes[d->s1].ptsX[d->p1]; double lA1y = refCache[rIdx].shapes[d->s1].ptsY[d->p1];
                                    double lA2x = refCache[rIdx].shapes[d->s2].ptsX[d->p2]; double lA2y = refCache[rIdx].shapes[d->s2].ptsY[d->p2];
                                    double dX = lA2x - lA1x, dY = lA2y - lA1y, val, sOffset = d->offset * sc, dx_r, dy_r, gA1x, gA1y, gA2x, gA2y;
                                    int sA1x, sA1y, sA2x, sA2y, sD1x, sD1y, sD2x, sD2y, sMidX, sMidY;

                                    dx_r = (lA1x - lcx) * refScale; dy_r = (lA1y - lcy) * refScale;
                                    gA1x = objCx + (dx_r * cosR - dy_r * sinR); gA1y = objCy + (dx_r * sinR + dy_r * cosR);
                                    dx_r = (lA2x - lcx) * refScale; dy_r = (lA2y - lcy) * refScale;
                                    gA2x = objCx + (dx_r * cosR - dy_r * sinR); gA2y = objCy + (dx_r * sinR + dy_r * cosR);

                                    sA1x = offX + (int)round(gA1x * sc); sA1y = offY + (int)round(gA1y * sc);
                                    sA2x = offX + (int)round(gA2x * sc); sA2y = offY + (int)round(gA2y * sc);

                                    if (d->mode == 0) {
                                        double sDX = sA2x - sA1x, sDY = sA2y - sA1y, ang = atan2(sDY, sDX), nX = -sin(ang), nY = cos(ang);
                                        sD1x = sA1x + (int)round(nX * sOffset); sD1y = sA1y + (int)round(nY * sOffset); sD2x = sA2x + (int)round(nX * sOffset); sD2y = sA2y + (int)round(nY * sOffset);
                                        val = sqrt(dX*dX + dY*dY) * refScale;
                                    } else if (d->mode == 1) {
                                        sD1x = sA1x; sD1y = sA1y + (int)round(sOffset); sD2x = sA2x; sD2y = sA1y + (int)round(sOffset); val = fabs(dX) * refScale;
                                    } else {
                                        sD1x = sA1x + (int)round(sOffset); sD1y = sA1y; sD2x = sA1x + (int)round(sOffset); sD2y = sA2y; val = fabs(dY) * refScale;
                                    }

                                    {
                                        double L1dx = sD1x - sA1x, L1dy = sD1y - sA1y, L1len = sqrt(L1dx*L1dx + L1dy*L1dy);
                                        if (L1len > 5.0) { MoveTo(dc, sA1x + (int)round(L1dx/L1len*5.0), sA1y + (int)round(L1dy/L1len*5.0)); LineTo(dc, sD1x + (int)round(L1dx/L1len*2.0), sD1y + (int)round(L1dy/L1len*2.0)); }
                                        double L2dx = sD2x - sA2x, L2dy = sD2y - sA2y, L2len = sqrt(L2dx*L2dx + L2dy*L2dy);
                                        if (L2len > 5.0) { MoveTo(dc, sA2x + (int)round(L2dx/L2len*5.0), sA2y + (int)round(L2dy/L2len*5.0)); LineTo(dc, sD2x + (int)round(L2dx/L2len*2.0), sD2y + (int)round(L2dy/L2len*2.0)); }
                                    }
                                    MoveTo(dc, sD1x, sD1y); LineTo(dc, sD2x, sD2y);
                                    {
                                        double dimDx = sD2x - sD1x, dimDy = sD2y - sD1y, dimLen = sqrt(dimDx*dimDx + dimDy*dimDy);
                                        if (dimLen > 0) {
                                            double dirX = dimDx/dimLen, dirY = dimDy/dimLen; POINT pts[3];
                                            pts[0].x = sD1x; pts[0].y = sD1y; pts[1].x = sD1x + (int)round(dirX*10 - dirY*3); pts[1].y = sD1y + (int)round(dirY*10 + dirX*3); pts[2].x = sD1x + (int)round(dirX*10 + dirY*3); pts[2].y = sD1y + (int)round(dirY*10 - dirX*3); Polygon(dc, pts, 3);
                                            pts[0].x = sD2x; pts[0].y = sD2y; pts[1].x = sD2x - (int)round(dirX*10 - dirY*3); pts[1].y = sD2y - (int)round(dirY*10 + dirX*3); pts[2].x = sD2x - (int)round(dirX*10 + dirY*3); pts[2].y = sD2y - (int)round(dirY*10 - dirX*3); Polygon(dc, pts, 3);
                                        }
                                    }

                                    sMidX = sD1x + (int)round((sD2x - sD1x) * d->textPos); sMidY = sD1y + (int)round((sD2y - sD1y) * d->textPos);
                                    char buf[64]; FormatDimension(val, activeUnitName, buf);
                                    DWORD ext = GetTextExtent(dc, buf, strlen(buf)); int tW = LOWORD(ext), tH = HIWORD(ext), tx = sMidX - tW/2, ty = sMidY - tH/2;
                                    RECT tR; tR.left = tx - 2; tR.top = ty - 2; tR.right = tx + tW + 2; tR.bottom = ty + tH + 2;
                                    FillRect(dc, &tR, (HBRUSH)GetStockObject(WHITE_BRUSH)); TextOut(dc, tx, ty, buf, strlen(buf));
                                }
                            }
                            SelectObject(dc, oldPen2); DeleteObject(hDimPen); SelectObject(dc, oldBr2); DeleteObject(hDimBr); SelectObject(dc, oldFont2); DeleteObject(hDimFont);
                        }

                        bx[0] = refCache[rIdx].minX; by[0] = refCache[rIdx].minY; bx[1] = refCache[rIdx].maxX; by[1] = refCache[rIdx].minY;
                        bx[2] = refCache[rIdx].maxX; by[2] = refCache[rIdx].maxY; bx[3] = refCache[rIdx].minX; by[3] = refCache[rIdx].maxY;

                        for (r = 0; r < 4; r++) {
                            double dx = (bx[r] - lcx) * refScale; double dy = (by[r] - lcy) * refScale;
                            s->ptsX[r + 1] = objCx + (dx * cosR - dy * sinR); s->ptsY[r + 1] = objCy + (dx * sinR + dy * cosR);
                        }
                        s->ptsX[5] = objCx; s->ptsY[5] = objCy; s->ptCount = currentPt;

                        if (!isPreview && (currentMode == 27 || (sArr == shapes && ptSelected[i][0]))) {
                            if (sArr == shapes && ptSelected[i][0]) hBoxPen = CreatePen(PS_DOT, 1, RGB(255, 0, 0)); else hBoxPen = CreatePen(PS_DOT, 1, RGB(160, 160, 160));
                            oldBoxPen = SelectObject(dc, hBoxPen);
                            for (r = 0; r < 4; r++) { boxPts[r].x = offX + (int)round(s->ptsX[r + 1] * sc); boxPts[r].y = offY + (int)round(s->ptsY[r + 1] * sc); }
                            boxPts[4] = boxPts[0]; Polyline(dc, boxPts, 5);
                            SelectObject(dc, oldBoxPen); DeleteObject(hBoxPen);
                        }
                    }
                }
                continue;
            }

            if (s->type == 4) {
                if (!isPreview) {
                    if (s->text[0] == '{' && s->text[1] == '{') { strcpy(dispT, s->text + 2); char *pEnd = strstr(dispT, "}}"); if (pEnd) *pEnd = '\0'; } 
                    else { strcpy(dispT, s->text); }

                    if (strcmp(dispT, "SHEETSCALE") == 0) {
                        if (activePageScale > 0 && activePageScale <= 1.0) sprintf(dispT, "1:%g", 1.0 / activePageScale);
                        else sprintf(dispT, "%g", activePageScale);
                    }

                    int fSize = s->fontSize > 0 ? s->fontSize : 24; int fH = (int)(fSize * (sc / 10.0)); if (fH < 2) fH = 2;
                    HFONT hFont = CreateFont(fH, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
                    HGDIOBJ oldFont = SelectObject(dc, hFont);

                    RECT rCalc = {0, 0, 0, 0}; DrawText(dc, dispT, -1, &rCalc, DT_CALCRECT | DT_NOPREFIX);
                    int tw = rCalc.right - rCalc.left, th = rCalc.bottom - rCalc.top, tx = pA[0].x, ty = pA[0].y, dtFlags = DT_NOPREFIX;
                    
                    if (s->useFill == 1) { tr.left = tx - tw/2; tr.right = tx + tw/2; dtFlags |= DT_CENTER; }
                    else if (s->useFill == 2) { tr.left = tx - tw; tr.right = tx; dtFlags |= DT_RIGHT; }
                    else { tr.left = tx; tr.right = tx + tw; dtFlags |= DT_LEFT; }
                    tr.top = ty; tr.bottom = ty + th;
                    
                    SetTextColor(dc, s->stroke); SetBkMode(dc, TRANSPARENT); DrawText(dc, dispT, -1, &tr, dtFlags);
                    SelectObject(dc, oldFont); DeleteObject(hFont);

                    if (textHitCount < 128 && sArr == shapes) { textHits[textHitCount].shapeIdx = i; textHits[textHitCount].isRef = 0; textHits[textHitCount].tagIdx = -1; textHits[textHitCount].box = tr; textHitCount++; }
                }
                continue;
            }

            b = s->useFill ? CreateSolidBrush(s->fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
            p = s->useStroke ? CreatePen(PS_SOLID, s->strokeWidth > 0 ? s->strokeWidth : 1, s->stroke) : (HPEN)GetStockObject(NULL_PEN);
            ob = SelectObject(dc, b); op = SelectObject(dc, p);

            if (s->type == 0) { SetPolyFillMode(dc, ALTERNATE); Polygon(dc, pA, s->ptCount); } 
            else { Polyline(dc, pA, s->ptCount); }

            SelectObject(dc, ob); SelectObject(dc, op);
            if (s->useFill) DeleteObject(b); if (s->useStroke) DeleteObject(p);
        }
    }
}

void DrawPreview(HDC dc) {
    static HBITMAP hbmPrev = NULL;
    static HDC memDC = NULL;
    int cx = clientW - PANEL_WIDTH + 15;
    int px = cx + 220, py = 420; 
    
    if (!memDC) {
        memDC = CreateCompatibleDC(dc);
        hbmPrev = CreateCompatibleBitmap(dc, 34, 34);
        SelectObject(memDC, hbmPrev);
    }
    
    if (previewDirty) {
        RECT r; r.left = 0; r.top = 0; r.right = 34; r.bottom = 34;
        FillRect(memDC, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        FrameRect(memDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        
        if (shapes) {
            double pScaleX = 32.0 / gridW;
            double pScaleY = 32.0 / gridH;
            double pScale = fmin(pScaleX, pScaleY);
            RenderShapes(memDC, shapes, shapeCount, pScale, 1, 1, NULL, 0, 1);
        }
        previewDirty = 0;
    }
    
    BitBlt(dc, px, py, 34, 34, memDC, 0, 0, SRCCOPY);
}

void DrawNodes(HDC dc) {
    int j, i, px, py;
    HBRUSH hSelBr = CreateSolidBrush(RGB(255, 0, 0)); HBRUSH hUnselBr = CreateSolidBrush(RGB(0, 0, 255));
    HBRUSH hHovBr = CreateSolidBrush(RGB(255, 255, 0)); HBRUSH hEdgeBr = CreateSolidBrush(RGB(0, 255, 255));
    HBRUSH hOrigBr = (HBRUSH)SelectObject(dc, hUnselBr); HPEN hOrigPen = (HPEN)SelectObject(dc, GetStockObject(BLACK_PEN));

    if (shapes) {
        for (i = 0; i < shapeCount; i++) {
            if (shapes[i].type == 3 && currentMode != 27) continue;

            int drawNodes = (currentMode == 27) ? 1 : (i == selectedShape || i == hoverShape);
            for (j = 0; j < shapes[i].ptCount; j++) if (ptSelected[i][j]) drawNodes = 1;
            
            if (drawNodes) {
                int startPt = (shapes[i].type == 3) ? 0 : 0;
                int endPt = shapes[i].ptCount;
                for (j = startPt; j < endPt; j++) {
                    px = (int)round(shapes[i].ptsX[j] * (scaleFactor * viewZoom) + viewPanX); 
                    py = (int)round(shapes[i].ptsY[j] * (scaleFactor * viewZoom) + viewPanY);
                    if (ptSelected[i][j] || (shapes[i].type != 3 && ptSelected[i][0])) SelectObject(dc, hSelBr); 
                    else if (hoverShape == i && hoverPt == j) SelectObject(dc, hHovBr);
                    else SelectObject(dc, hUnselBr);
                    Rectangle(dc, px - 3, py - 3, px + 4, py + 4);
                }
            }
        }
        if (hoverSegShape != -1) {
            SelectObject(dc, hEdgeBr);
            int eX = (int)(hoverProjX * (scaleFactor * viewZoom) + viewPanX);
            int eY = (int)(hoverProjY * (scaleFactor * viewZoom) + viewPanY);
            Ellipse(dc, eX - 4, eY - 4, eX + 5, eY + 5);
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
        double scOffset;
        char buf[64];
        
        if (dims[i].s1 < 0 || dims[i].s1 >= shapeCount || dims[i].s2 < 0 || dims[i].s2 >= shapeCount) continue;
        if (shapes[dims[i].s1].type != 3 && dims[i].p1 >= shapes[dims[i].s1].ptCount) continue;
        if (shapes[dims[i].s2].type != 3 && dims[i].p2 >= shapes[dims[i].s2].ptCount) continue;

        A1x = shapes[dims[i].s1].ptsX[dims[i].p1] * (scaleFactor * viewZoom) + viewPanX;
        A1y = shapes[dims[i].s1].ptsY[dims[i].p1] * (scaleFactor * viewZoom) + viewPanY;
        A2x = shapes[dims[i].s2].ptsX[dims[i].p2] * (scaleFactor * viewZoom) + viewPanX;
        A2y = shapes[dims[i].s2].ptsY[dims[i].p2] * (scaleFactor * viewZoom) + viewPanY;
        
        dx = A2x - A1x; dy = A2y - A1y;
        scOffset = dims[i].offset * (scaleFactor * viewZoom);
        
        if (dims[i].mode == 0) { 
            ang = atan2(dy, dx);
            nx = -sin(ang); ny = cos(ang);
            D1x = A1x + nx * scOffset; D1y = A1y + ny * scOffset;
            D2x = A2x + nx * scOffset; D2y = A2y + ny * scOffset;
            val = sqrt(pow(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1], 2) + pow(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1], 2));
        } else if (dims[i].mode == 1) { 
            D1x = A1x; D1y = A1y + scOffset;
            D2x = A2x; D2y = A1y + scOffset;
            val = fabs(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1]);
        } else { 
            D1x = A1x + scOffset; D1y = A1y;
            D2x = A1x + scOffset; D2y = A2y;
            val = fabs(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1]);
        }
        
        FormatDimension(val, activeUnitName, buf);

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

        MoveTo(dc, (int)D1x, (int)D1y); LineTo(dc, (int)D2x, (int)D2y);
        
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
            
            FillRect(dc, &tR, (HBRUSH)GetStockObject(WHITE_BRUSH));
            TextOut(dc, tx, ty, buf, strlen(buf));
            
            hx = tx + tW + 8;
            hy = (int)midY;
            
            SelectObject(dc, hHandlePen);
            SelectObject(dc, hHandleBr);
            Rectangle(dc, hx - 4, hy - 4, hx + 5, hy + 5);
            
            SelectObject(dc, hPen);
            SelectObject(dc, hBr);
        }
    }
    
    SelectObject(dc, oldPen); DeleteObject(hPen); DeleteObject(hHandlePen);
    SelectObject(dc, oldBr); DeleteObject(hBr); DeleteObject(hHandleBr);
    SelectObject(dc, oldFont); DeleteObject(hFont);
}
void WriteShapeToC(FILE* f, Shape* s, int j) {
    int k; char escV[512];
    if (s->type == 3) {
        if (strlen(s->tagData) > 0) {
            EscapeCString(s->tagData, escV, 512);
            fprintf(f, "    EXT_REF(%g, %g, \"%s\", \"%s\");\n", s->ptsX[0], s->ptsY[0], s->text, escV);
        } else {
            fprintf(f, "    EXT_REF(%g, %g, \"%s\");\n", s->ptsX[0], s->ptsY[0], s->text);
        }
        return;
    } else if (s->type == 4) {
        char jChar = (s->useFill == 1) ? 'C' : ((s->useFill == 2) ? 'R' : 'L');
        EscapeCString(s->text, escV, 512);
        fprintf(f, "    TAG_TEXT(%g, %g, \"%s\", %c, %d);\n", s->ptsX[0], s->ptsY[0], escV, jChar, s->fontSize > 0 ? s->fontSize : 24);
        return;
    }
    
    if (s->useFill || s->useStroke) {
        if (s->useFill) fprintf(f, "        HBRUSH hBr = CreateSolidBrush(RGB(%d,%d,%d)); HGDIOBJ oBr = SelectObject(hdc, hBr);\n", (int)(s->fill & 0xFF), (int)((s->fill >> 8) & 0xFF), (int)((s->fill >> 16) & 0xFF));
        if (s->useStroke) fprintf(f, "        HPEN hPen = CreatePen(PS_SOLID, %d, RGB(%d,%d,%d)); HGDIOBJ oPen = SelectObject(hdc, hPen);\n", s->strokeWidth > 0 ? s->strokeWidth : 1, (int)(s->stroke & 0xFF), (int)((s->stroke >> 8) & 0xFF), (int)((s->stroke >> 16) & 0xFF));
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
    }
}

#pragma code_seg ( "WND_TEXT" );
LRESULT FAR PASCAL _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static double dragStartX = 0, dragStartY = 0;
    static double exactDragStartX = 0, exactDragStartY = 0;
    static double shapeCx = 0, shapeCy = 0;
    static int dragType = 0;
    static int isDraggingPoint = 0;

    switch (msg) {
        case WM_CREATE: {
            int i; int allocFailed = 0;
            hMain = hwnd;
            DragAcceptFiles(hwnd, TRUE);

            shapes = (Shape*)GlobalAllocPtr(GHND, (DWORD)MAX_SHAPES * sizeof(Shape));
            dragStartSnapshot = (Shape*)GlobalAllocPtr(GHND, (DWORD)MAX_SHAPES * sizeof(Shape));
            
            g_tempRef = (Shape*)GlobalAllocPtr(GHND, 5 * sizeof(Shape));
            g_subPts = (POINT*)GlobalAllocPtr(GHND, 5 * MAX_POINTS * sizeof(POINT));
            g_pA = (POINT*)GlobalAllocPtr(GHND, 5 * MAX_POINTS * sizeof(POINT));

            for(i=0; i<MAX_UNDO; i++) {
                history[i] = (Shape*)GlobalAllocPtr(GHND, (DWORD)MAX_SHAPES * sizeof(Shape));
                if (!history[i]) allocFailed = 1;
            }
            
            parsedIcons = (IconDef*)GlobalAllocPtr(GHND, (DWORD)MAX_ICONS * sizeof(IconDef));
            refCache = (RefCache*)GlobalAllocPtr(GHND, (DWORD)MAX_REFS * sizeof(RefCache));
            editMap = (TagEditMap*)GlobalAllocPtr(GHND, (DWORD)MAX_EDIT_TAGS * sizeof(TagEditMap));

            if (!shapes || !dragStartSnapshot || allocFailed || !parsedIcons || !refCache || !editMap || !g_tempRef || !g_subPts || !g_pA) {
                MessageBox(hwnd, "Failed to allocate memory on Far Heap!", "Error", MB_ICONHAND);
                PostQuitMessage(0); return -1;
            }

            InitFont();

            parsedCount = 1; currentIconIdx = -1; currentCaseId = 1;
            parsedIcons[0].caseId = 1; strcpy(parsedIcons[0].name, "New");
            parsedIcons[0].shapes = NULL; parsedIcons[0].shapeCount = 0; parsedIcons[0].dimCount = 0; parsedIcons[0].tCount = 0;
            strcpy(parsedIcons[0].pageName, "A4 (210x297)");
            strcpy(parsedIcons[0].unitName, "mm");
            parsedIcons[0].pageScale = 1.0;
            parsedIcons[0].gridW = 32; parsedIcons[0].gridH = 32;

            hStatus = CreateWindow("STATIC", " Ready", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)100, hInst, NULL);
            hScrlSides = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)153, hInst, NULL);
            hScrlDepth = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)154, hInst, NULL);
            hScrlZoom  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)158, hInst, NULL);
            
            hBtnAddIcon = CreateWindow("BUTTON", "+", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)151, hInst, NULL);
            hScrlIcon  = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)155, hInst, NULL);
            hBtnDelIcon = CreateWindow("BUTTON", "-", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)152, hInst, NULL);
            
            hBtnThickPlus = CreateWindow("BUTTON", "+", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)156, hInst, NULL);
            hBtnThickMinus = CreateWindow("BUTTON", "-", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)157, hInst, NULL);

            SetScrollRange(hScrlSides, SB_CTL, -5, 5, FALSE); SetScrollPos(hScrlSides, SB_CTL, 0, TRUE);
            SetScrollRange(hScrlDepth, SB_CTL, -5, 5, FALSE); SetScrollPos(hScrlDepth, SB_CTL, 0, TRUE);
            SetScrollRange(hScrlIcon,  SB_CTL, 0, 0, FALSE); SetScrollPos(hScrlIcon, SB_CTL, 0, TRUE);
            SetScrollRange(hScrlZoom,  SB_CTL, 1, 50, FALSE); SetScrollPos(hScrlZoom, SB_CTL, 10, TRUE);
            
            SwitchToIcon(0); 
            
            for(i = 0; i < 33; i++) hBtn[i] = CreateWindow("BUTTON", bT[i], WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)(200+i), hInst, NULL);
            
            hDistEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 60, 20, hwnd, (HMENU)300, hInst, NULL);
            subclassThunk = MakeProcInstance((FARPROC)DistEditProc, hInst);
            oldEditProc = (FARPROC)SetWindowLong(hDistEdit, GWL_WNDPROC, (LONG)subclassThunk);

            hMultiEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL, 0, 0, 150, 60, hwnd, (HMENU)301, hInst, NULL);
            multiSubclassThunk = MakeProcInstance((FARPROC)MultiEditProc, hInst);
            oldMultiEditProc = (FARPROC)SetWindowLong(hMultiEdit, GWL_WNDPROC, (LONG)multiSubclassThunk);
            break;
        }
        case WM_DROPFILES: {
            char filePath[260];
            HDROP hDrop = (HDROP)wParam;
            if (DragQueryFile(hDrop, 0, filePath, 260)) {
                ProcessDropFile(filePath, hwnd);
            }
            DragFinish(hDrop);
            break;
        }
        case WM_SIZE: {
            int cx, w, by, i, availW, availH;
            if (!hScrlIcon || !hStatus || wParam == SIZE_MINIMIZED) return DefWindowProc(hwnd, msg, wParam, lParam);
            
            clientW = LOWORD(lParam); clientH = HIWORD(lParam); 
            availW = clientW - PANEL_WIDTH; availH = clientH - 100;
            if (availW < 10) availW = 10; if (availH < 10) availH = 10;
            
            scaleFactor = (availW / gridW < availH / gridH) ? (availW / gridW) : (availH / gridH);
            if (scaleFactor < 1) scaleFactor = 1; 
            
            canvasW_px = scaleFactor * gridW; canvasH_px = scaleFactor * gridH;
            cx = clientW - PANEL_WIDTH + 15; w = PANEL_WIDTH - 30; by = 10;
            
            for(i = 0; i < 33; i++) {
                if (hBtn[i]) MoveWindow(hBtn[i], cx + (i%3)*(w/3 + 2), by + (i/3)*26, (w/3)-4, 24, TRUE);
            }
            MoveWindow(hScrlSides, cx + 50, 466, w - 50, 18, TRUE);
            MoveWindow(hScrlDepth, cx + 50, 486, w - 50, 18, TRUE);
            MoveWindow(hScrlZoom,  cx + 50, 506, w - 50, 18, TRUE);
            MoveWindow(hBtnAddIcon, cx, 526, 24, 20, TRUE);
            MoveWindow(hScrlIcon, cx + 26, 526, w - 54, 20, TRUE);
            MoveWindow(hBtnDelIcon, cx + w - 26, 526, 24, 20, TRUE);
            MoveWindow(hStatus, 0, clientH - 20, clientW, 20, TRUE);
            MoveWindow(hBtnThickPlus, cx + 190, 420, 24, 16, TRUE);
            MoveWindow(hBtnThickMinus, cx + 190, 437, 24, 16, TRUE);

            InvalidateRect(hwnd, NULL, TRUE); break;
        }
        case WM_HSCROLL: {
            HWND hTrk = (HWND)(UINT)HIWORD(lParam); UINT code = wParam; int pos, p;
            double minX, maxX, minY, maxY, cx, cy, rx, ry; int sides;
            
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
                if (pos < 0) pos = 0; if (parsedCount > 0 && pos >= parsedCount) pos = parsedCount - 1;
                SwitchToIcon(pos);
            }
            else if (hTrk == hScrlZoom) {
                pos = GetScrollPos(hScrlZoom, SB_CTL);
                if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) { pos = LOWORD(lParam); }
                else if (code == SB_LINELEFT) pos--; 
                else if (code == SB_LINERIGHT) pos++; 
                else if (code == SB_PAGELEFT) pos-=5; 
                else if (code == SB_PAGERIGHT) pos+=5;
                if (pos < 1) pos = 1; if (pos > 50) pos = 50;
                SetScrollPos(hScrlZoom, SB_CTL, pos, TRUE);
                viewZoom = pos / 10.0;
                RedrawCanvas(hwnd);
            }
            else if (hTrk == hScrlSides || hTrk == hScrlDepth) {
                pos = GetScrollPos(hTrk, SB_CTL);
                if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) { pos = (int)(short)LOWORD(lParam); } 
                else if (code == SB_LINELEFT) { pos = -1; } 
                else if (code == SB_LINERIGHT) { pos = 1; } 
                else if (code == SB_PAGELEFT) { pos = -3; } 
                else if (code == SB_PAGERIGHT) { pos = 3; } 
                else { pos = 0; }

                if (pos != 0) {
                    if (hTrk == hScrlSides) {
                        paramSides += pos; if (paramSides < 3) paramSides = 3; if (paramSides > 32) paramSides = 32;
                        if (selectedShape != -1 && shapes[selectedShape].type == 0) {
                            SaveState();
                            minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                            for (p = 0; p < shapes[selectedShape].ptCount; p++) {
                                minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]);
                                minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]);
                            }
                            cx = (minX + maxX) / 2.0; cy = (minY + maxY) / 2.0;
                            rx = (maxX - minX) / 2.0; if (rx < 1) rx = 1; ry = (maxY - minY) / 2.0; if (ry < 1) ry = 1;
                            shapes[selectedShape].ptCount = paramSides;
                            for (p = 0; p < paramSides; p++) {
                                double ang = p * (2.0 * PI / paramSides) - (PI / 2.0);
                                double rF = (paramSides > 4 && paramStar < 100 && p % 2 != 0) ? fmax(0.2, paramStar / 100.0) : 1.0;
                                shapes[selectedShape].ptsX[p] = cx + cos(ang) * rx * rF; shapes[selectedShape].ptsY[p] = cy + sin(ang) * ry * rF;
                            }
                            RedrawCanvas(hwnd);
                        }
                    } else if (hTrk == hScrlDepth) {
                        if (selectedShape != -1 && shapes[selectedShape].type == 4) {
                            SaveState();
                            shapes[selectedShape].fontSize += pos;
                            if (shapes[selectedShape].fontSize < 4) shapes[selectedShape].fontSize = 4;
                            if (shapes[selectedShape].fontSize > 144) shapes[selectedShape].fontSize = 144;
                            RedrawCanvas(hwnd);
                        } else {
                            paramStar += pos * 5; if (paramStar < 10) paramStar = 10; if (paramStar > 100) paramStar = 100;
                            if (selectedShape != -1 && shapes[selectedShape].type == 0) {
                                SaveState(); minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                                for (p = 0; p < shapes[selectedShape].ptCount; p++) {
                                    minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]);
                                    minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]);
                                }
                                cx = (minX + maxX) / 2.0; cy = (minY + maxY) / 2.0;
                                rx = (maxX - minX) / 2.0; if (rx < 1) rx = 1; ry = (maxY - minY) / 2.0; if (ry < 1) ry = 1;
                                sides = shapes[selectedShape].ptCount;
                                for (p = 0; p < sides; p++) {
                                    double ang = p * (2.0 * PI / sides) - (PI / 2.0);
                                    double rF = (sides > 4 && paramStar < 100 && p % 2 != 0) ? fmax(0.2, paramStar / 100.0) : 1.0;
                                    shapes[selectedShape].ptsX[p] = cx + cos(ang) * rx * rF; shapes[selectedShape].ptsY[p] = cy + sin(ang) * ry * rF;
                                }
                                RedrawCanvas(hwnd);
                            }
                        }
                    }
                    SetScrollPos(hTrk, SB_CTL, 0, TRUE); UpdateStatusBar();
                }
            }
            break;
        }
        case WM_KEYDOWN: {
            int i, j, k, m;
            if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) { Undo(hwnd); return 0; }
            if (wParam == VK_ESCAPE) { 
                if (distEditMode != 0 && IsWindowVisible(hDistEdit)) { ShowWindow(hDistEdit, SW_HIDE); distEditMode = 0; RedrawCanvas(hwnd); return 0; }
                if (distEditMode != 0 && IsWindowVisible(hMultiEdit)) { ShowWindow(hMultiEdit, SW_HIDE); distEditMode = 0; RedrawCanvas(hwnd); return 0; }
                if (isDrawing) { isDrawing = 0; currentShape.ptCount = 0; }
                if (shapeCount > 0) currentMode = 0; else currentMode = 3; 
                ClearSelection(); RedrawCanvas(hwnd); 
                return 0;
            }
            if (wParam == VK_RETURN) {
                if (currentMode == 0 && selOrderCount == 2) { SendMessage(hwnd, WM_COMMAND, 222, 0); return 0; } 
                if (isDrawing && (currentMode >= 3 && currentMode <= 5) && currentShape.ptCount >= 2) { 
                    currentShape.ptCount--; 
                    if (currentShape.ptCount >= (currentMode == 3 ? 3 : 2)) {
                        SaveState(); shapes[shapeCount++] = currentShape; isDrawing = 0; currentShape.ptCount = 0; selectedShape = shapeCount - 1; currentMode = 0; UpdateStatusBar(); RedrawCanvas(hwnd); 
                    } else {
                        isDrawing = 0; currentShape.ptCount = 0; currentMode = 0; RedrawCanvas(hwnd);
                    }
                    return 0;
                }
            }
            if (wParam == VK_DELETE || wParam == VK_BACK) {
                if (dragDimIdx != -1) {
                    for(m = dragDimIdx; m < dimCount - 1; m++) {
                        dims[m] = dims[m + 1];
                    }
                    dimCount--;
                    dragDimIdx = -1;
                    ReleaseCapture();
                    RedrawCanvas(hwnd);
                    return 0;
                }

                if (isDrawing && currentShape.ptCount > 0) { 
                    currentShape.ptCount--; if (currentShape.ptCount == 0) isDrawing = 0; RedrawCanvas(hwnd); 
                }
                else if (currentMode == 0 || currentMode == 1 || currentMode == 2 || currentMode == 27) {
                    if (currentMode == 27 && selOrderCount == 2) {
                        ClearSelection();
                        UpdateStatusBar();
                        RedrawCanvas(hwnd);
                        ShowStatus(" Deleting dimensions is disabled.");
                        return 0;
                    }

                    if (selOrderCount > 0) {
                        SaveState();
                        for (i = shapeCount - 1; i >= 0; i--) {
                            if (shapes[i].type == 3 || shapes[i].type == 4) {
                                int delRef = 0;
                                for (k = 0; k < shapes[i].ptCount; k++) if (ptSelected[i][k]) delRef = 1;
                                if (delRef) {
                                    for(j = 0; j < dimCount; ) {
                                        if (dims[j].s1 == i || dims[j].s2 == i) {
                                            for(m = j; m < dimCount - 1; m++) dims[m] = dims[m+1];
                                            dimCount--;
                                        } else {
                                            if (dims[j].s1 > i) dims[j].s1--;
                                            if (dims[j].s2 > i) dims[j].s2--;
                                            j++;
                                        }
                                    }
                                    for(k = i; k < shapeCount - 1; k++) shapes[k] = shapes[k+1]; 
                                    shapeCount--; 
                                }
                                continue;
                            }
                            for (k = shapes[i].ptCount - 1; k >= 0; k--) {
                                if (ptSelected[i][k]) {
                                    for(j = 0; j < dimCount; ) {
                                        if ((dims[j].s1 == i && dims[j].p1 == k) || (dims[j].s2 == i && dims[j].p2 == k)) {
                                            for(m = j; m < dimCount - 1; m++) dims[m] = dims[m+1];
                                            dimCount--;
                                        } else {
                                            if (dims[j].s1 == i && dims[j].p1 > k) dims[j].p1--;
                                            if (dims[j].s2 == i && dims[j].p2 > k) dims[j].p2--;
                                            j++;
                                        }
                                    }
                                    for(j = k; j < shapes[i].ptCount - 1; j++) { shapes[i].ptsX[j] = shapes[i].ptsX[j+1]; shapes[i].ptsY[j] = shapes[i].ptsY[j+1]; }
                                    shapes[i].ptCount--;
                                }
                            }
                            if (shapes[i].ptCount < (shapes[i].type==0?3:2)) { 
                                for(j = 0; j < dimCount; ) {
                                    if (dims[j].s1 == i || dims[j].s2 == i) {
                                        for(m = j; m < dimCount - 1; m++) dims[m] = dims[m+1];
                                        dimCount--;
                                    } else {
                                        if (dims[j].s1 > i) dims[j].s1--;
                                        if (dims[j].s2 > i) dims[j].s2--;
                                        j++;
                                    }
                                }
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
            int hitDimDrag, hitDimCycle;
            int x, y, cx;
            int tagHit;

            x = (int)(short)LOWORD(lParam); 
            y = (int)(short)HIWORD(lParam); 
            cx = clientW - PANEL_WIDTH + 15;
            isPartialSelection = 0;
            hitDimDrag = -1; hitDimCycle = -1;
            tagHit = -1;

            if (x >= cx && x < cx + 256 && y >= 382 && y < 414) {
                currentFill = palette[((y - 382) / 16) * 8 + (x - cx) / 32]; useFill = 1;
                if (selectedShape != -1) { SaveState(); shapes[selectedShape].fill = currentFill; shapes[selectedShape].useFill = 1; }
                InvalidateRect(hwnd, NULL, TRUE); return 0;
            }

            if (currentMode == 7) { 
                dragType = 5; panStartX = x; panStartY = y; 
                SetCapture(hwnd); return 0; 
            }

            if (dimCount > 0) {
                HDC hdc = GetDC(hwnd);
                HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
                HGDIOBJ oldFont = SelectObject(hdc, hFont);

                for (i = 0; i < dimCount; i++) {
                    double A1x, A1y, A2x, A2y, D1x, D1y, D2x, D2y;
                    double midX, midY, val; char buf[64]; DWORD ext; int tW, tH, tx, ty, hx, hy;
                    double scOffset;
                    
                    if (dims[i].s1 >= shapeCount || dims[i].s2 >= shapeCount) continue;
                    if (shapes[dims[i].s1].type != 3 && dims[i].p1 >= shapes[dims[i].s1].ptCount) continue;
                    if (shapes[dims[i].s2].type != 3 && dims[i].p2 >= shapes[dims[i].s2].ptCount) continue;

                    A1x = shapes[dims[i].s1].ptsX[dims[i].p1] * (scaleFactor * viewZoom) + viewPanX; 
                    A1y = shapes[dims[i].s1].ptsY[dims[i].p1] * (scaleFactor * viewZoom) + viewPanY;
                    A2x = shapes[dims[i].s2].ptsX[dims[i].p2] * (scaleFactor * viewZoom) + viewPanX; 
                    A2y = shapes[dims[i].s2].ptsY[dims[i].p2] * (scaleFactor * viewZoom) + viewPanY;
                    
                    scOffset = dims[i].offset * (scaleFactor * viewZoom);

                    if (dims[i].mode == 0) {
                        double dX = A2x - A1x, dY = A2y - A1y;
                        double ang = atan2(dY, dX); double nX = -sin(ang), nY = cos(ang);
                        D1x = A1x + nX * scOffset; D1y = A1y + nY * scOffset;
                        D2x = A2x + nX * scOffset; D2y = A2y + nY * scOffset;
                    } else if (dims[i].mode == 1) {
                        D1x = A1x; D1y = A1y + scOffset; D2x = A2x; D2y = A1y + scOffset;
                    } else {
                        D1x = A1x + scOffset; D1y = A1y; D2x = A1x + scOffset; D2y = A2y;
                    }
                    midX = D1x + (D2x - D1x) * dims[i].textPos; midY = D1y + (D2y - D1y) * dims[i].textPos;
                    
                    FormatDimension(0.0, activeUnitName, buf);
                    ext = GetTextExtent(hdc, buf, strlen(buf));
                    tW = LOWORD(ext); tH = HIWORD(ext); tx = (int)(midX - tW/2.0); ty = (int)(midY - tH/2.0);
                    hx = tx + tW + 8; hy = (int)midY;
                    
                    if (x >= tx - 2 && x <= tx + tW + 2 && y >= ty - 2 && y <= ty + tH + 2) { hitDimDrag = i; break; }
                    if (x >= hx - 4 && x <= hx + 5 && y >= hy - 4 && y <= hy + 5) { hitDimCycle = i; break; }
                }
                SelectObject(hdc, oldFont); DeleteObject(hFont); ReleaseDC(hwnd, hdc);
            }

            if (hitDimDrag != -1) { dragDimIdx = hitDimDrag; SetFocus(hwnd); SetCapture(hwnd); return 0; } 
            else if (hitDimCycle != -1) { dims[hitDimCycle].mode = (dims[hitDimCycle].mode + 1) % 3; RedrawCanvas(hwnd); return 0; }

            SetFocus(hwnd); SetCapture(hwnd);
            exactDragStartX = (x - viewPanX) / (scaleFactor * viewZoom);
            exactDragStartY = (y - viewPanY) / (scaleFactor * viewZoom);
            startX = CLAMP(Snap(exactDragStartX), 0, gridW); 
            startY = CLAMP(Snap(exactDragStartY), 0, gridH);
            gx = startX; gy = startY;

            if (currentMode == 8) { 
                int fillHit = -1; double exactX = exactDragStartX; double exactY = exactDragStartY;
                for (i = shapeCount - 1; i >= 0; i--) { if (shapes[i].type == 0 && PointInPolyShape(exactX, exactY, &shapes[i])) { fillHit = i; break; } }
                if (fillHit != -1) { SaveState(); shapes[fillHit].fill = currentFill; shapes[fillHit].useFill = 1; selectedShape = fillHit; UpdateStatusBar(); RedrawCanvas(hwnd); }
                ReleaseCapture(); return 0;
            }

            for (k = textHitCount - 1; k >= 0; k--) {
                if (x >= textHits[k].box.left && x <= textHits[k].box.right && y >= textHits[k].box.top && y <= textHits[k].box.bottom) {
                    if (!textHits[k].isRef) { tagHit = textHits[k].shapeIdx; break; }
                }
            }

            if (currentMode == 0 || currentMode == 1 || currentMode == 2 || currentMode == 27) { 
                shiftDown = (GetKeyState(VK_SHIFT) & 0x8000); ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000); hitS = -1; hitP = -1; sHasSel = 0; allSel = 1;

                if (tagHit != -1) {
                    hitS = tagHit; hitP = 0;
                    if (shiftDown && shapes[hitS].type == 4) {
                        SaveState(); shapes[hitS].useFill = (shapes[hitS].useFill + 1) % 3; RedrawCanvas(hwnd); return 0;
                    }
                }

                if (hitS == -1) {
                    if (selectedShape != -1 && selectedShape < shapeCount && shapes[selectedShape].type != 3 && shapes[selectedShape].type != 4) {
                        for (j = 0; j < shapes[selectedShape].ptCount; j++) {
                            int px = (int)round(shapes[selectedShape].ptsX[j] * (scaleFactor * viewZoom) + viewPanX);
                            int py = (int)round(shapes[selectedShape].ptsY[j] * (scaleFactor * viewZoom) + viewPanY);
                            if (sqrt(pow(px - x, 2) + pow(py - y, 2)) <= 8.0) { hitS = selectedShape; hitP = j; break; }
                        }
                    }
                    if (hitS == -1) {
                        for (i = shapeCount - 1; i >= 0; i--) {
                            if (shapes[i].type == 3 || shapes[i].type == 4) continue;
                            for (j = 0; j < shapes[i].ptCount; j++) {
                                int px = (int)round(shapes[i].ptsX[j] * (scaleFactor * viewZoom) + viewPanX);
                                int py = (int)round(shapes[i].ptsY[j] * (scaleFactor * viewZoom) + viewPanY);
                                if (sqrt(pow(px - x, 2) + pow(py - y, 2)) <= 8.0) { hitS = i; hitP = j; break; }
                            }
                            if (hitS != -1) break;
                        }
                    }
                }

                if (hitS == -1) {
                    if (selectedShape != -1 && selectedShape < shapeCount && shapes[selectedShape].type == 0) {
                        if (PointInPolyShape(exactDragStartX, exactDragStartY, &shapes[selectedShape])) { hitS = selectedShape; hitP = -1; }
                    }
                    if (hitS == -1) {
                        for (i = shapeCount - 1; i >= 0; i--) { 
                            if (shapes[i].type == 0 && PointInPolyShape(exactDragStartX, exactDragStartY, &shapes[i])) { 
                                hitS = i; hitP = -1; break; 
                            } 
                        } 
                    }
                }

                if (hitS == -1) {
                    double bestDist = 99999.0, d, prX, prY;
                    int foundSegS = -1, foundSegP = -1;
                    double foundPrX = 0, foundPrY = 0;
                    
                    for (i = shapeCount - 1; i >= 0; i--) {
                        if (shapes[i].type == 3 || shapes[i].type == 4 || shapes[i].ptCount < 2) continue;
                        for (j = 0; j < (shapes[i].type == 0 ? shapes[i].ptCount : shapes[i].ptCount - 1); j++) {
                            int np = (j + 1) % shapes[i].ptCount;
                            PtToSegProj((double)exactDragStartX, (double)exactDragStartY, shapes[i].ptsX[j], shapes[i].ptsY[j], shapes[i].ptsX[np], shapes[i].ptsY[np], &prX, &prY, &d);
                            if (d < (10.0 / scaleFactor) && d < bestDist) { 
                                foundSegS = i; foundSegP = j; 
                                foundPrX = prX; foundPrY = prY;
                                bestDist = d; 
                            }
                        }
                    }
                    if (foundSegS != -1) {
                        hitS = foundSegS;
                        if (currentMode == 0 && shapes[foundSegS].ptCount < MAX_POINTS) {
                            SaveState();
                            for (k = shapes[foundSegS].ptCount; k > foundSegP + 1; k--) {
                                shapes[foundSegS].ptsX[k] = shapes[foundSegS].ptsX[k-1];
                                shapes[foundSegS].ptsY[k] = shapes[foundSegS].ptsY[k-1];
                            }
                            shapes[foundSegS].ptsX[foundSegP+1] = foundPrX;
                            shapes[foundSegS].ptsY[foundSegP+1] = foundPrY;
                            shapes[foundSegS].ptCount++;
                            hitP = foundSegP + 1;
                        }
                    }
                }

                if (hitS == -1) {
                    for (i = shapeCount - 1; i >= 0; i--) {
                        if (shapes[i].type == 3) {
                            if (currentMode == 27) {
                                for (j = 0; j < shapes[i].ptCount; j++) {
                                    int px = (int)round(shapes[i].ptsX[j] * (scaleFactor * viewZoom) + viewPanX);
                                    int py = (int)round(shapes[i].ptsY[j] * (scaleFactor * viewZoom) + viewPanY);
                                    if (sqrt(pow(px - x, 2) + pow(py - y, 2)) <= 8.0) { hitS = i; hitP = j; break; }
                                }
                            }
                            if (hitS == -1 && PointInPolyShape(exactDragStartX, exactDragStartY, &shapes[i])) { hitS = i; hitP = -1; break; }
                            if (hitS != -1) break;
                        }
                    }
                }
                
                if (hitS != -1) {
                    if (currentMode == 27) {
                        if (hitP != -1) {
                            ToggleSelection(hitS, hitP);
                            if (selOrderCount == 2) {
                                if (dimCount < MAX_DIMS) {
                                    dims[dimCount].s1 = selOrderS[0]; dims[dimCount].p1 = selOrderP[0];
                                    dims[dimCount].s2 = selOrderS[1]; dims[dimCount].p2 = selOrderP[1];
                                    dims[dimCount].offset = 20.0 / (scaleFactor * viewZoom); 
                                    dims[dimCount].textPos = 0.5; dims[dimCount].mode = 0;
                                    dimCount++;
                                }
                                ClearSelection();
                            }
                        } else { ClearSelection(); }
                        RedrawCanvas(hwnd); return 0;
                    }
                    
                    if (currentMode == 1 || currentMode == 2) {
                        ClearSelection();
                        if (shapes[hitS].type == 3 || shapes[hitS].type == 4) ToggleSelection(hitS, 0);
                        else for(j = 0; j < shapes[hitS].ptCount; j++) ToggleSelection(hitS, j);
                        selectedShape = hitS;
                    } else {
                        for(j = 0; j < shapes[hitS].ptCount; j++) if(ptSelected[hitS][j]) sHasSel = 1;
                        if (shiftDown) {
                            if (hitP != -1) ToggleSelection(hitS, hitP);
                            else {
                                if (shapes[hitS].type == 3 || shapes[hitS].type == 4) {
                                    if (allSel && ptSelected[hitS][0]) ToggleSelection(hitS, 0); 
                                    else if (!allSel && !ptSelected[hitS][0]) ToggleSelection(hitS, 0);
                                } else {
                                    for(j = 0; j < shapes[hitS].ptCount; j++) if(!ptSelected[hitS][j]) allSel = 0;
                                    for(j = 0; j < shapes[hitS].ptCount; j++) {
                                        if (allSel && ptSelected[hitS][j]) ToggleSelection(hitS, j); 
                                        else if (!allSel && !ptSelected[hitS][j]) ToggleSelection(hitS, j);
                                    }
                                }
                            }
                        } else {
                            if (hitP != -1) { 
                                if (!ptSelected[hitS][hitP]) { 
                                    ClearSelection(); 
                                    if (currentMode == 1 || currentMode == 2) {
                                        if (shapes[hitS].type == 3 || shapes[hitS].type == 4) ToggleSelection(hitS, 0);
                                        else for(j = 0; j < shapes[hitS].ptCount; j++) ToggleSelection(hitS, j);
                                    } else {
                                        ToggleSelection(hitS, hitP); 
                                    }
                                } 
                            } 
                            else if (!sHasSel) { 
                                ClearSelection(); 
                                if (shapes[hitS].type == 3 || shapes[hitS].type == 4) ToggleSelection(hitS, 0);
                                else for(j = 0; j < shapes[hitS].ptCount; j++) ToggleSelection(hitS, j); 
                            }
                        }
                        selectedShape = hitS;
                    }
                    currentFill = shapes[selectedShape].fill; useFill = shapes[selectedShape].useFill; 
                    currentStroke = shapes[selectedShape].stroke; useStroke = shapes[selectedShape].useStroke;
                    currentStrokeWidth = shapes[selectedShape].strokeWidth > 0 ? shapes[selectedShape].strokeWidth : 1;
                } else { 
                    ClearSelection(); selectedShape = -1; 
                    if (currentMode == 27) { RedrawCanvas(hwnd); return 0; }
                }

                if (selectedShape != -1) {
                    pSelCount = 0;
                    for (i = 0; i < shapeCount; i++) {
                        int cnt = 0; for(j = 0; j < shapes[i].ptCount; j++) if(ptSelected[i][j]) cnt++;
                        if (cnt > 0 && cnt < shapes[i].ptCount && shapes[i].type != 3 && shapes[i].type != 4) isPartialSelection = 1;
                        pSelCount += cnt;
                    }
                    if (ctrlDown && pSelCount == shapes[selectedShape].ptCount && shapeCount < MAX_SHAPES && currentMode == 0) {
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
                    dragType = (currentMode == 0) ? ((shiftDown && !lockAxis) ? 3 : 1) : (currentMode == 1 ? 3 : 4); 
                    dragStartX = startX; dragStartY = startY; isDraggingPoint = isPartialSelection;
                    
                    minX = 99999; maxX = -99999; minY = 99999; maxY = -99999;
                    for(i = 0; i < shapeCount; i++) {
                        int anySel = 0;
                        for(j = 0; j < shapes[i].ptCount; j++) if (ptSelected[i][j]) anySel = 1;
                        
                        if (anySel) {
                            if (shapes[i].type == 3) {
                                for(j = 1; j <= 4; j++) {
                                    minX = fmin(minX, dragStartSnapshot[i].ptsX[j]); maxX = fmax(maxX, dragStartSnapshot[i].ptsX[j]);
                                    minY = fmin(minY, dragStartSnapshot[i].ptsY[j]); maxY = fmax(maxY, dragStartSnapshot[i].ptsY[j]);
                                }
                            } else if (shapes[i].type == 4) {
                                minX = fmin(minX, dragStartSnapshot[i].ptsX[0]); maxX = fmax(maxX, dragStartSnapshot[i].ptsX[0]);
                                minY = fmin(minY, dragStartSnapshot[i].ptsY[0]); maxY = fmax(maxY, dragStartSnapshot[i].ptsY[0]);
                            } else {
                                for(j = 0; j < shapes[i].ptCount; j++) {
                                    minX = fmin(minX, dragStartSnapshot[i].ptsX[j]); maxX = fmax(maxX, dragStartSnapshot[i].ptsX[j]);
                                    minY = fmin(minY, dragStartSnapshot[i].ptsY[j]); maxY = fmax(maxY, dragStartSnapshot[i].ptsY[j]);
                                }
                            }
                        }
                    }
                    shapeCx = (minX + maxX)/2.0; shapeCy = (minY + maxY)/2.0;
                }
                RedrawCanvas(hwnd); return 0;
            }
            else if (currentMode == 6) { 
                currentEndX = gx; currentEndY = gy; isDrawing = 1;
                currentShape.type = 0; currentShape.useFill = useFill; currentShape.fill = currentFill;
                currentShape.useStroke = useStroke; currentShape.stroke = currentStroke;
                currentShape.strokeWidth = currentStrokeWidth;
                GenShape(currentEndX, currentEndY); RedrawCanvas(hwnd);
            }
            else if (currentMode >= 3 && currentMode <= 5) { 
                if (!isDrawing) {
                    isDrawing = 1; currentShape.type = (currentMode == 4) ? 1 : (currentMode == 5 ? 2 : 0);
                    currentShape.useFill = (currentMode == 3) ? useFill : 0; currentShape.fill = currentFill;
                    currentShape.useStroke = 1; currentShape.stroke = currentStroke;
                    currentShape.strokeWidth = currentStrokeWidth;
                    currentShape.ptsX[0] = gx; currentShape.ptsY[0] = gy; currentShape.ptsX[1] = gx; currentShape.ptsY[1] = gy; currentShape.ptCount = 2;
                } else if (currentShape.ptCount < MAX_POINTS) {
                    currentShape.ptsX[currentShape.ptCount-1] = gx; currentShape.ptsY[currentShape.ptCount-1] = gy; currentShape.ptCount++; 
                    currentShape.ptsX[currentShape.ptCount-1] = gx; currentShape.ptsY[currentShape.ptCount-1] = gy;
                }
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            double nx, ny, dx, dy, bestDist, prX, prY, d, newX, newY, minX, maxX, minY, maxY;
            int x, y, i, j, p, np, ctrlDown, shiftDown;
            double diff, ox, oy, d1, d2, scale, actualDx, actualDy;
            double exactX, exactY;
            
            int oldHoverS, oldHoverP;
            int oldSegS, oldSegP;
            int needsRedraw;

            x = (int)(short)LOWORD(lParam); 
            y = (int)(short)HIWORD(lParam);
            exactX = (x - viewPanX) / (scaleFactor * viewZoom); 
            exactY = (y - viewPanY) / (scaleFactor * viewZoom);
            
            oldHoverS = hoverShape; oldHoverP = hoverPt;
            oldSegS = hoverSegShape; oldSegP = hoverSegPt;
            needsRedraw = 0;
            
            nx = CLAMP(Snap(exactX), 0, gridW); ny = CLAMP(Snap(exactY), 0, gridH);
            
            if (dragType == 5) {
                viewPanX += (x - panStartX);
                viewPanY += (y - panStartY);
                panStartX = x; panStartY = y;
                RedrawCanvas(hwnd);
                return 0;
            }

            if (dragDimIdx != -1) {
                Dimension* dptr = &dims[dragDimIdx];
                if (dptr->s1 < shapeCount && dptr->s2 < shapeCount) {
                    double lA1x = shapes[dptr->s1].ptsX[dptr->p1];
                    double lA1y = shapes[dptr->s1].ptsY[dptr->p1];
                    double lA2x = shapes[dptr->s2].ptsX[dptr->p2];
                    double lA2y = shapes[dptr->s2].ptsY[dptr->p2];
                    
                    double lMouseX = exactX;
                    double lMouseY = exactY;

                    if (dptr->mode == 0) {
                        double dimDx = lA2x - lA1x, dimDy = lA2y - lA1y; 
                        double ang = atan2(dimDy, dimDx);
                        double nX = -sin(ang), nY = cos(ang); 
                        dptr->offset = ((lMouseX - lA1x) * nX + (lMouseY - lA1y) * nY);
                        double len2 = dimDx*dimDx + dimDy*dimDy; 
                        if (len2 > 0) dptr->textPos = ((lMouseX - lA1x) * dimDx + (lMouseY - lA1y) * dimDy) / len2;
                    } else if (dptr->mode == 1) {
                        dptr->offset = lMouseY - lA1y; 
                        double dimDx = lA2x - lA1x; if (dimDx != 0) dptr->textPos = (lMouseX - lA1x) / dimDx;
                    } else if (dptr->mode == 2) {
                        dptr->offset = lMouseX - lA1x; 
                        double dimDy = lA2y - lA1y; if (dimDy != 0) dptr->textPos = (lMouseY - lA1y) / dimDy;
                    }
                    if (dptr->textPos < 0.0) dptr->textPos = 0.0; if (dptr->textPos > 1.0) dptr->textPos = 1.0;
                    needsRedraw = 1;
                }
            } else if (dragType > 0) {
                ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000); 
                shiftDown = (GetKeyState(VK_SHIFT) & 0x8000); 
                dx = nx - dragStartX; dy = ny - dragStartY;
                
                if (dragType == 1) {
                    minX = gridW; maxX = 0; minY = gridH; maxY = 0;
                    for(i = 0; i < shapeCount; i++) {
                        if (shapes[i].type == 3) {
                            int anySel = 0;
                            for(j = 0; j < shapes[i].ptCount; j++) if (ptSelected[i][j]) anySel = 1;
                            if (anySel) {
                                for(j = 1; j <= 4; j++) {
                                    minX = fmin(minX, dragStartSnapshot[i].ptsX[j]); maxX = fmax(maxX, dragStartSnapshot[i].ptsX[j]);
                                    minY = fmin(minY, dragStartSnapshot[i].ptsY[j]); maxY = fmax(maxY, dragStartSnapshot[i].ptsY[j]);
                                }
                            }
                        } else if (shapes[i].type == 4) {
                            if (ptSelected[i][0]) {
                                minX = fmin(minX, dragStartSnapshot[i].ptsX[0]); maxX = fmax(maxX, dragStartSnapshot[i].ptsX[0]);
                                minY = fmin(minY, dragStartSnapshot[i].ptsY[0]); maxY = fmax(maxY, dragStartSnapshot[i].ptsY[0]);
                            }
                        } else {
                            for(j = 0; j < shapes[i].ptCount; j++) {
                                if (ptSelected[i][j]) {
                                    minX = fmin(minX, dragStartSnapshot[i].ptsX[j]); maxX = fmax(maxX, dragStartSnapshot[i].ptsX[j]);
                                    minY = fmin(minY, dragStartSnapshot[i].ptsY[j]); maxY = fmax(maxY, dragStartSnapshot[i].ptsY[j]);
                                }
                            }
                        }
                    }
                    if (minX + dx < 0) dx = -minX; if (maxX + dx > gridW) dx = gridW - maxX; 
                    if (minY + dy < 0) dy = -minY; if (maxY + dy > gridH) dy = gridH - maxY;
                    if (ctrlDown && isDraggingPoint) { if (fabs(dx) > fabs(dy)) dy = 0; else dx = 0; }
                    
                    if (dx != 0 || dy != 0 || lockAxis) {
                        for(i = 0; i < shapeCount; i++) {
                            int anySelected = 0;
                            for(j=0; j<shapes[i].ptCount; j++) if(ptSelected[i][j]) anySelected = 1;
                            if (anySelected) {
                                if (shapes[i].type == 3) {
                                    newX = dragStartSnapshot[i].ptsX[0] + dx; newY = dragStartSnapshot[i].ptsY[0] + dy;
                                    shapes[i].ptsX[0] = snapToGrid ? round(newX) : newX; shapes[i].ptsY[0] = snapToGrid ? round(newY) : newY;
                                    actualDx = shapes[i].ptsX[0] - dragStartSnapshot[i].ptsX[0]; actualDy = shapes[i].ptsY[0] - dragStartSnapshot[i].ptsY[0];
                                    for(j = 1; j < shapes[i].ptCount; j++) {
                                        shapes[i].ptsX[j] = dragStartSnapshot[i].ptsX[j] + actualDx; shapes[i].ptsY[j] = dragStartSnapshot[i].ptsY[j] + actualDy;
                                    }
                                } else {
                                    for(j = 0; j < shapes[i].ptCount; j++) {
                                        if (ptSelected[i][j]) {
                                            int handledByLock = 0;
                                            if (lockAxis && isDraggingPoint) {
                                                int fixP = -1;
                                                if (shapes[i].ptCount == 2) {
                                                    if (!ptSelected[i][1 - j]) fixP = 1 - j;
                                                } else if (shapes[i].type == 0 || shapes[i].type == 2) {
                                                    int prev = (j - 1 + shapes[i].ptCount) % shapes[i].ptCount;
                                                    int next = (j + 1) % shapes[i].ptCount;
                                                    if (shapes[i].type == 2 && j == 0) prev = -1;
                                                    if (shapes[i].type == 2 && j == shapes[i].ptCount - 1) next = -1;
                                                    
                                                    if (shiftDown) {
                                                        if (next != -1 && !ptSelected[i][next]) fixP = next;
                                                        else if (prev != -1 && !ptSelected[i][prev]) fixP = prev;
                                                    } else {
                                                        if (prev != -1 && !ptSelected[i][prev]) fixP = prev;
                                                        else if (next != -1 && !ptSelected[i][next]) fixP = next;
                                                    }
                                                }

                                                if (fixP != -1) {
                                                    double fx = dragStartSnapshot[i].ptsX[fixP];
                                                    double fy = dragStartSnapshot[i].ptsY[fixP];
                                                    double ox = dragStartSnapshot[i].ptsX[j];
                                                    double oy = dragStartSnapshot[i].ptsY[j];
                                                    double vx = ox - fx;
                                                    double vy = oy - fy;
                                                    
                                                    if (fabs(vx) < 0.001) { 
                                                        shapes[i].ptsX[j] = ox;
                                                        shapes[i].ptsY[j] = snapToGrid ? round(exactY) : exactY;
                                                    } else if (fabs(vy) < 0.001) { 
                                                        shapes[i].ptsX[j] = snapToGrid ? round(exactX) : exactX;
                                                        shapes[i].ptsY[j] = oy;
                                                    } else { 
                                                        double len2 = vx*vx + vy*vy;
                                                        double t = ((exactX - fx)*vx + (exactY - fy)*vy) / len2;
                                                        shapes[i].ptsX[j] = fx + t * vx;
                                                        shapes[i].ptsY[j] = fy + t * vy;
                                                    }
                                                    handledByLock = 1;
                                                }
                                            }
                                            if (!handledByLock) {
                                                newX = dragStartSnapshot[i].ptsX[j] + dx; 
                                                newY = dragStartSnapshot[i].ptsY[j] + dy;
                                                shapes[i].ptsX[j] = snapToGrid ? round(newX) : newX; 
                                                shapes[i].ptsY[j] = snapToGrid ? round(newY) : newY;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (dragType == 3) {
                    diff = atan2(exactY - shapeCy, exactX - shapeCx) - atan2(exactDragStartY - shapeCy, exactDragStartX - shapeCx);
                    
                    if (ctrlDown) diff = round(diff / (PI/12.0)) * (PI/12.0);
                    else if (!(GetKeyState(VK_SHIFT) & 0x8000)) diff = round(diff / (PI/36.0)) * (PI/36.0);

                    for(i = 0; i < shapeCount; i++) {
                        int anySelected = 0;
                        for(j=0; j<shapes[i].ptCount; j++) if(ptSelected[i][j]) anySelected = 1;
                        if (anySelected) {
                            if (shapes[i].type == 3) {
                                char refPath[260]; double rSc = 1.0, rRot = 0.0;
                                if (strncmp(dragStartSnapshot[i].text, "{{EXT_REF=", 10) == 0) {
                                    char* pS = strstr(dragStartSnapshot[i].text, " scale=");
                                    char* pR = strstr(dragStartSnapshot[i].text, " rot=");
                                    if (pS) {
                                        int len = pS - (dragStartSnapshot[i].text + 10);
                                        if (len > 0 && len < 260) {
                                            double newRot, oldCx, oldCy, newCx, newCy, newX2, newY2, lcx = 0, lcy = 0; char absPath[260]; int rIdx;
                                            strncpy(refPath, dragStartSnapshot[i].text + 10, len); refPath[len] = '\0';
                                            rSc = atof(pS + 7); if (pR) rRot = atof(pR + 5);
                                            newRot = rRot + (diff * 180.0 / PI);
                                            while (newRot >= 360.0) newRot -= 360.0; while (newRot < 0.0) newRot += 360.0;
                                            sprintf(shapes[i].text, "{{EXT_REF=%s scale=%.2f rot=%.2f}}", refPath, rSc, newRot);
                                            ResolvePath(loadedCFile[0] ? loadedCFile : "", refPath, absPath); rIdx = EnsureRefLoaded(absPath);
                                            if (rIdx != -1) { lcx = (refCache[rIdx].minX + refCache[rIdx].maxX) / 2.0; lcy = (refCache[rIdx].minY + refCache[rIdx].maxY) / 2.0; }
                                            oldCx = dragStartSnapshot[i].ptsX[0] + lcx; oldCy = dragStartSnapshot[i].ptsY[0] + lcy;
                                            newCx = shapeCx + (oldCx - shapeCx) * cos(diff) - (oldCy - shapeCy) * sin(diff);
                                            newCy = shapeCy + (oldCx - shapeCx) * sin(diff) + (oldCy - shapeCy) * cos(diff);
                                            newX2 = newCx - lcx; newY2 = newCy - lcy;
                                            shapes[i].ptsX[0] = snapToGrid ? round(newX2) : newX2; shapes[i].ptsY[0] = snapToGrid ? round(newY2) : newY2;
                                        }
                                    }
                                }
                            } else {
                                for(j = 0; j < shapes[i].ptCount; j++) {
                                    if (ptSelected[i][j]) {
                                        ox = dragStartSnapshot[i].ptsX[j] - shapeCx; oy = dragStartSnapshot[i].ptsY[j] - shapeCy;
                                        shapes[i].ptsX[j] = shapeCx + (ox * cos(diff) - oy * sin(diff)); shapes[i].ptsY[j] = shapeCy + (ox * sin(diff) + oy * cos(diff));
                                    }
                                }
                            }
                        }
                    }
                } else if (dragType == 4) {
                    d1 = sqrt(pow(exactDragStartX - shapeCx, 2) + pow(exactDragStartY - shapeCy, 2)); d2 = sqrt(pow(exactX - shapeCx, 2) + pow(exactY - shapeCy, 2));
                    if (d1 > 0.01) {
                        scale = d2 / d1;
                        for(i = 0; i < shapeCount; i++) {
                            int anySelected = 0;
                            for(j=0; j<shapes[i].ptCount; j++) if(ptSelected[i][j]) anySelected = 1;
                            if (anySelected) {
                                if (shapes[i].type == 3) {
                                    char refPath[260]; double rSc = 1.0, rRot = 0.0;
                                    if (strncmp(dragStartSnapshot[i].text, "{{EXT_REF=", 10) == 0) {
                                        char* pS = strstr(dragStartSnapshot[i].text, " scale=");
                                        char* pR = strstr(dragStartSnapshot[i].text, " rot=");
                                        if (pS) {
                                            int len = pS - (dragStartSnapshot[i].text + 10);
                                            if (len > 0 && len < 260) {
                                                double newScale, oldCx, oldCy, newCx, newCy, newX2, newY2, lcx = 0, lcy = 0; char absPath[260]; int rIdx;
                                                strncpy(refPath, dragStartSnapshot[i].text + 10, len); refPath[len] = '\0';
                                                rSc = atof(pS + 7); if (pR) rRot = atof(pR + 5);
                                                newScale = rSc * scale;
                                                sprintf(shapes[i].text, "{{EXT_REF=%s scale=%.2f rot=%.2f}}", refPath, newScale, rRot);
                                                ResolvePath(loadedCFile[0] ? loadedCFile : "", refPath, absPath); rIdx = EnsureRefLoaded(absPath);
                                                if (rIdx != -1) { lcx = (refCache[rIdx].minX + refCache[rIdx].maxX) / 2.0; lcy = (refCache[rIdx].minY + refCache[rIdx].maxY) / 2.0; }
                                                oldCx = dragStartSnapshot[i].ptsX[0] + lcx * rSc; oldCy = dragStartSnapshot[i].ptsY[0] + lcy * rSc;
                                                newCx = shapeCx + (oldCx - shapeCx) * scale; newCy = shapeCy + (oldCy - shapeCy) * scale;
                                                newX2 = newCx - lcx * newScale; newY2 = newCy - lcy * newScale;
                                                shapes[i].ptsX[0] = snapToGrid ? round(newX2) : newX2; shapes[i].ptsY[0] = snapToGrid ? round(newY2) : newY2;
                                            }
                                        }
                                    }
                                } else {
                                    for(j = 0; j < shapes[i].ptCount; j++) {
                                        if (ptSelected[i][j]) {
                                            newX = shapeCx + ((dragStartSnapshot[i].ptsX[j] - shapeCx) * scale); newY = shapeCy + ((dragStartSnapshot[i].ptsY[j] - shapeCy) * scale);
                                            shapes[i].ptsX[j] = snapToGrid ? round(newX) : newX; shapes[i].ptsY[j] = snapToGrid ? round(newY) : newY;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                needsRedraw = 1;
            } else if (currentMode == 0 || currentMode == 1 || currentMode == 2 || currentMode == 27) {
                hoverShape = -1; hoverPt = -1; hoverSegShape = -1; hoverSegPt = -1; bestDist = 99999.0;
                
                if (selectedShape != -1 && selectedShape < shapeCount && shapes[selectedShape].type != 3 && shapes[selectedShape].type != 4) {
                    for (p = 0; p < shapes[selectedShape].ptCount; p++) {
                        d = sqrt(pow(shapes[selectedShape].ptsX[p]*(scaleFactor * viewZoom) + viewPanX - x, 2) + pow(shapes[selectedShape].ptsY[p]*(scaleFactor * viewZoom) + viewPanY - y, 2));
                        if (d < 15.0 && d < bestDist) { hoverShape = selectedShape; hoverPt = p; bestDist = d; }
                    }
                }
                if (hoverPt == -1) {
                    for (i = 0; i < shapeCount; i++) {
                        if (shapes[i].type == 3 || shapes[i].type == 4) continue;
                        for (p = 0; p < shapes[i].ptCount; p++) {
                            d = sqrt(pow(shapes[i].ptsX[p]*(scaleFactor * viewZoom) + viewPanX - x, 2) + pow(shapes[i].ptsY[p]*(scaleFactor * viewZoom) + viewPanY - y, 2));
                            if (d < 15.0 && d < bestDist) { hoverShape = i; hoverPt = p; bestDist = d; }
                        }
                    }
                }
                if (hoverPt == -1) {
                    bestDist = 99999.0;
                    for (i = 0; i < shapeCount; i++) {
                        if (shapes[i].type == 3 || shapes[i].type == 4 || shapes[i].ptCount >= MAX_POINTS) continue;
                        for (p = 0; p < (shapes[i].type == 0 ? shapes[i].ptCount : shapes[i].ptCount - 1); p++) {
                            np = (p + 1) % shapes[i].ptCount;
                            PtToSegProj((double)exactX, (double)exactY, shapes[i].ptsX[p], shapes[i].ptsY[p], shapes[i].ptsX[np], shapes[i].ptsY[np], &prX, &prY, &d);
                            if (d < (10.0 / scaleFactor) && d < bestDist) { hoverSegShape = i; hoverSegPt = p; hoverProjX = prX; hoverProjY = prY; bestDist = d; }
                        }
                    }
                }
                if (hoverPt == -1 && hoverSegShape == -1 && currentMode == 27) {
                    for (i = 0; i < shapeCount; i++) {
                        if (shapes[i].type == 3) {
                            for (p = 0; p < shapes[i].ptCount; p++) {
                                d = sqrt(pow(shapes[i].ptsX[p]*(scaleFactor * viewZoom) + viewPanX - x, 2) + pow(shapes[i].ptsY[p]*(scaleFactor * viewZoom) + viewPanY - y, 2));
                                if (d < 15.0 && d < bestDist) { hoverShape = i; hoverPt = p; bestDist = d; }
                            }
                        }
                    }
                }
            } else if (isDrawing) {
                if (currentMode == 6) { currentEndX = nx; currentEndY = ny; GenShape(currentEndX, currentEndY); } 
                else if (currentMode >= 3 && currentMode <= 5) { currentShape.ptsX[currentShape.ptCount-1] = nx; currentShape.ptsY[currentShape.ptCount-1] = ny; }
                needsRedraw = 1;
            }
            
            if (needsRedraw || hoverShape != oldHoverS || hoverPt != oldHoverP || hoverSegShape != oldSegS || hoverSegPt != oldSegP) {
                RedrawCanvas(hwnd);
            }
            break;
        }
        case WM_LBUTTONUP: {
            int x = (int)(short)LOWORD(lParam); 
            int y = (int)(short)HIWORD(lParam);
            ReleaseCapture();
            if (dragDimIdx != -1) dragDimIdx = -1;
            if (dragType > 0) { 
                if (dragType != 5) SaveState(); 
                dragType = 0;
                RedrawCanvas(hwnd);
            }
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
            int x, y, i, k, px, py, dimIdx, m;
            double exactX, exactY;
            
            x = (int)(short)LOWORD(lParam); 
            y = (int)(short)HIWORD(lParam);
            
            exactX = (x - viewPanX) / (scaleFactor * viewZoom); 
            exactY = (y - viewPanY) / (scaleFactor * viewZoom);

            int hitExtRef = -1;
            for (i = shapeCount - 1; i >= 0; i--) {
                if (shapes[i].type == 3) {
                    if (PointInPolyShape(exactX, exactY, &shapes[i])) {
                        hitExtRef = i;
                        break;
                    }
                }
            }
            
            if (hitExtRef != -1) {
                int baseExt = hitExtRef;
                if (selectedShape != -1 && shapes[selectedShape].type == 3) {
                    baseExt = selectedShape;
                }
                int nextExt = -1;
                for (i = 1; i <= shapeCount; i++) {
                    int idx = (baseExt + i) % shapeCount;
                    if (shapes[idx].type == 3) {
                        nextExt = idx;
                        break;
                    }
                }
                if (nextExt != -1) {
                    ClearSelection();
                    ToggleSelection(nextExt, 0);
                    selectedShape = nextExt;
                    UpdateStatusBar();
                    RedrawCanvas(hwnd);
                    return 0;
                }
            }

            if (currentMode == 0 || currentMode == 1 || currentMode == 2 || currentMode == 27) {
                if (currentMode == 27) {
                    ClearSelection(); currentMode = 0; UpdateStatusBar(); RedrawCanvas(hwnd); return 0;
                }
                for (i = 0; i < shapeCount; i++) {
                    if (shapes[i].type == 3 || shapes[i].type == 4) continue;
                    for (int p = 0; p < shapes[i].ptCount; p++) {
                        px = (int)round(shapes[i].ptsX[p] * (scaleFactor * viewZoom) + viewPanX);
                        py = (int)round(shapes[i].ptsY[p] * (scaleFactor * viewZoom) + viewPanY);
                        if (sqrt(pow(px - x, 2) + pow(py - y, 2)) <= 8.0) {
                            if (shapes[i].ptCount > (shapes[i].type == 0 ? 3 : 2)) {
                                SaveState();
                                for(dimIdx = 0; dimIdx < dimCount; ) {
                                    if ((dims[dimIdx].s1 == i && dims[dimIdx].p1 == p) || (dims[dimIdx].s2 == i && dims[dimIdx].p2 == p)) {
                                        for(m = dimIdx; m < dimCount - 1; m++) dims[m] = dims[m+1];
                                        dimCount--;
                                    } else {
                                        if (dims[dimIdx].s1 == i && dims[dimIdx].p1 > p) dims[dimIdx].p1--;
                                        if (dims[dimIdx].s2 == i && dims[dimIdx].p2 > p) dims[dimIdx].p2--;
                                        dimIdx++;
                                    }
                                }
                                for (k = p; k < shapes[i].ptCount - 1; k++) {
                                    shapes[i].ptsX[k] = shapes[i].ptsX[k+1]; shapes[i].ptsY[k] = shapes[i].ptsY[k+1];
                                }
                                shapes[i].ptCount--; ClearSelection(); RedrawCanvas(hwnd);
                            }
                            return 0;
                        }
                    }
                }
            } else if (isDrawing && (currentMode >= 3 && currentMode <= 5)) { 
                currentShape.ptCount--; 
                if (currentShape.ptCount >= (currentMode == 3 ? 3 : 2) && shapeCount < MAX_SHAPES) {
                    SaveState(); shapes[shapeCount++] = currentShape; selectedShape = shapeCount - 1; currentMode = 0;
                }
                currentShape.ptCount = 0; isDrawing = 0; RedrawCanvas(hwnd); 
            } else {
                currentShape.ptCount = 0; isDrawing = 0; if (shapeCount > 0) currentMode = 0; else currentMode = 3; RedrawCanvas(hwnd); 
            }
            break;
        }
        case WM_LBUTTONDBLCLK: {
            int x, y, i, j, k, p, s, np, tagHit;
            int foundSegS, foundSegP;
            double exactX, exactY, bestDist, d, prX, prY, foundPrX, foundPrY;
            double A1x, A1y, A2x, A2y, D1x, D1y, D2x, D2y, midX, midY, val;
            double dX, dY, ang, nX, nY;
            char escBuf[512];
            char rawVal[128];
            char buf[32];
            
            x = (int)(short)LOWORD(lParam); 
            y = (int)(short)HIWORD(lParam);

            int cx_pal = clientW - PANEL_WIDTH + 15;
            if (x >= cx_pal && x < cx_pal + 256 && y >= 382 && y < 414) {
                int pIdx = ((y - 382) / 16) * 8 + (x - cx_pal) / 32;
                CHOOSECOLOR cc;
                static COLORREF acrCustClr[16];
                memset(&cc, 0, sizeof(cc));
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = palette[pIdx];
                cc.lpCustColors = (LPDWORD)acrCustClr;
                cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                if (ChooseColor(&cc)) {
                    palette[pIdx] = cc.rgbResult;
                    currentFill = cc.rgbResult;
                    useFill = 1;
                    if (selectedShape != -1) { 
                        SaveState(); 
                        shapes[selectedShape].fill = currentFill; 
                        shapes[selectedShape].useFill = 1;
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                return 0;
            }

            tagHit = 0;
            for (k = textHitCount - 1; k >= 0; k--) {
                if (x >= textHits[k].box.left && x <= textHits[k].box.right && y >= textHits[k].box.top && y <= textHits[k].box.bottom) {
                    editShapeIdx = textHits[k].shapeIdx;
                    editTagIdx = textHits[k].isRef ? textHits[k].tagIdx : -1;
                    if (editTagIdx == -1) {
                        EscapeCString(shapes[editShapeIdx].text, escBuf, 512);
                    } else {
                        GetPipeValue(shapes[editShapeIdx].tagData, editTagIdx, rawVal, 128);
                        EscapeCString(rawVal, escBuf, 512);
                    }
                    distEditMode = 6;
                    SetWindowText(hDistEdit, escBuf);
                    SetWindowPos(hDistEdit, (HWND)0, textHits[k].box.left, textHits[k].box.top, 150, 20, SWP_SHOWWINDOW);
                    BringWindowToTop(hDistEdit);
                    SetFocus(hDistEdit);
                    tagHit = 1; break;
                }
            }
            
            if (!tagHit && currentMode == 0) {
                exactX = (x - viewPanX) / (scaleFactor * viewZoom); 
                exactY = (y - viewPanY) / (scaleFactor * viewZoom);
                bestDist = 99999.0;
                foundPrX = 0; foundPrY = 0;
                foundSegS = -1; foundSegP = -1;
                
                for (i = shapeCount - 1; i >= 0; i--) {
                    if (shapes[i].type == 3 || shapes[i].type == 4 || shapes[i].ptCount < 2) continue;
                    for (j = 0; j < (shapes[i].type == 0 ? shapes[i].ptCount : shapes[i].ptCount - 1); j++) {
                        np = (j + 1) % shapes[i].ptCount;
                        PtToSegProj(exactX, exactY, shapes[i].ptsX[j], shapes[i].ptsY[j], shapes[i].ptsX[np], shapes[i].ptsY[np], &prX, &prY, &d);
                        if (d < (10.0 / scaleFactor) && d < bestDist) { 
                            foundSegS = i; foundSegP = j; foundPrX = prX; foundPrY = prY; bestDist = d; 
                        }
                    }
                }
                if (foundSegS != -1 && shapes[foundSegS].ptCount < MAX_POINTS) {
                    s = foundSegS; p = foundSegP; SaveState();
                    for (k = shapes[s].ptCount; k > p + 1; k--) {
                        shapes[s].ptsX[k] = shapes[s].ptsX[k-1]; shapes[s].ptsY[k] = shapes[s].ptsY[k-1];
                    }
                    shapes[s].ptsX[p+1] = foundPrX; shapes[s].ptsY[p+1] = foundPrY; shapes[s].ptCount++;
                    ClearSelection(); ToggleSelection(s, p+1); selectedShape = s; RedrawCanvas(hwnd);
                }
            }
            
            if (!tagHit) {
                for (i = 0; i < dimCount; i++) {
                    A1x = shapes[dims[i].s1].ptsX[dims[i].p1] * (scaleFactor * viewZoom) + viewPanX;
                    A1y = shapes[dims[i].s1].ptsY[dims[i].p1] * (scaleFactor * viewZoom) + viewPanY;
                    A2x = shapes[dims[i].s2].ptsX[dims[i].p2] * (scaleFactor * viewZoom) + viewPanX;
                    A2y = shapes[dims[i].s2].ptsY[dims[i].p2] * (scaleFactor * viewZoom) + viewPanY;
                    
                    double scOffset = dims[i].offset * (scaleFactor * viewZoom);

                    if (dims[i].mode == 0) {
                        dX = A2x - A1x; dY = A2y - A1y;
                        ang = atan2(dY, dX); nX = -sin(ang); nY = cos(ang);
                        D1x = A1x + nX * scOffset; D1y = A1y + nY * scOffset;
                        D2x = A2x + nX * scOffset; D2y = A2y + nY * scOffset;
                    } else if (dims[i].mode == 1) {
                        D1x = A1x; D1y = A1y + scOffset; D2x = A2x; D2y = A1y + scOffset;
                    } else {
                        D1x = A1x + scOffset; D1y = A1y; D2x = A1x + scOffset; D2y = A2y;
                    }
                    midX = D1x + (D2x - D1x) * dims[i].textPos;
                    midY = D1y + (D2y - D1y) * dims[i].textPos;
                    
                    if (x >= midX - 20 && x <= midX + 20 && y >= midY - 10 && y <= midY + 10) {
                        if (dims[i].mode == 0) val = sqrt(pow(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1], 2) + pow(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1], 2));
                        else if (dims[i].mode == 1) val = fabs(shapes[dims[i].s2].ptsX[dims[i].p2] - shapes[dims[i].s1].ptsX[dims[i].p1]);
                        else val = fabs(shapes[dims[i].s2].ptsY[dims[i].p2] - shapes[dims[i].s1].ptsY[dims[i].p1]);
                        sprintf(buf, "%.2f", val);
                        distEditMode = 7; editDimIdx = i;
                        SetWindowText(hDistEdit, buf);
                        SetWindowPos(hDistEdit, (HWND)0, (int)midX - 20, (int)midY - 10, 60, 20, SWP_SHOWWINDOW);
                        BringWindowToTop(hDistEdit);
                        SetFocus(hDistEdit);
                        return 0;
                    }
                }
            }
            return 0;
        }
        case WM_APP + 1: { 
            static char buf[512]; double newDist, dx, dy, minX, maxX, minY, maxY, cx, cy, oldVal, scale; 
            int p, s1, p1, s2, p2;
            GetWindowText(hDistEdit, buf, 512); newDist = atof(buf);
            
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
            } else if (distEditMode == 5) {
                char relPath[260]; double refScale = atof(buf);
                if (refScale <= 0.0) refScale = 1.0;
                
                if (loadedCFile[0]) {
                    GetRelativePath(loadedCFile, pendingSvgFile, relPath);
                } else {
                    char cwd[260];
                    if (getcwd(cwd, 260)) {
                        strcat(cwd, "\\dummy.c"); 
                        GetRelativePath(cwd, pendingSvgFile, relPath);
                    } else {
                        strcpy(relPath, pendingSvgFile);
                    }
                }
                
                if (shapeCount < MAX_SHAPES) {
                    SaveState(); memset(&shapes[shapeCount], 0, sizeof(Shape));
                    shapes[shapeCount].type = 3; shapes[shapeCount].ptCount = 1; shapes[shapeCount].stroke = currentStroke;
                    sprintf(shapes[shapeCount].text, "{{EXT_REF=%s scale=%.2f rot=0.00}}", relPath, refScale);
                    shapeCount++;
                }
            } else if (distEditMode == 6) {
                if (editShapeIdx != -1) {
                    static char unesc[512];
                    UnescapeCString(buf, unesc, 512);
                    SaveState();
                    if (editTagIdx == -1) {
                        strncpy(shapes[editShapeIdx].text, unesc, 127);
                        shapes[editShapeIdx].text[127] = '\0';
                    } else {
                        SetPipeValue(shapes[editShapeIdx].tagData, editTagIdx, unesc);
                    }
                    editShapeIdx = -1;
                }
            } else if (distEditMode == 7 && editDimIdx != -1) {
                Dimension* dptr = &dims[editDimIdx];
                s1 = dptr->s1; p1 = dptr->p1; s2 = dptr->s2; p2 = dptr->p2;
                dx = shapes[s2].ptsX[p2] - shapes[s1].ptsX[p1];
                dy = shapes[s2].ptsY[p2] - shapes[s1].ptsY[p1];
                
                if (dptr->mode == 0) oldVal = sqrt(dx*dx + dy*dy);
                else if (dptr->mode == 1) oldVal = fabs(dx);
                else oldVal = fabs(dy);
                
                if (oldVal > 0.0001) {
                    SaveState();
                    if (dptr->mode == 0) {
                        shapes[s2].ptsX[p2] = shapes[s1].ptsX[p1] + dx * (newDist / oldVal);
                        shapes[s2].ptsY[p2] = shapes[s1].ptsY[p1] + dy * (newDist / oldVal);
                    } else if (dptr->mode == 1) {
                        shapes[s2].ptsX[p2] = shapes[s1].ptsX[p1] + dx * (newDist / oldVal);
                    } else {
                        shapes[s2].ptsY[p2] = shapes[s1].ptsY[p1] + dy * (newDist / oldVal);
                    }
                }
                editDimIdx = -1;
            }
            ShowWindow(hDistEdit, SW_HIDE); distEditMode = 0; RedrawCanvas(hwnd); SetFocus(hwnd); 
            break;
        }
        case WM_APP + 2: {
            SetWindowPos(hDistEdit, (HWND)0, canvasW_px/2, canvasH_px/2, 60, 20, SWP_SHOWWINDOW);
            BringWindowToTop(hDistEdit);
            SetFocus(hDistEdit);
            break;
        }
        case WM_COMMAND: {
            UINT id = wParam;
            int btnId = id - 200;
            int i, j, k, m;

            if (id == 151) { 
                if (parsedCount < MAX_ICONS) {
                    int newId = 1;
                    for(i=0; i<parsedCount; i++) if(parsedIcons[i].caseId >= newId) newId = parsedIcons[i].caseId + 1;
                    parsedIcons[parsedCount].caseId = newId;
                    strcpy(parsedIcons[parsedCount].name, "New");
                    parsedIcons[parsedCount].shapes = NULL;
                    parsedIcons[parsedCount].shapeCount = 0;
                    parsedIcons[parsedCount].dimCount = 0;
                    parsedIcons[parsedCount].tCount = 0;
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
                        parsedIcons[parsedCount].dimCount = 0;
                    }
                    
                    if (parsedCount == 0) { 
                        parsedCount = 1; currentIconIdx = -1; currentCaseId = 1;
                        parsedIcons[0].caseId = 1;
                        strcpy(parsedIcons[0].name, "New");
                        parsedIcons[0].shapes = NULL; parsedIcons[0].shapeCount = 0;
                        parsedIcons[0].dimCount = 0;
                        shapeCount = 0; tagCount = 0; dimCount = 0;
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
            else if (id == 156) {
                currentStrokeWidth++; if (currentStrokeWidth > 20) currentStrokeWidth = 20;
                if (selectedShape != -1 && shapes[selectedShape].type != 3 && shapes[selectedShape].type != 4) { SaveState(); shapes[selectedShape].strokeWidth = currentStrokeWidth; RedrawCanvas(hwnd); }
                UpdateStatusBar(); SetFocus(hwnd); return 0;
            } else if (id == 157) {
                currentStrokeWidth--; if (currentStrokeWidth < 1) currentStrokeWidth = 1;
                if (selectedShape != -1 && shapes[selectedShape].type != 3 && shapes[selectedShape].type != 4) { SaveState(); shapes[selectedShape].strokeWidth = currentStrokeWidth; RedrawCanvas(hwnd); }
                UpdateStatusBar(); SetFocus(hwnd); return 0;
            }

            if (btnId >= 0 && btnId < 33) {
                if (btnId >= 0 && btnId <= 5) { 
                    if (btnId == 1 && lockAxis) {
                        ShowStatus(" Rotation disabled while Lock Axis is on.");
                    } else {
                        int keepSel = (btnId == 0 && selectedShape != -1 && shapes[selectedShape].type == 3);
                        currentMode = btnId; isDrawing = 0; dragType = 0; 
                        if (!keepSel) ClearSelection(); 
                        UpdateStatusBar(); InvalidateRect(hwnd, NULL, TRUE); 
                    }
                }
                else if (btnId == 7) { currentMode = 7; isDrawing = 0; dragType = 0; ClearSelection(); UpdateStatusBar(); RedrawCanvas(hwnd); }
                else if (btnId == 8) { currentMode = 8; isDrawing = 0; dragType = 0; ClearSelection(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, TRUE); }
                else if (btnId == 9) Undo(hwnd);
                else if (btnId == 10) { 
                    SaveState(); 
                    shapeCount = 0; 
                    selectedShape = -1; 
                    tagCount = 0; 
                    dimCount = 0; 
                    parsedCount = 1; currentCaseId = 1; currentIconIdx = -1;
                    parsedIcons[0].caseId = 1;
                    strcpy(parsedIcons[0].name, "New");
                    parsedIcons[0].shapes = NULL; parsedIcons[0].shapeCount = 0; parsedIcons[0].dimCount = 0;
                    loadedCFile[0] = '\0';
                    refCacheCount = 0; 
                    SetScrollRange(hScrlIcon, SB_CTL, 0, 0, TRUE); 
                    SwitchToIcon(0);
                    ClearSelection(); 
                    currentMode = 3; 
                    UpdateStatusBar(); 
                    RedrawCanvas(hwnd);
                }
                else if (btnId == 6) { 
                    if (loadedCFile[0] != '\0') {
                        LoadCFile(loadedCFile, hwnd);
                    } else {
                        SendMessage(hwnd, WM_COMMAND, 210, 0); 
                    }
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
                            PostMessage(hwnd, WM_APP + 2, 0, 0);
                        }
                    }
                }
                else if (btnId == 13) { 
                    static OPENFILENAME ofn; static char szFile[260]; memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; 
                    ofn.lpstrFilter = "C / SVG Files\0*.*\0"; 
                    ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) {
                        if (loadedCFile[0] && stricmp(szFile, loadedCFile) == 0) {
                            MessageBox(hwnd, "Cannot import a reference to the currently open file.", "Circular Reference", MB_ICONHAND);
                        } else {
                            strcpy(pendingSvgFile, szFile);
                            distEditMode = 5;
                            SetWindowText(hDistEdit, "1.0");
                            PostMessage(hwnd, WM_APP + 2, 0, 0);
                        }
                    }
                }
                else if (btnId == 27) { 
                    currentMode = 27; isDrawing = 0; dragType = 0; ClearSelection(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, TRUE);
                }
                else if (btnId == 28) { 
                    if (!hPageSizeDlg) hPageSizeDlg = CreateWindow("PageSizeClass", "Edit Page Size", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 320, 270, hwnd, NULL, hInst, NULL);
                    SetFocus(hPageSizeDlg);
                }
                else if (btnId == 29) {
                    HWND hDlg = CreateWindow("TagEditorClass", "Tags", WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE|WS_VSCROLL, 200, 200, 300, 250, hwnd, NULL, hInst, NULL);
                    EnableWindow(hwnd, FALSE); MSG tmsg;
                    while (IsWindow(hDlg) && GetMessage(&tmsg, NULL, 0, 0)) { TranslateMessage(&tmsg); DispatchMessage(&tmsg); }
                    EnableWindow(hwnd, TRUE); SetFocus(hwnd);
                }
                else if (btnId == 30) {
                    if (shapeCount < MAX_SHAPES) {
                        char newTagName[32]; int tNum = 1, found;
                        do {
                            found = 0; sprintf(newTagName, "TAG%d", tNum);
                            for (k = 0; k < tagCount; k++) {
                                if (strcmp(tagNames[k], newTagName) == 0) { found = 1; break; }
                            }
                            for (k = 0; k < shapeCount; k++) {
                                if (shapes[k].type == 4) {
                                    if (strcmp(shapes[k].text, newTagName) == 0) { found = 1; break; }
                                }
                            }
                            if (found) tNum++;
                        } while(found);

                        SaveState(); memset(&currentShape, 0, sizeof(Shape));
                        currentShape.type = 4; currentShape.ptsX[0] = startX; currentShape.ptsY[0] = startY; currentShape.ptCount = 1;
                        currentShape.useFill = 0; currentShape.fontSize = 24;
                        strcpy(currentShape.text, newTagName); currentShape.stroke = currentStroke;
                        shapes[shapeCount++] = currentShape; 
                        
                        if (tagCount < MAX_TAGS) {
                            strcpy(tagNames[tagCount], newTagName);
                            strcpy(tagValues[tagCount], newTagName);
                            tagCount++;
                        }
                        currentMode = 0; UpdateStatusBar(); RedrawCanvas(hwnd);
                    }
                    ReleaseCapture(); return 0;
                }
                else if (btnId == 31) {
                    lockAxis = !lockAxis;
                    SetWindowText(hBtn[31], lockAxis ? "Lock Axis" : "(Lock Axis)");
                    if (lockAxis && currentMode == 1) {
                        currentMode = 0;
                        ClearSelection();
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                    UpdateStatusBar();
                }
                else if (btnId == 32) {
                    viewPanX = 0; viewPanY = 0; viewZoom = 1.0; 
                    SetScrollPos(hScrlZoom, SB_CTL, 10, TRUE);
                    RedrawCanvas(hwnd);
                }
                else if (btnId == 14) { 
                    static OPENFILENAME ofn; static char szFile[260];
                    memset(&ofn, 0, sizeof(ofn)); szFile[0] = '\0';
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFilter = "C Data Files (*.c)\0*.c\0"; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile); ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) LoadCFile(szFile, hwnd); 
                }
                else if (btnId == 15) { DoSaveFile(hwnd); }
                else if (btnId == 16) { 
                    if (selectedShape != -1 && (shapes[selectedShape].type == 0 || shapes[selectedShape].type == 2)) {
                        SaveState();
                        if (shapes[selectedShape].type == 0) {
                            shapes[selectedShape].type = 2;
                            if (shapes[selectedShape].ptCount > 0 && shapes[selectedShape].ptCount < MAX_POINTS) {
                                shapes[selectedShape].ptsX[shapes[selectedShape].ptCount] = shapes[selectedShape].ptsX[0];
                                shapes[selectedShape].ptsY[shapes[selectedShape].ptCount] = shapes[selectedShape].ptsY[0];
                                shapes[selectedShape].ptCount++;
                            }
                        } else {
                            shapes[selectedShape].type = 0;
                            if (shapes[selectedShape].ptCount > 0) {
                                shapes[selectedShape].ptCount--;
                            }
                        }
                        UpdateStatusBar();
                        RedrawCanvas(hwnd);
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

                                    for(j = 0; j < dimCount; ) {
                                        if (dims[j].s1 == keepIdx || dims[j].s2 == keepIdx || dims[j].s1 == delIdx || dims[j].s2 == delIdx) {
                                            for(m = j; m < dimCount - 1; m++) dims[m] = dims[m+1];
                                            dimCount--;
                                        } else {
                                            if (dims[j].s1 > delIdx) dims[j].s1--;
                                            if (dims[j].s2 > delIdx) dims[j].s2--;
                                            j++;
                                        }
                                    }
                                    
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
                        int px = (int)round(shapes[s2_l].ptsX[p2_l] * (scaleFactor * viewZoom) + viewPanX) + 10;
                        int py = (int)round(shapes[s2_l].ptsY[p2_l] * (scaleFactor * viewZoom) + viewPanY) + 10;
                        char buf[32]; distEditMode = 0; 
                        sprintf(buf, "%.2f", sqrt(dx*dx + dy*dy));
                        SetWindowText(hDistEdit, buf); SetWindowPos(hDistEdit, (HWND)0, px, py, 60, 20, SWP_SHOWWINDOW); BringWindowToTop(hDistEdit); SetFocus(hDistEdit);
                    } else if (selectedShape != -1 && shapes[selectedShape].ptCount > 0) {
                        double minX = 9999, maxX = -9999, minY = 9999, maxY = -9999, val; int p; char buf[32];
                        for(p=0; p<shapes[selectedShape].ptCount; p++) { minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]); minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]); }
                        val = (btnId == 23) ? (maxX - minX) : (maxY - minY); distEditMode = (btnId == 23) ? 1 : 2;
                        sprintf(buf, "%.2f", val); SetWindowText(hDistEdit, buf); 
                        SetWindowPos(hDistEdit, (HWND)0, (int)(((minX+maxX)/2.0) * (scaleFactor * viewZoom) + viewPanX), (int)(((minY+maxY)/2.0) * (scaleFactor * viewZoom) + viewPanY), 60, 20, SWP_SHOWWINDOW); BringWindowToTop(hDistEdit); SetFocus(hDistEdit);
                    }
                }
                else if (btnId == 25) { 
                    if (selectedShape != -1 && shapeCount < MAX_SHAPES) { SaveState(); shapes[shapeCount] = shapes[selectedShape]; selectedShape = shapeCount++; RedrawCanvas(hwnd); }
                }
                else if (btnId == 26) { 
                    if (lockAxis) {
                        ShowStatus(" Angle setting disabled while Lock Axis is on.");
                    } else if (selOrderCount == 2) {
                        int s1_l = selOrderS[0], p1_l = selOrderP[0], s2_l = selOrderS[1], p2_l = selOrderP[1];
                        double dx = shapes[s2_l].ptsX[p2_l] - shapes[s1_l].ptsX[p1_l];
                        double dy = shapes[s2_l].ptsY[p2_l] - shapes[s1_l].ptsY[p1_l];
                        double ang = atan2(dy, dx) * 180.0 / PI;
                        int px = (int)round(shapes[s2_l].ptsX[p2_l] * (scaleFactor * viewZoom) + viewPanX) + 10;
                        int py = (int)round(shapes[s2_l].ptsY[p2_l] * (scaleFactor * viewZoom) + viewPanY) + 10;
                        char buf[32]; distEditMode = 4;
                        if (ang < 0) ang += 360.0;
                        sprintf(buf, "%.2f", ang);
                        SetWindowText(hDistEdit, buf); SetWindowPos(hDistEdit, (HWND)0, px, py, 60, 20, SWP_SHOWWINDOW); BringWindowToTop(hDistEdit); SetFocus(hDistEdit);
                    } else {
                        ShowStatus(" Select exactly 2 nodes to set angle.");
                    }
                }
            }
            if (id != 300 && (id < 222 || id > 224) && id != 226) SetFocus(hwnd); break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc;
            hdc = BeginPaint(hwnd, &ps);
            if (shapes) {
                textHitCount = 0;
                DrawGrid(hdc); RenderShapes(hdc, shapes, shapeCount, scaleFactor * viewZoom, (int)viewPanX, (int)viewPanY, &currentShape, isDrawing, 0);
                if (currentMode == 0 || currentMode == 1 || currentMode == 2 || currentMode == 27) DrawNodes(hdc);
                DrawDimensions(hdc);
                
                if (ps.rcPaint.right > clientW - PANEL_WIDTH + 10) {
                    DrawPalette(hdc); DrawPreview(hdc); 
                }
            }
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_DESTROY: {
            int i;
            if (IsWindow(hDistEdit) && oldEditProc) { SetWindowLong(hDistEdit, GWL_WNDPROC, (LONG)oldEditProc); oldEditProc = NULL; }
            if (IsWindow(hMultiEdit) && oldMultiEditProc) { SetWindowLong(hMultiEdit, GWL_WNDPROC, (LONG)oldMultiEditProc); oldMultiEditProc = NULL; }

            if (shapes) { GlobalFreePtr(shapes); shapes = NULL; }
            if (dragStartSnapshot) { GlobalFreePtr(dragStartSnapshot); dragStartSnapshot = NULL; }
            
            if (g_tempRef) { GlobalFreePtr(g_tempRef); g_tempRef = NULL; }
            if (g_subPts) { GlobalFreePtr(g_subPts); g_subPts = NULL; }
            if (g_pA) { GlobalFreePtr(g_pA); g_pA = NULL; }

            for(i=0; i<MAX_UNDO; i++) {
                if (history[i]) { GlobalFreePtr(history[i]); history[i] = NULL; }
            }
            
            if (parsedIcons) {
                for(i=0; i<MAX_ICONS; i++) if (parsedIcons[i].shapes) { GlobalFreePtr(parsedIcons[i].shapes); parsedIcons[i].shapes = NULL; }
                GlobalFreePtr(parsedIcons); parsedIcons = NULL;
            }
            if (refCache) {
                for(i=0; i<MAX_REFS; i++) if (refCache[i].shapes) { GlobalFreePtr(refCache[i].shapes); refCache[i].shapes = NULL; }
                GlobalFreePtr(refCache); refCache = NULL;
            }
            if (editMap) { GlobalFreePtr(editMap); editMap = NULL; }
            
            if (subclassThunk) { FreeProcInstance(subclassThunk); subclassThunk = NULL; }
            if (multiSubclassThunk) { FreeProcInstance(multiSubclassThunk); multiSubclassThunk = NULL; }
            
            PostQuitMessage(0); break;
        }
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0L;
}
#pragma code_seg ();

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow) {
    MSG msg; WNDCLASS wc; hInst = hInstance;
    if (!hPrevInstance) {
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WndProc; wc.cbClsExtra = 0; wc.cbWndExtra = 0;
        wc.hInstance = hInstance; wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = NULL; wc.lpszClassName = "IconEditClass";
        if (!RegisterClass(&wc)) return FALSE;

        wc.lpfnWndProc = PageSizeProc; wc.lpszClassName = "PageSizeClass"; RegisterClass(&wc);
        wc.lpfnWndProc = TagEditorProc; wc.lpszClassName = "TagEditorClass"; RegisterClass(&wc);
        wc.lpfnWndProc = ScaleDlgProc; wc.lpszClassName = "ScaleDlgClass"; RegisterClass(&wc);
        wc.lpfnWndProc = RefListProc; wc.lpszClassName = "RefListClass"; RegisterClass(&wc);
    }

    hMain = CreateWindow("IconEditClass", "Win16 C Pro Vector Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 680, 560, NULL, NULL, hInstance, NULL);
    if (!hMain) return FALSE; ShowWindow(hMain, nCmdShow); UpdateWindow(hMain);
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return msg.wParam;
}
