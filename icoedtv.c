/* ============================================================================
 * PUBLIC DOMAIN NOTICE
 * Free and unencumbered software released into the public domain.
 * ============================================================================
 *
 * OPENWATCOM WIN16 C PORT (Windows 3.1x / 16-bit Target)
 * wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s icoedtv.c commdlg.lib
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
#define MAX_POINTS 128
#define MAX_SHAPES 50
#define MAX_UNDO 10
#define PANEL_WIDTH 340

/* C89 Math Macros */
#define fmax(a,b) (((a)>(b))?(a):(b))
#define fmin(a,b) (((a)<(b))?(a):(b))
#define round(x) ((double)((long)((x) + ((x)>=0 ? 0.5 : -0.5))))

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

Shape shapes[MAX_SHAPES]; 
int shapeCount = 0;
Shape history[MAX_UNDO][MAX_SHAPES]; 
int historyShapeCount[MAX_UNDO]; 
int undoIndex = -1;

Shape currentShape;
int isDrawing = 0, currentMode = 3; 
int selectedShape = -1;

/* Dragging & Selection State */
int dragType = 0; /* 0=none, 1=nodes, 2=shape */
double dragLastX = 0, dragLastY = 0;

int ptSelected[MAX_SHAPES][MAX_POINTS];
int selOrderS[MAX_POINTS], selOrderP[MAX_POINTS], selOrderCount = 0;

int startX = 0, startY = 0;
double currentEndX = 0, currentEndY = 0;
int snapToGrid = 1;

COLORREF palette[16] = {
    RGB(0,0,0), RGB(255,255,255), RGB(128,128,128), RGB(192,192,192),
    RGB(255,0,0), RGB(128,0,0), RGB(255,255,0), RGB(128,128,0),
    RGB(0,255,0), RGB(0,128,0), RGB(0,255,255), RGB(0,128,128),
    RGB(0,0,255), RGB(0,0,128), RGB(255,0,255), RGB(128,0,128)
};
COLORREF currentFill = RGB(128, 128, 128); int useFill = 1;
COLORREF currentStroke = RGB(0, 0, 0); int useStroke = 1;

HINSTANCE hInst = NULL;
HWND hMain, hBtn[26], hStatus;
HWND hScrlSides, hScrlDepth;
HWND hDistEdit = NULL; 
FARPROC oldEditProc = NULL;

int paramSides = 4, paramStar = 100;
int canvasSize = 320, scaleFactor = 10, clientW = 0, clientH = 0;

const char* const bT[26] = {
    "Select/Edit", "Rotate", "Scale", "Polygon", "Line",
    "Polyline", "Shapes", "Text", "Flood Fill", "Undo",
    "Clear", "Delete", "Import SVG", "Open Ref", "Open .C",
    "Save .C", "Export Code", "Merge", "Move Up", "Move Down",
    "Align Vert", "Align Horz", "Set Dist", "Set Width", "Set Height", "Duplicate"
};

/* --- Utilities & Selection --- */
void ShowStatus(const char* msg) {
    if (hStatus) SetWindowText(hStatus, msg);
}

void UpdateStatusBar(void) {
    char sb[128]; 
    sprintf(sb, " [Mode: %s] Shapes: %d | Nodes Sel: %d | Depth: %d%%", bT[currentMode], shapeCount, selOrderCount, paramStar);
    ShowStatus(sb);
}

void SaveState(void) { 
    int i;
    if (undoIndex >= MAX_UNDO - 1) {
        for(i = 0; i < MAX_UNDO - 1; i++) {
            historyShapeCount[i] = historyShapeCount[i+1];
            memcpy(history[i], history[i+1], sizeof(Shape)*MAX_SHAPES);
        }
        undoIndex = MAX_UNDO - 2;
    }
    undoIndex++; 
    historyShapeCount[undoIndex] = shapeCount; 
    memcpy(history[undoIndex], shapes, sizeof(Shape)*shapeCount); 
    UpdateStatusBar(); 
}

void Undo(HWND hwnd) { 
    if (undoIndex >= 0) { 
        shapeCount = historyShapeCount[undoIndex]; 
        memcpy(shapes, history[undoIndex], sizeof(Shape)*shapeCount); 
        undoIndex--; 
    } 
    UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); 
}

void ClearSelection(void) {
    memset(ptSelected, 0, sizeof(ptSelected));
    selOrderCount = 0;
    UpdateStatusBar();
}

void ToggleSelection(int s, int p) {
    int i, j;
    if (!ptSelected[s][p]) { 
        ptSelected[s][p] = 1; 
        selOrderS[selOrderCount] = s; 
        selOrderP[selOrderCount] = p; 
        selOrderCount++; 
    } else {
        ptSelected[s][p] = 0;
        for(i = 0; i < selOrderCount; i++) {
            if (selOrderS[i] == s && selOrderP[i] == p) {
                for(j = i; j < selOrderCount - 1; j++) { 
                    selOrderS[j] = selOrderS[j+1]; 
                    selOrderP[j] = selOrderP[j+1]; 
                }
                selOrderCount--; 
                break;
            }
        }
    }
    UpdateStatusBar();
}

/* --- Subclassed Edit Control --- */
LRESULT CALLBACK _export DistEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(GetParent(hwnd), WM_APP + 1, 0, 0L);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        ShowWindow(hwnd, SW_HIDE);
        SetFocus(GetParent(hwnd));
        return 0;
    }
    return CallWindowProc((FARPROC)oldEditProc, hwnd, msg, wParam, lParam);
}

/* --- Math & Geometry --- */
double Snap(double val) { return snapToGrid ? round(val) : val; }

void GenShape(double eX, double eY) {
    int i;
    double cx = (startX + eX) / 2.0, cy = (startY + eY) / 2.0; 
    double rx = fabs(eX - startX) / 2.0, ry = fabs(eY - startY) / 2.0;
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

/* --- GDI Rendering Functions --- */
void DrawGrid(HDC dc) {
    int i;
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
    HPEN hOldPen = SelectObject(dc, hPen);
    for (i = 0; i <= GRID_SIZE; i++) {
        MoveTo(dc, 0, i * scaleFactor);
        LineTo(dc, canvasSize, i * scaleFactor);
        MoveTo(dc, i * scaleFactor, 0);
        LineTo(dc, i * scaleFactor, canvasSize);
    }
    SelectObject(dc, hOldPen);
    DeleteObject(hPen);
}

void DrawPalette(HDC dc) {
    int i, col, row, cx = canvasSize + 15;
    HBRUSH hBr, hOldBr;
    HPEN hPen = (HPEN)GetStockObject(BLACK_PEN), hOldPen;
    
    hOldPen = SelectObject(dc, hPen);
    for (i = 0; i < 16; i++) {
        col = i % 8; row = i / 8;
        hBr = CreateSolidBrush(palette[i]);
        hOldBr = SelectObject(dc, hBr);
        Rectangle(dc, cx + col * 32, 356 + row * 16, cx + col * 32 + 32, 356 + row * 16 + 16);
        SelectObject(dc, hOldBr);
        DeleteObject(hBr);
    }
    
    hBr = useFill ? CreateSolidBrush(currentFill) : (HBRUSH)GetStockObject(NULL_BRUSH);
    hOldBr = SelectObject(dc, hBr);
    Rectangle(dc, cx, 396, cx + 32, 428);
    SelectObject(dc, hOldBr);
    if (useFill) DeleteObject(hBr);
    
    hBr = useStroke ? CreateSolidBrush(currentStroke) : (HBRUSH)GetStockObject(NULL_BRUSH);
    hOldBr = SelectObject(dc, hBr);
    Rectangle(dc, cx + 80, 396, cx + 112, 428);
    SelectObject(dc, hOldBr);
    if (useStroke) DeleteObject(hBr);
    
    SelectObject(dc, hOldPen);
    SetBkMode(dc, TRANSPARENT);
    TextOut(dc, cx + 40, 404, "Fill", 4);
    TextOut(dc, cx + 120, 404, "Stroke", 6);
    
    TextOut(dc, cx, 440, "Sides:", 6);
    TextOut(dc, cx, 465, "Depth:", 6);
}

void DrawNodes(HDC dc) {
    int j, px, py;
    HBRUSH hSelBr = CreateSolidBrush(RGB(255, 0, 0));   /* Red for selected */
    HBRUSH hUnselBr = CreateSolidBrush(RGB(0, 0, 255)); /* Blue for unselected */
    HBRUSH hOldBr;
    HPEN hOldPen = SelectObject(dc, GetStockObject(BLACK_PEN));
    
    if (selectedShape >= 0 && selectedShape < shapeCount) { 
        Shape* s = &shapes[selectedShape];
        for (j = 0; j < s->ptCount; j++) {
            px = (int)round(s->ptsX[j] * scaleFactor);
            py = (int)round(s->ptsY[j] * scaleFactor);
            hOldBr = SelectObject(dc, ptSelected[selectedShape][j] ? hSelBr : hUnselBr);
            Rectangle(dc, px - 3, py - 3, px + 4, py + 4);
            SelectObject(dc, hOldBr);
        }
    }
    SelectObject(dc, hOldPen);
    DeleteObject(hSelBr); DeleteObject(hUnselBr);
}

void RenderShapes(HDC dc, Shape* sArr, int sCnt, int sc, Shape* activeShape, int isActDrawing) {
    int i, j;
    for (i = 0; i <= sCnt; i++) {
        Shape* s = (i == sCnt) ? (isActDrawing ? activeShape : NULL) : &sArr[i]; 
        HBRUSH b; HPEN p; HGDIOBJ ob, op; POINT pA[MAX_POINTS];
        if (!s || s->ptCount == 0) continue;
        
        b = s->useFill ? CreateSolidBrush(s->fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
        p = s->useStroke ? CreatePen(PS_SOLID, 1, s->stroke) : (HPEN)GetStockObject(NULL_PEN);
        ob = SelectObject(dc, b); op = SelectObject(dc, p);
        
        for(j=0; j<s->ptCount; j++) { 
            pA[j].x = (int)round(s->ptsX[j] * sc); 
            pA[j].y = (int)round(s->ptsY[j] * sc); 
        }
        
        if (s->type == 0) { SetPolyFillMode(dc, ALTERNATE); Polygon(dc, pA, s->ptCount); } 
        else Polyline(dc, pA, s->ptCount);
        
        SelectObject(dc, ob); SelectObject(dc, op);
        if (s->useFill) DeleteObject(b); if (s->useStroke) DeleteObject(p);
    }
}

int HandlePaletteClick(HWND hwnd, int x, int y, int isLeft) {
    int cx = canvasSize + 15;
    if (x >= cx && x < cx + 256 && y >= 356 && y < 388) {
        int col = (x - cx) / 32, row = (y - 356) / 16, index = row * 8 + col;
        if (index >= 0 && index < 16) {
            if (isLeft) { currentFill = palette[index]; useFill = 1; }
            else { currentStroke = palette[index]; useStroke = 1; }
            if (selectedShape != -1) {
                SaveState();
                if (isLeft) { shapes[selectedShape].fill = currentFill; shapes[selectedShape].useFill = 1; }
                else { shapes[selectedShape].stroke = currentStroke; shapes[selectedShape].useStroke = 1; }
            }
            InvalidateRect(hwnd, NULL, FALSE); return 1;
        }
    }
    return 0;
}

/* --- Main Windows Procedure --- */
LRESULT CALLBACK _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    int i, j;

    switch (msg) {
        case WM_CREATE: {
            hStatus = CreateWindow("STATIC", " Ready", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)100, hInst, NULL);
            hScrlSides = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)153, hInst, NULL);
            hScrlDepth = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)154, hInst, NULL);
            
            SetScrollRange(hScrlSides, SB_CTL, 3, 32, FALSE);
            SetScrollPos(hScrlSides, SB_CTL, paramSides, TRUE);
            SetScrollRange(hScrlDepth, SB_CTL, 10, 100, FALSE);
            SetScrollPos(hScrlDepth, SB_CTL, paramStar, TRUE);
            
            for(i = 0; i < 26; i++) {
                hBtn[i] = CreateWindow("BUTTON", bT[i], WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)(200+i), hInst, NULL);
            }
            
            hDistEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 60, 20, hwnd, (HMENU)300, hInst, NULL);
            oldEditProc = (FARPROC)SetWindowLong(hDistEdit, GWL_WNDPROC, (LONG)(FARPROC)DistEditProc);
            break;
        }
        case WM_SIZE: {
            int botY, bottomAvailWidth, cx, w, by;
            clientW = LOWORD(lParam); clientH = HIWORD(lParam); 
            scaleFactor = (clientW - PANEL_WIDTH < clientH - 100) ? (clientW - PANEL_WIDTH) / GRID_SIZE : (clientH - 100) / GRID_SIZE;
            if (scaleFactor < 1) scaleFactor = 1; 
            canvasSize = scaleFactor * GRID_SIZE;
            
            cx = canvasSize + 15; w = PANEL_WIDTH - 30; by = 10;
            for(i = 0; i < 26; i++) MoveWindow(hBtn[i], cx + (i%2)*(w/2 + 2), by + (i/2)*26, (w/2)-4, 24, TRUE); 
            
            MoveWindow(hScrlSides, cx + 50, 440, w - 50, 18, TRUE);
            MoveWindow(hScrlDepth, cx + 50, 465, w - 50, 18, TRUE);
            
            botY = canvasSize + 15; bottomAvailWidth = canvasSize; 
            MoveWindow(hStatus, 0, clientH - 20, clientW, 20, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            
            if (HandlePaletteClick(hwnd, x, y, 1)) return 0;
            
            SetCapture(hwnd);
            startX = Snap(x / (double)scaleFactor);
            startY = Snap(y / (double)scaleFactor);
            
            if (currentMode == 0) { /* Edit Mode */
                int hitS = -1, hitP = -1, shift = GetKeyState(VK_SHIFT) & 0x8000;
                double minX, maxX, minY, maxY;
                
                /* Prioritize hitting nodes on the currently selected shape */
                if (selectedShape != -1) {
                    for (j = 0; j < shapes[selectedShape].ptCount; j++) {
                        double dx = shapes[selectedShape].ptsX[j] - startX;
                        double dy = shapes[selectedShape].ptsY[j] - startY;
                        if (sqrt(dx*dx + dy*dy) <= 1.0) { hitS = selectedShape; hitP = j; break; }
                    }
                }
                
                if (hitS != -1 && hitP != -1) {
                    /* Clicked a node */
                    if (shift) ToggleSelection(hitS, hitP);
                    else if (!ptSelected[hitS][hitP]) {
                        ClearSelection();
                        ToggleSelection(hitS, hitP);
                    }
                    dragType = 1; /* Drag Node(s) */
                    dragLastX = startX; dragLastY = startY;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                
                /* Test shape bounding boxes if we didn't hit a node */
                if (selectedShape >= 0 && selectedShape < shapeCount) {
                    minX = 9999; maxX = -9999; minY = 9999; maxY = -9999;
                    for(j = 0; j < shapes[selectedShape].ptCount; j++) {
                        minX = fmin(minX, shapes[selectedShape].ptsX[j]); maxX = fmax(maxX, shapes[selectedShape].ptsX[j]);
                        minY = fmin(minY, shapes[selectedShape].ptsY[j]); maxY = fmax(maxY, shapes[selectedShape].ptsY[j]);
                    }
                    if (startX >= minX && startX <= maxX && startY >= minY && startY <= maxY) {
                        dragType = 2; dragLastX = startX; dragLastY = startY;
                        return 0;
                    }
                }
                
                /* Clicked empty space */
                ClearSelection();
                selectedShape = -1;
                dragType = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (currentMode >= 3 && currentMode <= 6) {
                currentEndX = startX; currentEndY = startY;
                isDrawing = 1;
                currentShape.type = (currentMode == 4) ? 1 : 0;
                currentShape.useFill = useFill; currentShape.fill = currentFill;
                currentShape.useStroke = useStroke; currentShape.stroke = currentStroke;
                GenShape(currentEndX, currentEndY);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            double nx = Snap(LOWORD(lParam) / (double)scaleFactor);
            double ny = Snap(HIWORD(lParam) / (double)scaleFactor);
            
            if (currentMode == 0 && dragType > 0) {
                double dx = nx - dragLastX, dy = ny - dragLastY;
                if (dragType == 1) { /* Group Drag Selected Nodes */
                    for (i = 0; i < selOrderCount; i++) {
                        shapes[selOrderS[i]].ptsX[selOrderP[i]] += dx;
                        shapes[selOrderS[i]].ptsY[selOrderP[i]] += dy;
                    }
                } else if (dragType == 2) { /* Drag Full Shape */
                    for(j = 0; j < shapes[selectedShape].ptCount; j++) {
                        shapes[selectedShape].ptsX[j] += dx;
                        shapes[selectedShape].ptsY[j] += dy;
                    }
                }
                dragLastX = nx; dragLastY = ny;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (isDrawing) {
                currentEndX = nx; currentEndY = ny;
                GenShape(currentEndX, currentEndY);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (currentMode == 0 && dragType > 0) {
                dragType = 0; SaveState();
            }
            else if (isDrawing) {
                isDrawing = 0;
                if (currentShape.ptCount > 0 && shapeCount < MAX_SHAPES) {
                    SaveState();
                    shapes[shapeCount] = currentShape;
                    selectedShape = shapeCount; /* Auto select newly drawn shape */
                    shapeCount++; currentShape.ptCount = 0;
                    UpdateStatusBar();
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case WM_RBUTTONDOWN: {
            HandlePaletteClick(hwnd, LOWORD(lParam), HIWORD(lParam), 0);
            break;
        }
        case WM_HSCROLL: {
            UINT nScrollCode = wParam;
            int nPos = LOWORD(lParam);
            HWND hCtl = (HWND)HIWORD(lParam);
            int* pVal = NULL; int vMin = 0, vMax = 0;
            
            if (hCtl == hScrlSides) { pVal = &paramSides; vMin = 3; vMax = 32; }
            else if (hCtl == hScrlDepth) { pVal = &paramStar; vMin = 10; vMax = 100; }
            
            if (pVal) {
                if (nScrollCode == SB_LINELEFT) (*pVal)--;
                else if (nScrollCode == SB_LINERIGHT) (*pVal)++;
                else if (nScrollCode == SB_THUMBTRACK) *pVal = nPos;
                if (*pVal < vMin) *pVal = vMin;
                if (*pVal > vMax) *pVal = vMax;
                SetScrollPos(hCtl, SB_CTL, *pVal, TRUE);
                UpdateStatusBar();
            }
            break;
        }
        case WM_APP + 1: { /* Process Set Dist Input */
            if (selOrderCount == 2) {
                char buf[32];
                double newDist, oldDist, dx, dy, ratio;
                int s1 = selOrderS[0], p1 = selOrderP[0];
                int s2 = selOrderS[1], p2 = selOrderP[1];
                
                GetWindowText(hDistEdit, buf, 32);
                newDist = atof(buf);
                dx = shapes[s2].ptsX[p2] - shapes[s1].ptsX[p1];
                dy = shapes[s2].ptsY[p2] - shapes[s1].ptsY[p1];
                oldDist = sqrt(dx*dx + dy*dy);
                
                if (oldDist > 0.0001) {
                    SaveState();
                    ratio = newDist / oldDist;
                    shapes[s2].ptsX[p2] = shapes[s1].ptsX[p1] + dx * ratio;
                    shapes[s2].ptsY[p2] = shapes[s1].ptsY[p1] + dy * ratio;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            ShowWindow(hDistEdit, SW_HIDE);
            SetFocus(hwnd);
            break;
        }
        case WM_COMMAND: {
            UINT id = wParam;
            if (id >= 200 && id < 226) {
                int btnId = id - 200;
                if (btnId >= 0 && btnId <= 8) {
                    currentMode = btnId; isDrawing = 0; dragType = 0; ClearSelection();
                    UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE);
                }
                else if (btnId == 9) Undo(hwnd);
                else if (btnId == 10) { /* Clear */
                    SaveState(); shapeCount = 0; selectedShape = -1; ClearSelection();
                    UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); 
                }
                else if (btnId == 11) { /* Delete */
                    if (selectedShape >= 0 && selectedShape < shapeCount) {
                        SaveState();
                        for(i = selectedShape; i < shapeCount - 1; i++) shapes[i] = shapes[i+1];
                        shapeCount--; selectedShape = -1; ClearSelection();
                        UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                else if (btnId == 18) { /* Move Up */
                    if (selectedShape < shapeCount - 1 && selectedShape >= 0) {
                        Shape tmp = shapes[selectedShape];
                        shapes[selectedShape] = shapes[selectedShape+1];
                        shapes[selectedShape+1] = tmp;
                        selectedShape++;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                else if (btnId == 19) { /* Move Down */
                    if (selectedShape > 0) {
                        Shape tmp = shapes[selectedShape];
                        shapes[selectedShape] = shapes[selectedShape-1];
                        shapes[selectedShape-1] = tmp;
                        selectedShape--;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                else if (btnId == 22) { /* Set Dist */
                    if (selOrderCount == 2) {
                        int s1 = selOrderS[0], p1 = selOrderP[0];
                        int s2 = selOrderS[1], p2 = selOrderP[1];
                        double dx = shapes[s2].ptsX[p2] - shapes[s1].ptsX[p1];
                        double dy = shapes[s2].ptsY[p2] - shapes[s1].ptsY[p1];
                        int px = (int)round(shapes[s2].ptsX[p2] * scaleFactor) + 10;
                        int py = (int)round(shapes[s2].ptsY[p2] * scaleFactor) + 10;
                        char buf[32];
                        
                        sprintf(buf, "%.2f", sqrt(dx*dx + dy*dy));
                        SetWindowText(hDistEdit, buf);
                        MoveWindow(hDistEdit, px, py, 60, 20, TRUE);
                        ShowWindow(hDistEdit, SW_SHOW);
                        SetFocus(hDistEdit);
                    } else {
                        ShowStatus("Select exactly 2 nodes (Shift+Click) to set distance.");
                    }
                }
                else if (btnId == 25) { /* Duplicate */
                    if (selectedShape >= 0 && shapeCount < MAX_SHAPES) {
                        SaveState();
                        shapes[shapeCount] = shapes[selectedShape];
                        for (j = 0; j < shapes[shapeCount].ptCount; j++) {
                            shapes[shapeCount].ptsX[j] += 1.0;
                            shapes[shapeCount].ptsY[j] += 1.0;
                        }
                        selectedShape = shapeCount;
                        shapeCount++;
                        UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            DrawGrid(hdc);
            RenderShapes(hdc, shapes, shapeCount, scaleFactor, &currentShape, isDrawing);
            DrawNodes(hdc);
            DrawPalette(hdc);
            
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0L;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow) {
    MSG msg; WNDCLASS wc;
    hInst = hInstance;

    if (!hPrevInstance) {
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = 0; wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "IconEditClass";
        if (!RegisterClass(&wc)) return FALSE;
    }

    hMain = CreateWindow("IconEditClass", "Win16 Icon Vector Editor",
                         WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         680, 560, NULL, NULL, hInstance, NULL);

    if (!hMain) return FALSE;
    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}