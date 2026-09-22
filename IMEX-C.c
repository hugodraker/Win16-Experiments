/* ============================================================================
 * IMEX-C SVG/DXF to C and C to SVG/DXF Importer/Exporter - Win16 OpenWatcom
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s imex-c.c commdlg.lib shell.lib
 *
 * REQUIREMENTS: Windows 3.1x (Win16)
 * DEPENDENCIES: USER, GDI, COMDLG, SHELL
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <direct.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define MAX_POINTS 64
#define MAX_SHAPES 400
#define MAX_ICONS 100
#define MAX_REFS 20
#define MAX_DIMS 32

#ifndef CSV_MAX
#define CSV_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef CSV_MIN
#define CSV_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* --- Data Structures --- */
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
    int s1, p1, s2, p2; 
    double offset; 
    double textPos; 
    int mode; 
} Dimension;

typedef struct { 
    int caseId; 
    char pageName[64];
    char unitName[32];
    double pageScale;
    Shape FAR* shapes[MAX_SHAPES]; 
    int shapeCount; 
    Dimension dims[MAX_DIMS];
    int dimCount;
} IconDef;

typedef struct {
    char path[260];
    char unitName[32];
    Shape FAR* shapes[MAX_SHAPES];
    int shapeCount;
    Dimension dims[MAX_DIMS];
    int dimCount;
    double minX, minY, maxX, maxY;
} RefCache;

IconDef FAR* parsedIcons[MAX_ICONS];
int parsedCount = 0;
RefCache FAR* refCache[MAX_REFS];
int refCacheCount = 0;
char loadedCFile[260] = "";
int g_RenderRefDepth = 0;

Shape FAR* tempShapes[MAX_SHAPES];
int tempShapeCount = 0;
Dimension tempDims[MAX_DIMS];
int tempDimCount = 0;

/* --- GUI Globals --- */
HWND hMain, hTxtFile, hTxtOutFile, hBtnBrowse;
HWND hTxtScale;
HWND hBtnImportSVG, hBtnExportSVG;
HWND hBtnImportDXF, hBtnExportDXF, hBtnExit;
HWND hStatus;

/* --- Memory Cleanup --- */
void FreeAllData(void) {
    int i, j;
    for (i = 0; i < MAX_ICONS; i++) {
        if (parsedIcons[i]) {
            for (j = 0; j < parsedIcons[i]->shapeCount; j++) {
                if (parsedIcons[i]->shapes[j]) {
                    GlobalFreePtr(parsedIcons[i]->shapes[j]);
                    parsedIcons[i]->shapes[j] = NULL;
                }
            }
            GlobalFreePtr(parsedIcons[i]);
            parsedIcons[i] = NULL;
        }
    }
    parsedCount = 0;

    for (i = 0; i < MAX_REFS; i++) {
        if (refCache[i]) {
            for (j = 0; j < refCache[i]->shapeCount; j++) {
                if (refCache[i]->shapes[j]) {
                    GlobalFreePtr(refCache[i]->shapes[j]);
                    refCache[i]->shapes[j] = NULL;
                }
            }
            GlobalFreePtr(refCache[i]);
            refCache[i] = NULL;
        }
    }
    refCacheCount = 0;
}

void FreeTempShapes(void) {
    int i;
    for (i = 0; i < tempShapeCount; i++) {
        if (tempShapes[i]) {
            GlobalFreePtr(tempShapes[i]);
            tempShapes[i] = NULL;
        }
    }
    tempShapeCount = 0;
}

/* --- Text & Path Utilities --- */
void sanitize_pdf_string(char* str) {
    while(*str) {
        if(*str == '(' || *str == ')' || *str == '\\') *str = '_';
        str++;
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

char* FindQuote(char* start) {
    char* p = start;
    while (*p) {
        if (*p == '\\' && *(p+1) == '"') { p += 2; continue; }
        if (*p == '"') return p;
        p++;
    }
    return NULL;
}

void GetPipeValue(const char* pipeStr, int idx, char* out, int maxLen) {
    int curr = 0; const char *start = pipeStr, *p = pipeStr; out[0] = '\0';
    if (!pipeStr) return;
    while (*p) {
        if (*p == '|') { if (curr == idx) break; curr++; start = p + 1; }
        p++;
    }
    if (curr == idx) {
        int len = p - start; if (len > maxLen - 1) len = maxLen - 1;
        strncpy(out, start, len); out[len] = '\0';
    }
}

void ResolvePath(const char* base, const char* rel, char* out) {
    char tempRel[260]; char* p; char* lastSlash;
    if (!rel || !rel[0]) { if (out) out[0] = '\0'; return; }
    if ((rel[0] && rel[1] == ':') || rel[0] == '\\' || rel[0] == '/') { strncpy(out, rel, 259); out[259] = '\0'; return; }
    if (!base || !base[0]) { strncpy(out, rel, 259); out[259] = '\0'; return; }
    strncpy(out, base, 259); out[259] = '\0';
    lastSlash = strrchr(out, '\\'); if (!lastSlash) lastSlash = strrchr(out, '/');
    if (lastSlash) *(lastSlash + 1) = '\0'; else out[0] = '\0';
    strncpy(tempRel, rel, 259); tempRel[259] = '\0'; p = tempRel;
    while (strncmp(p, "..\\", 3) == 0 || strncmp(p, "../", 3) == 0) {
        p += 3;
        if (strlen(out) > 0) {
            out[strlen(out) - 1] = '\0';
            lastSlash = strrchr(out, '\\'); if (!lastSlash) lastSlash = strrchr(out, '/');
            if (lastSlash) *(lastSlash + 1) = '\0'; else out[0] = '\0';
        }
    }
    strncat(out, p, 259 - strlen(out));
}

void FormatDimension(double val, const char* unit, char* outBuf) {
    if (val < 0.0) val = 0.0;
    if (unit == NULL) unit = "None";
    
    if (stricmp(unit, "feet-inches") == 0) {
        int feet = (int)(val / 12.0);
        double inches = val - (feet * 12.0);
        if (fabs(inches - floor(inches + 0.5)) < 0.001) {
            sprintf(outBuf, "%d'-%d\"", feet, (int)floor(inches + 0.5));
        } else if (fabs(inches * 10.0 - floor(inches * 10.0 + 0.5)) < 0.001) {
            sprintf(outBuf, "%d'-%.1f\"", feet, inches);
        } else {
            sprintf(outBuf, "%d'-%.2f\"", feet, inches);
        }
    } else if (stricmp(unit, "inches") == 0) {
        if (fabs(val - floor(val + 0.5)) < 0.001) sprintf(outBuf, "%d\"", (int)floor(val + 0.5));
        else sprintf(outBuf, "%.2f\"", val);
    } else if (stricmp(unit, "None") == 0 || unit[0] == '\0') {
        if (fabs(val - floor(val + 0.5)) < 0.001) sprintf(outBuf, "%d", (int)floor(val + 0.5));
        else sprintf(outBuf, "%.2f", val);
    } else {
        if (fabs(val - floor(val + 0.5)) < 0.001) sprintf(outBuf, "%d %s", (int)floor(val + 0.5), unit);
        else sprintf(outBuf, "%.2f %s", val, unit);
    }
}

/* --- Stream-Based C Parsing Engine --- */
void SilentLoadC(const char* path, RefCache FAR* ref) {
    FILE* f; 
    static char line[512]; 
    static char stmtBuf[2048];
    COLORREF curF = RGB(128,128,128); int curUseF = 1;
    COLORREF curS = RGB(0,0,0); int curUseS = 1; int curSW = 1;

    ref->shapeCount = 0; ref->dimCount = 0;
    strcpy(ref->unitName, "mm");

    f = fopen(path, "r");
    if (!f) return;

    stmtBuf[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '/' || *p == '*') continue;

        strncat(stmtBuf, p, 2047 - strlen(stmtBuf));

        if (strchr(stmtBuf, ';') || strchr(stmtBuf, '}')) {
            char *st = stmtBuf;
            char *pt = strstr(st, "POINT ");
            char *lStr = strstr(st, "L(");
            char *ext = strstr(st, "EXT_REF(");
            char *tag = strstr(st, "TAG_TEXT(");
            char *dim = strstr(st, "DIMENSION(");
            char *pgDef = strstr(st, "PAGE_DEF(");

            if (strstr(st, "CreateSolidBrush")) {
                int cr, cg, cb;
                if (sscanf(strstr(st, "CreateSolidBrush"), "CreateSolidBrush(RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) {
                    curF = RGB(cr, cg, cb); curUseF = 1;
                }
            }
            if (strstr(st, "CreatePen")) {
                int sw, cr, cg, cb;
                if (sscanf(strstr(st, "CreatePen"), "CreatePen(PS_SOLID, %d, RGB(%d,%d,%d))", &sw, &cr, &cg, &cb) == 4) {
                    curS = RGB(cr, cg, cb); curUseS = 1; curSW = sw;
                } else if (sscanf(strstr(st, "CreatePen"), "CreatePen(PS_SOLID, 1, RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) {
                    curS = RGB(cr, cg, cb); curUseS = 1; curSW = 1;
                }
            }
            if (strstr(st, "DeleteObject(hBr)")) curUseF = 0;
            if (strstr(st, "DeleteObject(hPen)")) curUseS = 0;

            if (pgDef) {
                char* q[6]; int q_cnt = 0; char* pC = pgDef;
                while (pC && q_cnt < 6) {
                    q[q_cnt] = FindQuote(pC);
                    if (q[q_cnt]) pC = q[q_cnt] + 1; else break;
                    q_cnt++;
                }
                if (q_cnt >= 6) {
                    int l2 = q[5] - (q[4] + 1); if (l2 > 31) l2 = 31;
                    strncpy(ref->unitName, q[4] + 1, l2); ref->unitName[l2] = '\0';
                } else if (q_cnt >= 4) {
                    int l2 = q[3] - (q[2] + 1); if (l2 > 31) l2 = 31;
                    strncpy(ref->unitName, q[2] + 1, l2); ref->unitName[l2] = '\0';
                }
            } else if (dim) {
                int s1, p1, s2, p2, md; double off, tp;
                if (sscanf(dim, "DIMENSION(%d , %d , %d , %d , %lf , %lf , %d)", &s1, &p1, &s2, &p2, &off, &tp, &md) == 7) {
                    if (ref->dimCount < MAX_DIMS) {
                        ref->dims[ref->dimCount].s1 = s1; ref->dims[ref->dimCount].p1 = p1;
                        ref->dims[ref->dimCount].s2 = s2; ref->dims[ref->dimCount].p2 = p2;
                        ref->dims[ref->dimCount].offset = off; ref->dims[ref->dimCount].textPos = tp;
                        ref->dims[ref->dimCount].mode = md; ref->dimCount++;
                    }
                }
            } else if (ref->shapeCount < MAX_SHAPES && (pt || lStr || ext || tag)) {
                Shape FAR* s;
                if (!ref->shapes[ref->shapeCount]) {
                    ref->shapes[ref->shapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (!ref->shapes[ref->shapeCount]) break;
                }
                s = ref->shapes[ref->shapeCount];
                
                memset(s, 0, sizeof(Shape));
                s->useFill = curUseF; s->useStroke = curUseS; 
                s->fill = curF; s->stroke = curS;
                s->strokeWidth = curSW; s->fontSize = 24;

                if (pt) {
                    char *pC = pt, *bracket = strchr(pt, '}');
                    char *polyChk = strstr(st, "Polyline(");
                    s->type = (polyChk) ? 2 : 0;
                    while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) {
                        char* comma = strchr(pC + 3, ',');
                        if (comma && comma < bracket && s->ptCount < MAX_POINTS) {
                            s->ptsX[s->ptCount] = atof(pC + 3); s->ptsY[s->ptCount] = atof(comma + 1); s->ptCount++;
                        }
                        pC += 3;
                    }
                    if (s->ptCount > 0) ref->shapeCount++;
                } else if (lStr) {
                    s->type = 1; s->ptCount = 2;
                    if (sscanf(lStr, "L(%lf,%lf,%lf,%lf)", &s->ptsX[0], &s->ptsY[0], &s->ptsX[1], &s->ptsY[1]) == 4) ref->shapeCount++;
                } else if (ext) {
                    static char refStr[512]; static char tagStr[512]; tagStr[0] = '\0';
                    char* pOpen = strchr(ext, '(');
                    char* q1 = pOpen ? FindQuote(pOpen) : NULL;
                    char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                    if (pOpen && q1 && q2) {
                        char* comma1 = strchr(pOpen + 1, ',');
                        int rLen = q2 - (q1 + 1); if (rLen > 511) rLen = 511;
                        strncpy(refStr, q1 + 1, rLen); refStr[rLen] = '\0';
                        char* comma3 = q2 ? strchr(q2 + 1, ',') : NULL;
                        if (comma3) {
                            char* q3 = FindQuote(comma3); char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                            if (q3 && q4) {
                                int tLen = q4 - (q3 + 1); if (tLen > 511) tLen = 511;
                                strncpy(tagStr, q3 + 1, tLen); tagStr[tLen] = '\0';
                            }
                        }
                        s->type = 3; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = comma1 ? atof(comma1 + 1) : 0.0;
                        s->ptCount = 1; strcpy(s->text, refStr); 
                        UnescapeCString(tagStr, s->tagData, 512);
                        ref->shapeCount++;
                    }
                } else if (tag) {
                    char* pOpen = strchr(tag, '('); char* q1 = pOpen ? FindQuote(pOpen) : NULL; char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                    if (pOpen) {
                        char* comma1 = strchr(tag, ',');
                        if (comma1 && (!q1 || comma1 < q1)) {
                            s->type = 4; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = atof(comma1 + 1); s->ptCount = 1;
                            char* comma2 = strchr(comma1 + 1, ','); char* pClose = strchr(comma2 ? comma2 : pOpen, ')');
                            if (q1 && q2 && q1 < pClose) {
                                int tLen = q2 - (q1 + 1); if (tLen > 511) tLen = 511;
                                static char rawTag[512]; strncpy(rawTag, q1 + 1, tLen); rawTag[tLen] = '\0';
                                UnescapeCString(rawTag, s->text, 512);
                                char* commaAfter = strchr(q2, ',');
                                if (commaAfter && commaAfter < pClose) {
                                    char jChar = 'L'; char* jPtr = commaAfter + 1;
                                    while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                    if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                    char* comma4 = strchr(commaAfter + 1, ',');
                                    if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                                } else s->useFill = 0;
                            } else if (comma2) {
                                char* comma3 = strchr(comma2 + 1, ','); char* endMarker = comma3 ? comma3 : pClose;
                                if (endMarker) {
                                    int tLen = endMarker - (comma2 + 1); if (tLen > 511) tLen = 511;
                                    static char rawTag[512]; strncpy(rawTag, comma2 + 1, tLen); rawTag[tLen] = '\0';
                                    char* start = rawTag; while(*start && isspace((unsigned char)*start)) start++;
                                    char* end = start + strlen(start) - 1; while(end > start && isspace((unsigned char)*end)) *end-- = '\0';
                                    UnescapeCString(start, s->text, 512);
                                    if (comma3) {
                                        char jChar = 'L'; char* jPtr = comma3 + 1;
                                        while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                        if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                        char* comma4 = strchr(comma3 + 1, ',');
                                        if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                                    } else s->useFill = 0;
                                }
                            }
                            ref->shapeCount++;
                        }
                    }
                }
            }
            stmtBuf[0] = '\0';
        }
    }
    fclose(f);
}

int EnsureRefLoaded(const char* path) {
    int i, j; double minX, minY, maxX, maxY;
    if (!path || !path[0]) return -1;
    for (i = 0; i < refCacheCount; i++) if (refCache[i] && stricmp(refCache[i]->path, path) == 0) return i;
    if (refCacheCount >= MAX_REFS) return -1;
    
    if (!refCache[refCacheCount]) {
        refCache[refCacheCount] = (RefCache FAR*)GlobalAllocPtr(GHND, sizeof(RefCache));
        if (!refCache[refCacheCount]) return -1;
        memset(refCache[refCacheCount], 0, sizeof(RefCache));
    }
    strncpy(refCache[refCacheCount]->path, path, 259); refCache[refCacheCount]->path[259] = '\0';
    SilentLoadC(path, refCache[refCacheCount]);
    
    minX = 99999.0; minY = 99999.0; maxX = -99999.0; maxY = -99999.0;
    for (i = 0; i < refCache[refCacheCount]->shapeCount; i++) {
        Shape FAR* sh = refCache[refCacheCount]->shapes[i];
        for (j = 0; j < sh->ptCount; j++) {
            minX = fmin(minX, sh->ptsX[j]); maxX = fmax(maxX, sh->ptsX[j]);
            minY = fmin(minY, sh->ptsY[j]); maxY = fmax(maxY, sh->ptsY[j]);
        }
    }
    if (minX > maxX) { minX = 0; minY = 0; maxX = 32; maxY = 32; }
    refCache[refCacheCount]->minX = minX; refCache[refCacheCount]->minY = minY;
    refCache[refCacheCount]->maxX = maxX; refCache[refCacheCount]->maxY = maxY;
    
    return refCacheCount++;
}

int LoadCFile(const char* path) {
    FILE* f; 
    static char line[512]; 
    static char stmtBuf[2048];
    static Shape FAR* tmpShapesArr[MAX_SHAPES];
    int cId = 1, tmpCount = 0;
    int i;
    char tmpPageName[64] = "A4 (210x297)";
    char tmpUnitName[32] = "mm";
    double tmpPageScale = 1.0;
    COLORREF curF = RGB(128,128,128); int curUseF = 1;
    COLORREF curS = RGB(0,0,0); int curUseS = 1; int curSW = 1;

    memset(tmpShapesArr, 0, MAX_SHAPES * sizeof(Shape FAR*));
    FreeAllData();

    strcpy(loadedCFile, path);
    f = fopen(path, "r");
    if (!f) { loadedCFile[0] = '\0'; return 0; }

    stmtBuf[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '/' || *p == '*') continue;

        strncat(stmtBuf, p, 2047 - strlen(stmtBuf));

        if (strchr(stmtBuf, ';') || strchr(stmtBuf, '}')) {
            char *st = stmtBuf;
            char *pt = strstr(st, "POINT ");
            char *lStr = strstr(st, "L(");
            char *ext = strstr(st, "EXT_REF(");
            char *tag = strstr(st, "TAG_TEXT(");
            char *dim = strstr(st, "DIMENSION(");
            char *pgDef = strstr(st, "PAGE_DEF(");

            if (strncmp(st, "case ", 5) == 0) {
                cId = atoi(st + 5);
                tmpCount = 0; tempDimCount = 0;
                strcpy(tmpPageName, "A4 (210x297)"); strcpy(tmpUnitName, "mm"); tmpPageScale = 1.0;
                memset(tmpShapesArr, 0, MAX_SHAPES * sizeof(Shape FAR*));
                curF = RGB(128,128,128); curUseF = 1;
                curS = RGB(0,0,0); curUseS = 1; curSW = 1;
            } else if (strncmp(st, "break;", 6) == 0) {
                if (parsedCount < MAX_ICONS) {
                    parsedIcons[parsedCount] = (IconDef FAR*)GlobalAllocPtr(GHND, sizeof(IconDef));
                    if (parsedIcons[parsedCount]) {
                        memset(parsedIcons[parsedCount], 0, sizeof(IconDef));
                        parsedIcons[parsedCount]->caseId = cId;
                        strcpy(parsedIcons[parsedCount]->pageName, tmpPageName);
                        strcpy(parsedIcons[parsedCount]->unitName, tmpUnitName);
                        parsedIcons[parsedCount]->pageScale = tmpPageScale;
                        
                        for (i = 0; i < tmpCount; i++) {
                            parsedIcons[parsedCount]->shapes[i] = tmpShapesArr[i];
                            tmpShapesArr[i] = NULL;
                        }
                        parsedIcons[parsedCount]->shapeCount = tmpCount;
                        
                        parsedIcons[parsedCount]->dimCount = tempDimCount;
                        if (tempDimCount > 0) {
                            memcpy(parsedIcons[parsedCount]->dims, tempDims, tempDimCount * sizeof(Dimension));
                        }
                        parsedCount++;
                    }
                }
            } else if (pgDef) {
                char* q[6]; int q_cnt = 0; char* pC = pgDef;
                while (pC && q_cnt < 6) {
                    q[q_cnt] = FindQuote(pC);
                    if (q[q_cnt]) pC = q[q_cnt] + 1; else break;
                    q_cnt++;
                }
                if (q_cnt >= 6) {
                    int l1 = q[3] - (q[2] + 1); if (l1 > 63) l1 = 63;
                    strncpy(tmpPageName, q[2] + 1, l1); tmpPageName[l1] = '\0';
                    int l2 = q[5] - (q[4] + 1); if (l2 > 31) l2 = 31;
                    strncpy(tmpUnitName, q[4] + 1, l2); tmpUnitName[l2] = '\0';
                    char* comma = strchr(q[5] + 1, ',');
                    if (comma) tmpPageScale = atof(comma + 1);
                } else if (q_cnt >= 4) {
                    int l1 = q[1] - (q[0] + 1); if (l1 > 63) l1 = 63;
                    strncpy(tmpPageName, q[0] + 1, l1); tmpPageName[l1] = '\0';
                    int l2 = q[3] - (q[2] + 1); if (l2 > 31) l2 = 31;
                    strncpy(tmpUnitName, q[2] + 1, l2); tmpUnitName[l2] = '\0';
                    char* comma = strchr(q[3] + 1, ',');
                    if (comma) tmpPageScale = atof(comma + 1);
                }
            } else if (dim) {
                int s1, p1, s2, p2, md; double off, tp;
                if (sscanf(dim, "DIMENSION(%d , %d , %d , %d , %lf , %lf , %d)", &s1, &p1, &s2, &p2, &off, &tp, &md) == 7) {
                    if (tempDimCount < MAX_DIMS) {
                        tempDims[tempDimCount].s1 = s1; tempDims[tempDimCount].p1 = p1;
                        tempDims[tempDimCount].s2 = s2; tempDims[tempDimCount].p2 = p2;
                        tempDims[tempDimCount].offset = off; tempDims[tempDimCount].textPos = tp;
                        tempDims[tempDimCount].mode = md; tempDimCount++;
                    }
                }
            } else {
                if (strstr(st, "CreateSolidBrush")) {
                    int cr, cg, cb;
                    if (sscanf(strstr(st, "CreateSolidBrush"), "CreateSolidBrush(RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) {
                        curF = RGB(cr, cg, cb); curUseF = 1;
                    }
                }
                if (strstr(st, "CreatePen")) {
                    int sw, cr, cg, cb;
                    if (sscanf(strstr(st, "CreatePen"), "CreatePen(PS_SOLID, %d, RGB(%d,%d,%d))", &sw, &cr, &cg, &cb) == 4) {
                        curS = RGB(cr, cg, cb); curUseS = 1; curSW = sw;
                    } else if (sscanf(strstr(st, "CreatePen"), "CreatePen(PS_SOLID, 1, RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) {
                        curS = RGB(cr, cg, cb); curUseS = 1; curSW = 1;
                    }
                }
                if (strstr(st, "DeleteObject(hBr)")) curUseF = 0;
                if (strstr(st, "DeleteObject(hPen)")) curUseS = 0;

                if (tmpCount < MAX_SHAPES && (pt || lStr || ext || tag)) {
                    Shape FAR* s;
                    if (!tmpShapesArr[tmpCount]) {
                        tmpShapesArr[tmpCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    }
                    s = tmpShapesArr[tmpCount];
                    if (!s) break;

                    memset(s, 0, sizeof(Shape));
                    s->useFill = curUseF; s->useStroke = curUseS; 
                    s->fill = curF; s->stroke = curS;
                    s->strokeWidth = curSW; s->fontSize = 24;

                    if (pt) {
                        char *pC = pt, *bracket = strchr(pt, '}');
                        char *polyChk = strstr(st, "Polyline(");
                        s->type = (polyChk) ? 2 : 0;
                        while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) {
                            char* comma = strchr(pC + 3, ',');
                            if (comma && comma < bracket && s->ptCount < MAX_POINTS) {
                                s->ptsX[s->ptCount] = atof(pC + 3); s->ptsY[s->ptCount] = atof(comma + 1); s->ptCount++;
                            }
                            pC += 3;
                        }
                        if (s->ptCount > 0) tmpCount++;
                    } else if (lStr) {
                        s->type = 1; s->ptCount = 2;
                        if (sscanf(lStr, "L(%lf,%lf,%lf,%lf)", &s->ptsX[0], &s->ptsY[0], &s->ptsX[1], &s->ptsY[1]) == 4) tmpCount++;
                    } else if (ext) {
                        static char refStr[512]; static char tagStr[512]; tagStr[0] = '\0';
                        char* pOpen = strchr(ext, '(');
                        char* q1 = pOpen ? FindQuote(pOpen) : NULL;
                        char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                        if (pOpen && q1 && q2) {
                            char* comma1 = strchr(pOpen + 1, ',');
                            int rLen = q2 - (q1 + 1); if (rLen > 511) rLen = 511;
                            strncpy(refStr, q1 + 1, rLen); refStr[rLen] = '\0';
                            char* comma3 = q2 ? strchr(q2 + 1, ',') : NULL;
                            if (comma3) {
                                char* q3 = FindQuote(comma3); char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                                if (q3 && q4) {
                                    int tLen = q4 - (q3 + 1); if (tLen > 511) tLen = 511;
                                    strncpy(tagStr, q3 + 1, tLen); tagStr[tLen] = '\0';
                                }
                            }
                            s->type = 3; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = comma1 ? atof(comma1 + 1) : 0.0;
                            s->ptCount = 1; strcpy(s->text, refStr); 
                            UnescapeCString(tagStr, s->tagData, 512);
                            tmpCount++;
                        }
                    } else if (tag) {
                        char* pOpen = strchr(tag, '('); char* q1 = pOpen ? FindQuote(pOpen) : NULL; char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                        if (pOpen) {
                            char* comma1 = strchr(tag, ',');
                            if (comma1 && (!q1 || comma1 < q1)) {
                                s->type = 4; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = atof(comma1 + 1); s->ptCount = 1;
                                char* comma2 = strchr(comma1 + 1, ','); char* pClose = strchr(comma2 ? comma2 : pOpen, ')');
                                if (q1 && q2 && q1 < pClose) {
                                    int tLen = q2 - (q1 + 1); if (tLen > 511) tLen = 511;
                                    static char rawTag[512]; strncpy(rawTag, q1 + 1, tLen); rawTag[tLen] = '\0';
                                    UnescapeCString(rawTag, s->text, 512);
                                    char* commaAfter = strchr(q2, ',');
                                    if (commaAfter && commaAfter < pClose) {
                                        char jChar = 'L'; char* jPtr = commaAfter + 1;
                                        while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                        if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                        char* comma4 = strchr(commaAfter + 1, ',');
                                        if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                                    } else s->useFill = 0;
                                } else if (comma2) {
                                    char* comma3 = strchr(comma2 + 1, ','); char* endMarker = comma3 ? comma3 : pClose;
                                    if (endMarker) {
                                        int tLen = endMarker - (comma2 + 1); if (tLen > 511) tLen = 511;
                                        static char rawTag[512]; strncpy(rawTag, comma2 + 1, tLen); rawTag[tLen] = '\0';
                                        char* start = rawTag; while(*start && isspace((unsigned char)*start)) start++;
                                        char* end = start + strlen(start) - 1; while(end > start && isspace((unsigned char)*end)) *end-- = '\0';
                                        UnescapeCString(start, s->text, 512);
                                        if (comma3) {
                                            char jChar = 'L'; char* jPtr = comma3 + 1;
                                            while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++; jChar = toupper((unsigned char)*jPtr);
                                            if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                            char* comma4 = strchr(comma3 + 1, ',');
                                            if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                                        } else s->useFill = 0;
                                    }
                                }
                                tmpCount++;
                            }
                        }
                    }
                }
            }
            stmtBuf[0] = '\0';
        }
    }
    fclose(f);
    
    for (i = 0; i < MAX_SHAPES; i++) {
        if (tmpShapesArr[i]) GlobalFreePtr(tmpShapesArr[i]);
    }
    
    return parsedCount;
}

void WriteShapeToC(FILE* f, Shape FAR* s, int j) {
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
        fprintf(f, "    {\n");
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
        fprintf(f, "    }\n");
    }
}

/* --- DXF Importer/Exporter --- */
void ExportCtoDXF(const char* c_path, const char* save_path, double scale) {
    char drive[3], dir[260], fname[260], ext[16]; int i, j, k;
    if (!LoadCFile(c_path)) { SetWindowText(hStatus, " Error: Failed to load C file."); return; }
    if (parsedCount <= 0) { SetWindowText(hStatus, " Error: No icons found in C file."); return; }
    
    _splitpath(save_path, drive, dir, fname, ext);
    if (!ext[0]) strcpy(ext, ".dxf");
    
    for (i = 0; i < parsedCount; i++) {
        char final_path[MAX_PATH]; FILE* f;
        if (parsedCount == 1) strcpy(final_path, save_path);
        else {
            char numStr[16]; char newFname[260]; int maxNameLen;
            sprintf(numStr, "%d", i+1); maxNameLen = 8 - strlen(numStr);
            if (maxNameLen < 1) maxNameLen = 1;
            strncpy(newFname, fname, maxNameLen); newFname[maxNameLen] = '\0';
            strcat(newFname, numStr); _makepath(final_path, drive, dir, newFname, ext);
        }
        
        f = fopen(final_path, "w");
        if (!f) continue;
        
        /* Highly Compatible Minimal DXF Header */
        fprintf(f, "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1009\n  0\nENDSEC\n");
        fprintf(f, "  0\nSECTION\n  2\nTABLES\n  0\nENDSEC\n");
        fprintf(f, "  0\nSECTION\n  2\nBLOCKS\n  0\nENDSEC\n");
        fprintf(f, "  0\nSECTION\n  2\nENTITIES\n");
        
        for (j = 0; j < parsedIcons[i]->shapeCount; j++) {
            Shape FAR* sh = parsedIcons[i]->shapes[j];
            if (sh->type == 1 && sh->ptCount >= 2) {
                fprintf(f, "  0\nLINE\n  8\n0\n 10\n%f\n 20\n%f\n 30\n0.0\n 11\n%f\n 21\n%f\n 31\n0.0\n", 
                    sh->ptsX[0]*scale, sh->ptsY[0]*scale, sh->ptsX[1]*scale, sh->ptsY[1]*scale);
            } else if (sh->type == 0 || sh->type == 2) {
                if (sh->ptCount >= 2) {
                    /* 70 Polyline flag: 1 = closed, 0 = open. 66 = 1 (vertices follow) */
                    fprintf(f, "  0\nPOLYLINE\n  8\n0\n 66\n1\n 70\n%d\n 10\n0.0\n 20\n0.0\n 30\n0.0\n", sh->type == 0 ? 1 : 0);
                    for (k = 0; k < sh->ptCount; k++) {
                        fprintf(f, "  0\nVERTEX\n  8\n0\n 10\n%f\n 20\n%f\n 30\n0.0\n", sh->ptsX[k]*scale, sh->ptsY[k]*scale);
                    }
                    fprintf(f, "  0\nSEQEND\n  8\n0\n");
                }
            } else if (sh->type == 3 || sh->type == 4) {
                char safeText[512];
                char *pText;
                strncpy(safeText, sh->text, 511);
                safeText[511] = '\0';
                if (strlen(safeText) == 0) strcpy(safeText, " ");
                for (pText = safeText; *pText; pText++) {
                    if (*pText == '\r' || *pText == '\n') *pText = ' ';
                }

                fprintf(f, "  0\nTEXT\n  8\n0\n 10\n%f\n 20\n%f\n 30\n0.0\n 40\n%f\n  1\n%s\n", 
                    sh->ptsX[0]*scale, sh->ptsY[0]*scale, (sh->type == 3 ? 10.0 : (sh->fontSize>0?sh->fontSize:24.0))*scale, safeText);
            }
        }
        
        for (j = 0; j < parsedIcons[i]->dimCount; j++) {
            Dimension* d = &parsedIcons[i]->dims[j];
            char dimStr[64]; double val = 0.0;
            
            if (d->s1 >= 0 && d->s1 < parsedIcons[i]->shapeCount && d->s2 >= 0 && d->s2 < parsedIcons[i]->shapeCount) {
                double lDx = parsedIcons[i]->shapes[d->s2]->ptsX[d->p2] - parsedIcons[i]->shapes[d->s1]->ptsX[d->p1];
                double lDy = parsedIcons[i]->shapes[d->s2]->ptsY[d->p2] - parsedIcons[i]->shapes[d->s1]->ptsY[d->p1];
                if (d->mode == 0) val = sqrt(lDx*lDx + lDy*lDy);
                else if (d->mode == 1) val = fabs(lDx);
                else val = fabs(lDy);
            }
            FormatDimension(val, parsedIcons[i]->unitName, dimStr);
            if (strlen(dimStr) == 0) strcpy(dimStr, " ");
            
            fprintf(f, "  0\nTEXT\n  8\nDIM_DEF\n 10\n0.0\n 20\n0.0\n 30\n0.0\n 40\n10.0\n  1\nDIMENSION(%d, %d, %d, %d, %f, %f, %d)\n",
                d->s1, d->p1, d->s2, d->p2, d->offset, d->textPos, d->mode);
            fprintf(f, "  0\nTEXT\n  8\nDIMENSIONS\n 10\n0.0\n 20\n0.0\n 30\n0.0\n 40\n10.0\n  1\n%s\n", dimStr);
        }
        
        fprintf(f, "  0\nENDSEC\n  0\nEOF\n");
        fclose(f);
    }
    SetWindowText(hStatus, " Success: DXF Exported!");
}

int LoadDXF(const char* path, double scale) {
    FILE* f; char line[256]; int code; char val[256];
    int inEntity = 0; char entType[32] = "";
    Shape s; char* nl; char* pCode; char* pVal;
    
    FreeTempShapes();
    f = fopen(path, "r"); if (!f) return 0;
    
    memset(&s, 0, sizeof(Shape));
    
    while (fgets(line, sizeof(line), f)) {
        pCode = line; while (*pCode == ' ' || *pCode == '\t') pCode++;
        code = atoi(pCode);
        
        if (!fgets(line, sizeof(line), f)) break;
        nl = strpbrk(line, "\r\n"); if (nl) *nl = '\0';
        
        pVal = line; while (*pVal == ' ' || *pVal == '\t') pVal++;
        strcpy(val, pVal);
        
        if (code == 0) {
            if (inEntity) {
                if (strcmp(val, "VERTEX") == 0 || strcmp(val, "SEQEND") == 0) {
                    /* Continue accumulating the current polyline context */
                } else {
                    if (strcmp(entType, "LINE") == 0 && s.ptCount == 2) {
                        s.type = 1; 
                        if (tempShapeCount < MAX_SHAPES) {
                            tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                            if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                        }
                    } else if ((strcmp(entType, "POLYLINE") == 0 || strcmp(entType, "LWPOLYLINE") == 0) && s.ptCount >= 2) {
                        if (tempShapeCount < MAX_SHAPES) {
                            tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                            if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                        }
                    } else if (strcmp(entType, "TEXT") == 0) {
                        if (strncmp(s.text, "DIMENSION(", 10) == 0) {
                            int s1, p1, s2, p2, md; double off, tp;
                            if (sscanf(s.text, "DIMENSION(%d , %d , %d , %d , %lf , %lf , %d)", &s1, &p1, &s2, &p2, &off, &tp, &md) == 7) {
                                if (tempDimCount < MAX_DIMS) {
                                    tempDims[tempDimCount].s1 = s1; tempDims[tempDimCount].p1 = p1;
                                    tempDims[tempDimCount].s2 = s2; tempDims[tempDimCount].p2 = p2;
                                    tempDims[tempDimCount].offset = off; tempDims[tempDimCount].textPos = tp;
                                    tempDims[tempDimCount].mode = md; tempDimCount++;
                                }
                            }
                        } else if (strncmp(s.text, "{{EXT_REF", 9) == 0) {
                            s.type = 3; s.ptCount = 1;
                            if (tempShapeCount < MAX_SHAPES) {
                                tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                                if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                            }
                        } else {
                            s.type = 4; s.ptCount = 1;
                            if (tempShapeCount < MAX_SHAPES) {
                                tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                                if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                            }
                        }
                    }
                    inEntity = 0;
                }
            }
            
            if (strcmp(val, "LINE") == 0 || strcmp(val, "LWPOLYLINE") == 0 || strcmp(val, "POLYLINE") == 0 || strcmp(val, "TEXT") == 0) {
                strcpy(entType, val);
                inEntity = 1;
                memset(&s, 0, sizeof(Shape));
                s.useFill = 1; s.useStroke = 1; s.fill = RGB(128,128,128); s.stroke = RGB(0,0,0);
                s.strokeWidth = 1; s.fontSize = 24;
                if (strcmp(entType, "POLYLINE") == 0) s.type = 2; /* default open unless 70 triggers close */
            } else if (inEntity && strcmp(val, "VERTEX") == 0) {
                strcpy(entType, "VERTEX");
            } else if (inEntity && strcmp(val, "SEQEND") == 0) {
                strcpy(entType, "SEQEND");
            }
        } else if (inEntity) {
            if (strcmp(entType, "LINE") == 0) {
                if (code == 10) { s.ptsX[0] = atof(val) / scale; s.ptCount = CSV_MAX(s.ptCount, 1); }
                if (code == 20) s.ptsY[0] = atof(val) / scale;
                if (code == 11) { s.ptsX[1] = atof(val) / scale; s.ptCount = CSV_MAX(s.ptCount, 2); }
                if (code == 21) s.ptsY[1] = atof(val) / scale;
            } else if (strcmp(entType, "LWPOLYLINE") == 0) {
                if (code == 70) { s.type = atoi(val) == 1 ? 0 : 2; }
                if (code == 10 && s.ptCount < MAX_POINTS) { s.ptsX[s.ptCount] = atof(val) / scale; }
                if (code == 20 && s.ptCount < MAX_POINTS) { s.ptsY[s.ptCount] = atof(val) / scale; s.ptCount++; }
            } else if (strcmp(entType, "POLYLINE") == 0) {
                if (code == 70) { s.type = (atoi(val) & 1) ? 0 : 2; }
            } else if (strcmp(entType, "VERTEX") == 0) {
                if (code == 10 && s.ptCount < MAX_POINTS) { s.ptsX[s.ptCount] = atof(val) / scale; }
                if (code == 20 && s.ptCount < MAX_POINTS) { s.ptsY[s.ptCount] = atof(val) / scale; s.ptCount++; }
            } else if (strcmp(entType, "TEXT") == 0) {
                if (code == 10) { s.ptsX[0] = atof(val) / scale; s.ptCount = 1; }
                if (code == 20) { s.ptsY[0] = atof(val) / scale; }
                if (code == 40) { s.fontSize = (int)(atof(val) / scale); }
                if (code == 1) { strncpy(s.text, val, 127); s.text[127]='\0'; }
            }
        }
    }
    
    if (inEntity) {
        if (strcmp(entType, "LINE") == 0 && s.ptCount == 2) {
            s.type = 1; 
            if (tempShapeCount < MAX_SHAPES) {
                tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
            }
        } else if ((strcmp(entType, "POLYLINE") == 0 || strcmp(entType, "LWPOLYLINE") == 0 || strcmp(entType, "SEQEND") == 0) && s.ptCount >= 2) {
            if (tempShapeCount < MAX_SHAPES) {
                tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
            }
        } else if (strcmp(entType, "TEXT") == 0) {
            if (strncmp(s.text, "DIMENSION(", 10) == 0) {
                int s1, p1, s2, p2, md; double off, tp;
                if (sscanf(s.text, "DIMENSION(%d , %d , %d , %d , %lf , %lf , %d)", &s1, &p1, &s2, &p2, &off, &tp, &md) == 7) {
                    if (tempDimCount < MAX_DIMS) {
                        tempDims[tempDimCount].s1 = s1; tempDims[tempDimCount].p1 = p1;
                        tempDims[tempDimCount].s2 = s2; tempDims[tempDimCount].p2 = p2;
                        tempDims[tempDimCount].offset = off; tempDims[tempDimCount].textPos = tp;
                        tempDims[tempDimCount].mode = md; tempDimCount++;
                    }
                }
            } else if (strncmp(s.text, "{{EXT_REF", 9) == 0) {
                s.type = 3; s.ptCount = 1;
                if (tempShapeCount < MAX_SHAPES) {
                    tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                }
            } else {
                s.type = 4; s.ptCount = 1;
                if (tempShapeCount < MAX_SHAPES) {
                    tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                }
            }
        }
    }
    fclose(f);
    return tempShapeCount > 0 || tempDimCount > 0;
}

void ImportDXFtoC(const char* dxf_path, const char* out_c_path, double scale) {
    FILE* f; int i;
    if (!LoadDXF(dxf_path, scale)) { SetWindowText(hStatus, " Error: Failed to parse DXF."); return; }
    
    f = fopen(out_c_path, "w");
    if (!f) { SetWindowText(hStatus, " Error: Failed to create C file."); return; }
    
    fprintf(f, "case 1: {\n");
    fprintf(f, "    PAGE_DEF(\"Imported\", \"mm\", 1.0);\n");
    for (i = 0; i < tempShapeCount; i++) WriteShapeToC(f, tempShapes[i], i);
    for (i = 0; i < tempDimCount; i++) {
        Dimension* d = &tempDims[i];
        fprintf(f, "    DIMENSION(%d, %d, %d, %d, %g, %g, %d);\n", d->s1, d->p1, d->s2, d->p2, d->offset, d->textPos, d->mode);
    }
    fprintf(f, "    break;\n}\n");
    fclose(f);
    FreeTempShapes();
    SetWindowText(hStatus, " Success: DXF Imported to C!");
}

/* --- SVG Importer/Exporter --- */
void MatMul(double* A, double* B, double* out) {
    out[0] = A[0]*B[0] + A[2]*B[1]; out[1] = A[1]*B[0] + A[3]*B[1]; out[2] = A[0]*B[2] + A[2]*B[3]; out[3] = A[1]*B[2] + A[3]*B[3]; out[4] = A[0]*B[4] + A[2]*B[5] + A[4]; out[5] = A[1]*B[4] + A[3]*B[5] + A[5];
}
int GetNextSVGFloat(char** pp, double* val) {
    char* p = *pp;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) p++;
    if (!*p || *p == '"' || *p == '\'' || *p == ')' || *p == '<' || *p == '>') return 0;
    if (isalpha((unsigned char)*p) && *p != 'e' && *p != 'E') return 0; 
    *val = strtod(p, &p); *pp = p; return 1;
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
            GetNextSVGFloat(&p, &t[4]); if (!GetNextSVGFloat(&p, &t[5])) t[5] = 0;
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5; while(*p && *p != '(' && *p != limitChar) p++; if(*p=='(') p++;
            for(i=0; i<6; i++) t[i] = (i==0||i==3)?1:0;
            GetNextSVGFloat(&p, &t[0]); if (!GetNextSVGFloat(&p, &t[3])) t[3] = t[0];
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else p++;
    }
}
char* Attr(char* tagStr, char* tagEnd, const char* attrName) {
    char search[32]; char* a; int i; const char* quotes = "\"'";
    for (i = 0; i < 2; i++) {
        sprintf(search, " %s=%c", attrName, quotes[i]); a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "\n%s=%c", attrName, quotes[i]); a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "\t%s=%c", attrName, quotes[i]); a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
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

int LoadSVG(const char* path, double customScale) {
    FILE* f; long sz; char *d, *p, *tagEnd, *transStr, *dStr, *ptsStr; 
    int i, sp = 0; double matStack[10][6], cMat[6]; Shape s;
    double rx, ry, rw, rh, px, py;
    
    FreeTempShapes();
    f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > 60000L || sz <= 0) { fclose(f); return 0; }
    d = (char*)GlobalAllocPtr(GHND, (size_t)sz + 1);
    if (!d) { fclose(f); return 0; }
    fread(d, 1, (size_t)sz, f); d[sz] = '\0'; fclose(f);
    
    for(i=0;i<6;i++) matStack[0][i] = (i==0||i==3)?1:0;
    p = d;
    
    while ((p = strchr(p, '<')) != NULL && tempShapeCount < MAX_SHAPES) {
        p++; if (strncmp(p, "/g", 2) == 0) { if (sp > 0) sp--; continue; }
        tagEnd = strchr(p, '>'); if (!tagEnd) break;
        
        for(i=0;i<6;i++) cMat[i] = matStack[sp][i];
        transStr = Attr(p, tagEnd, "transform");
        if (transStr) ParseTransform(transStr, cMat, '"');
        
        if (p[0] == 'g' && (isspace(p[1]) || p[1] == '>')) {
            if (sp < 9) { sp++; for(i=0;i<6;i++) matStack[sp][i] = cMat[i]; }
        } else if (strncmp(p, "rect", 4) == 0) {
            rx = AttrF(p, tagEnd, "x", 0.0); ry = AttrF(p, tagEnd, "y", 0.0); rw = AttrF(p, tagEnd, "width", 0.0); rh = AttrF(p, tagEnd, "height", 0.0);
            if (rw > 0 && rh > 0) {
                memset(&s, 0, sizeof(Shape)); s.type = 0; s.ptCount = 4;
                GetSVGColors(p, tagEnd, &s.fill, &s.stroke, &s.useFill, &s.useStroke, 1);
                s.strokeWidth = (int)(AttrF(p, tagEnd, "stroke-width", 1.0) * 10.0);
                s.ptsX[0] = (cMat[0]*rx + cMat[2]*ry + cMat[4]); s.ptsY[0] = (cMat[1]*rx + cMat[3]*ry + cMat[5]);
                s.ptsX[1] = (cMat[0]*(rx+rw) + cMat[2]*ry + cMat[4]); s.ptsY[1] = (cMat[1]*(rx+rw) + cMat[3]*ry + cMat[5]);
                s.ptsX[2] = (cMat[0]*(rx+rw) + cMat[2]*(ry+rh) + cMat[4]); s.ptsY[2] = (cMat[1]*(rx+rw) + cMat[3]*(ry+rh) + cMat[5]);
                s.ptsX[3] = (cMat[0]*rx + cMat[2]*(ry+rh) + cMat[4]); s.ptsY[3] = (cMat[1]*rx + cMat[3]*(ry+rh) + cMat[5]);
                if (tempShapeCount < MAX_SHAPES) {
                    tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                }
            }
        } else if (strncmp(p, "poly", 4) == 0) {
            int isPolyline = (strncmp(p, "polyline", 8) == 0); ptsStr = Attr(p, tagEnd, "points");
            if (ptsStr) {
                memset(&s, 0, sizeof(Shape)); s.type = isPolyline ? 2 : 0;
                GetSVGColors(p, tagEnd, &s.fill, &s.stroke, &s.useFill, &s.useStroke, !isPolyline);
                s.strokeWidth = (int)(AttrF(p, tagEnd, "stroke-width", 1.0) * 10.0);
                while (s.ptCount < MAX_POINTS) {
                    if (!GetNextSVGFloat(&ptsStr, &px)) break;
                    if (!GetNextSVGFloat(&ptsStr, &py)) break;
                    s.ptsX[s.ptCount] = (cMat[0]*px + cMat[2]*py + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*px + cMat[3]*py + cMat[5]); s.ptCount++;
                }
                if (s.ptCount >= 2 && tempShapeCount < MAX_SHAPES) {
                    tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                }
            }
        } else if (strncmp(p, "path", 4) == 0) {
            dStr = Attr(p, tagEnd, "d");
            if (dStr) {
                memset(&s, 0, sizeof(Shape)); s.type = 0;
                GetSVGColors(p, tagEnd, &s.fill, &s.stroke, &s.useFill, &s.useStroke, 1);
                s.strokeWidth = (int)(AttrF(p, tagEnd, "stroke-width", 1.0) * 10.0);
                double curX = 0, curY = 0; char cmd = 'M';
                while (*dStr && *dStr != '"' && s.ptCount < MAX_POINTS) {
                    while (*dStr && isspace((unsigned char)*dStr)) dStr++;
                    if (isalpha((unsigned char)*dStr)) { cmd = *dStr; dStr++; }
                    if (toupper((unsigned char)cmd) == 'Z') break;
                    
                    double argX, argY;
                    if (toupper((unsigned char)cmd) == 'M' || toupper((unsigned char)cmd) == 'L') {
                        if (!GetNextSVGFloat(&dStr, &argX) || !GetNextSVGFloat(&dStr, &argY)) break;
                        if (cmd == 'm' || cmd == 'l') { argX += curX; argY += curY; }
                        curX = argX; curY = argY;
                        s.ptsX[s.ptCount] = (cMat[0]*curX + cMat[2]*curY + cMat[4]); s.ptsY[s.ptCount] = (cMat[1]*curX + cMat[3]*curY + cMat[5]); s.ptCount++;
                        if (toupper((unsigned char)cmd) == 'M') cmd = (cmd == 'M') ? 'L' : 'l';
                    } else { GetNextSVGFloat(&dStr, &argX); }
                }
                if (s.ptCount >= 2 && tempShapeCount < MAX_SHAPES) {
                    tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                }
            }
        } else if (strncmp(p, "text", 4) == 0) {
            px = AttrF(p, tagEnd, "x", 0.0);
            py = AttrF(p, tagEnd, "y", 0.0);
            double fSz = AttrF(p, tagEnd, "font-size", 2.4);
            char* tAnchor = Attr(p, tagEnd, "text-anchor");
            char* endTag = strstr(tagEnd, "</text>");
            if (endTag) {
                memset(&s, 0, sizeof(Shape));
                s.type = 4; s.ptCount = 1;
                s.ptsX[0] = (cMat[0]*px + cMat[2]*py + cMat[4]);
                s.ptsY[0] = (cMat[1]*px + cMat[3]*py + cMat[5]);
                
                s.ptsY[0] -= fSz * 0.8; 
                s.fontSize = (int)(fSz * 10.0);
                
                GetSVGColors(p, tagEnd, &s.fill, &s.stroke, &s.useFill, &s.useStroke, 1);
                
                if (tAnchor) {
                    if (strncmp(tAnchor, "middle", 6) == 0) s.useFill = 1;
                    else if (strncmp(tAnchor, "end", 3) == 0) s.useFill = 2;
                    else s.useFill = 0;
                } else s.useFill = 0;

                int txtLen = endTag - (tagEnd + 1);
                if (txtLen > 511) txtLen = 511;
                
                static char rawText[512];
                strncpy(rawText, tagEnd + 1, txtLen);
                rawText[txtLen] = '\0';
                
                char *r = rawText, *w = s.text;
                while (*r && (w - s.text) < 511) {
                    if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; }
                    else if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; }
                    else if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; }
                    else { *w++ = *r++; }
                }
                *w = '\0';
                
                if (tempShapeCount < MAX_SHAPES) {
                    tempShapes[tempShapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tempShapes[tempShapeCount]) { *tempShapes[tempShapeCount] = s; tempShapeCount++; }
                }
            }
        }
    }
    GlobalFreePtr(d);
    
    if (tempShapeCount > 0 && customScale != 1.0) {
        int si, pi;
        for(si = 0; si < tempShapeCount; si++) {
            if (tempShapes[si]) {
                for(pi = 0; pi < tempShapes[si]->ptCount; pi++) {
                    tempShapes[si]->ptsX[pi] *= customScale; tempShapes[si]->ptsY[pi] *= customScale;
                }
            }
        }
    }
    return tempShapeCount;
}

void CalcBoundingBox(Shape FAR* FAR* shapes, int count, Dimension* dims, int dimCount, double* outMinX, double* outMinY, double* outMaxX, double* outMaxY) {
    int i, p; double minX = 99999.0, minY = 99999.0, maxX = -99999.0, maxY = -99999.0;
    for (i=0; i<count; i++) {
        Shape FAR* sh = shapes[i];
        if (sh->type == 3) {
            char* refPath = (char*)GlobalAllocPtr(GHND, 512);
            char* absPath = (char*)GlobalAllocPtr(GHND, 512);
            
            if (refPath && absPath) {
                double rScale = 1.0, rRot = 0.0; char *pScale, *pRot, *pEnd; int pathLen;
                if (strncmp(sh->text, "{{EXT_REF=", 10) == 0) {
                    pScale = strstr(sh->text, " scale="); pRot = strstr(sh->text, " rot="); pEnd = strstr(sh->text, "}}");
                    if (pScale) pathLen = (int)(pScale - (sh->text + 10)); else if (pRot) pathLen = (int)(pRot - (sh->text + 10)); else if (pEnd) pathLen = (int)(pEnd - (sh->text + 10)); else pathLen = strlen(sh->text + 10);
                    if (pathLen > 0 && pathLen < 512) {
                        strncpy(refPath, sh->text + 10, pathLen); refPath[pathLen] = '\0';
                        while (pathLen > 0 && isspace((unsigned char)refPath[pathLen - 1])) refPath[--pathLen] = '\0';
                        if (pScale) sscanf(pScale, " scale=%lf", &rScale);
                        if (pRot) sscanf(pRot, " rot=%lf", &rRot);
                        
                        ResolvePath(loadedCFile, refPath, absPath);
                        if (!loadedCFile[0] || stricmp(absPath, loadedCFile) != 0) {
                            int rIdx = EnsureRefLoaded(absPath);
                            if (rIdx != -1) {
                                double lcx = (refCache[rIdx]->minX + refCache[rIdx]->maxX) / 2.0;
                                double lcy = (refCache[rIdx]->minY + refCache[rIdx]->maxY) / 2.0;
                                double objCx = sh->ptsX[0] + lcx * rScale, objCy = sh->ptsY[0] + lcy * rScale;
                                double rRad = rRot * PI / 180.0, cosR = cos(rRad), sinR = sin(rRad);
                                double bx[4], by[4]; int r;
                                
                                bx[0] = refCache[rIdx]->minX; by[0] = refCache[rIdx]->minY; bx[1] = refCache[rIdx]->maxX; by[1] = refCache[rIdx]->minY;
                                bx[2] = refCache[rIdx]->maxX; by[2] = refCache[rIdx]->maxY; bx[3] = refCache[rIdx]->minX; by[3] = refCache[rIdx]->maxY;
                                for (r = 0; r < 4; r++) {
                                    double dx_r = (bx[r] - lcx) * rScale, dy_r = (by[r] - lcy) * rScale;
                                    double px_t = objCx + (dx_r * cosR - dy_r * sinR);
                                    double py_t = objCy + (dx_r * sinR + dy_r * cosR);
                                    minX = fmin(minX, px_t); maxX = fmax(maxX, px_t); minY = fmin(minY, py_t); maxY = fmax(maxY, py_t);
                                }
                            }
                        }
                    }
                }
            }
            if (refPath) GlobalFreePtr(refPath);
            if (absPath) GlobalFreePtr(absPath);
        } else if (sh->type == 4) {
            minX = fmin(minX, sh->ptsX[0]); maxX = fmax(maxX, sh->ptsX[0]); minY = fmin(minY, sh->ptsY[0]); maxY = fmax(maxY, sh->ptsY[0]);
        } else {
            for (p=0; p<sh->ptCount; p++) {
                minX = fmin(minX, sh->ptsX[p]); maxX = fmax(maxX, sh->ptsX[p]); minY = fmin(minY, sh->ptsY[p]); maxY = fmax(maxY, sh->ptsY[p]);
            }
        }
    }
    
    for (i=0; i<dimCount; i++) {
        Dimension* d = &dims[i];
        if (d->s1 >= 0 && d->s1 < count && d->s2 >= 0 && d->s2 < count) {
            Shape FAR* s1 = shapes[d->s1]; Shape FAR* s2 = shapes[d->s2];
            if (d->p1 >= 0 && d->p1 < s1->ptCount && d->p2 >= 0 && d->p2 < s2->ptCount) {
                double px1 = s1->ptsX[d->p1], py1 = s1->ptsY[d->p1];
                double px2 = s2->ptsX[d->p2], py2 = s2->ptsY[d->p2];
                double dx = px2 - px1, dy = py2 - py1, len, nx, ny, dim_x1, dim_y1, dim_x2, dim_y2;
                
                if (d->mode == 1) dy = 0.0; if (d->mode == 2) dx = 0.0;
                len = sqrt(dx*dx + dy*dy);
                if (len > 0) {
                    nx = -dy / len; ny = dx / len;
                    if (d->mode == 1) { dim_x1 = px1; dim_x2 = px2; dim_y1 = py1 - d->offset; dim_y2 = py2 - d->offset; }
                    else if (d->mode == 2) { dim_x1 = px1 + d->offset; dim_x2 = px2 + d->offset; dim_y1 = py1; dim_y2 = py2; }
                    else { dim_x1 = px1 + nx * d->offset; dim_y1 = py1 + ny * d->offset; dim_x2 = px2 + nx * d->offset; dim_y2 = py2 + ny * d->offset; }
                    minX = fmin(minX, fmin(dim_x1, dim_x2)); maxX = fmax(maxX, fmax(dim_x1, dim_x2));
                    minY = fmin(minY, fmin(dim_y1, dim_y2)); maxY = fmax(maxY, fmax(dim_y1, dim_y2));
                }
            }
        }
    }
    
    if (minX > maxX) { minX = 0; maxX = 32; minY = 0; maxY = 32; }
    if (maxX - minX < 1.0) maxX = minX + 32.0;
    if (maxY - minY < 1.0) maxY = minY + 32.0;
    *outMinX = minX; *outMinY = minY; *outMaxX = maxX; *outMaxY = maxY;
}

void DrawSVGDimensions(FILE* f, Dimension* dims, int dimCount, Shape FAR* FAR* shapes, int shapeCount, double pageScale, double offX, double offY, double minX, double minY, const char* unitName, double rScale, double cosR, double sinR, double objCx, double objCy, double lcx, double lcy) {
    int i;
    for (i = 0; i < dimCount; i++) {
        double lA1x, lA1y, lA2x, lA2y, lD1x, lD1y, lD2x, lD2y;
        double gA1x, gA1y, gA2x, gA2y, gD1x, gD1y, gD2x, gD2y, gMidX, gMidY;
        double svgA1x, svgA1y, svgA2x, svgA2y, svgD1x, svgD1y, svgD2x, svgD2y, svgMidX, svgMidY;
        double lDx, lDy, ang, nx, ny, val, lGridOffset, lMidX, lMidY;
        double tx_, ty_;
        char buf[64];

        if (dims[i].s1 < 0 || dims[i].s1 >= shapeCount || dims[i].s2 < 0 || dims[i].s2 >= shapeCount) continue;
        if (shapes[dims[i].s1]->type != 3 && dims[i].p1 >= shapes[dims[i].s1]->ptCount) continue;
        if (shapes[dims[i].s2]->type != 3 && dims[i].p2 >= shapes[dims[i].s2]->ptCount) continue;

        lA1x = shapes[dims[i].s1]->ptsX[dims[i].p1];
        lA1y = shapes[dims[i].s1]->ptsY[dims[i].p1];
        lA2x = shapes[dims[i].s2]->ptsX[dims[i].p2];
        lA2y = shapes[dims[i].s2]->ptsY[dims[i].p2];

        lDx = lA2x - lA1x; lDy = lA2y - lA1y;
        lGridOffset = dims[i].offset;

        if (dims[i].mode == 0) {
            if (lDx == 0.0 && lDy == 0.0) ang = 0.0; else ang = atan2(lDy, lDx);
            nx = -sin(ang); ny = cos(ang);
            lD1x = lA1x + nx * lGridOffset; lD1y = lA1y + ny * lGridOffset;
            lD2x = lA2x + nx * lGridOffset; lD2y = lA2y + ny * lGridOffset;
            val = sqrt(pow((lA2x - lA1x)*rScale, 2) + pow((lA2y - lA1y)*rScale, 2));
        } else if (dims[i].mode == 1) {
            lD1x = lA1x; lD1y = lA1y - lGridOffset;
            lD2x = lA2x; lD2y = lA1y - lGridOffset;
            val = fabs((lA2x - lA1x)*rScale);
        } else {
            lD1x = lA1x + lGridOffset; lD1y = lA1y;
            lD2x = lA1x + lGridOffset; lD2y = lA2y;
            val = fabs((lA2y - lA1y)*rScale);
        }

        lMidX = lD1x + (lD2x - lD1x) * dims[i].textPos;
        lMidY = lD1y + (lD2y - lD1y) * dims[i].textPos;

        tx_ = (lA1x - lcx) * rScale; ty_ = (lA1y - lcy) * rScale;
        gA1x = objCx + tx_ * cosR - ty_ * sinR; gA1y = objCy + tx_ * sinR + ty_ * cosR;

        tx_ = (lA2x - lcx) * rScale; ty_ = (lA2y - lcy) * rScale;
        gA2x = objCx + tx_ * cosR - ty_ * sinR; gA2y = objCy + tx_ * sinR + ty_ * cosR;

        tx_ = (lD1x - lcx) * rScale; ty_ = (lD1y - lcy) * rScale;
        gD1x = objCx + tx_ * cosR - ty_ * sinR; gD1y = objCy + tx_ * sinR + ty_ * cosR;

        tx_ = (lD2x - lcx) * rScale; ty_ = (lD2y - lcy) * rScale;
        gD2x = objCx + tx_ * cosR - ty_ * sinR; gD2y = objCy + tx_ * sinR + ty_ * cosR;

        tx_ = (lMidX - lcx) * rScale; ty_ = (lMidY - lcy) * rScale;
        gMidX = objCx + tx_ * cosR - ty_ * sinR; gMidY = objCy + tx_ * sinR + ty_ * cosR;

        svgA1x = offX + (gA1x - minX) * pageScale; svgA1y = offY + (gA1y - minY) * pageScale;
        svgA2x = offX + (gA2x - minX) * pageScale; svgA2y = offY + (gA2y - minY) * pageScale;
        svgD1x = offX + (gD1x - minX) * pageScale; svgD1y = offY + (gD1y - minY) * pageScale;
        svgD2x = offX + (gD2x - minX) * pageScale; svgD2y = offY + (gD2y - minY) * pageScale;
        svgMidX = offX + (gMidX - minX) * pageScale; svgMidY = offY + (gMidY - minY) * pageScale;

        FormatDimension(val, unitName, buf);

        double L1dx = svgD1x - svgA1x, L1dy = svgD1y - svgA1y;
        double L1len = sqrt(L1dx*L1dx + L1dy*L1dy);
        if (L1len > 5.0) {
            fprintf(f, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"#0080FF\" stroke-width=\"1.0\" />\n",
                svgA1x + L1dx/L1len*5.0, svgA1y + L1dy/L1len*5.0,
                svgD1x - L1dx/L1len*2.0, svgD1y - L1dy/L1len*2.0);
        }

        double L2dx = svgD2x - svgA2x, L2dy = svgD2y - svgA2y;
        double L2len = sqrt(L2dx*L2dx + L2dy*L2dy);
        if (L2len > 5.0) {
            fprintf(f, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"#0080FF\" stroke-width=\"1.0\" />\n",
                svgA2x + L2dx/L2len*5.0, svgA2y + L2dy/L2len*5.0,
                svgD2x - L2dx/L2len*2.0, svgD2y - L2dy/L2len*2.0);
        }

        fprintf(f, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"#0080FF\" stroke-width=\"1.0\" />\n", svgD1x, svgD1y, svgD2x, svgD2y);

        double dimDx = svgD2x - svgD1x, dimDy = svgD2y - svgD1y;
        double dimLen = sqrt(dimDx*dimDx + dimDy*dimDy);
        if (dimLen > 0) {
            double dirX = dimDx/dimLen, dirY = dimDy/dimLen;
            fprintf(f, "<polygon points=\"%.2f,%.2f %.2f,%.2f %.2f,%.2f\" fill=\"#0080FF\" />\n",
                svgD1x, svgD1y,
                svgD1x + dirX*16.0 - dirY*5.0, svgD1y + dirY*16.0 + dirX*5.0,
                svgD1x + dirX*16.0 + dirY*5.0, svgD1y + dirY*16.0 - dirX*5.0);
            fprintf(f, "<polygon points=\"%.2f,%.2f %.2f,%.2f %.2f,%.2f\" fill=\"#0080FF\" />\n",
                svgD2x, svgD2y,
                svgD2x - dirX*16.0 - dirY*5.0, svgD2y - dirY*16.0 + dirX*5.0,
                svgD2x - dirX*16.0 + dirY*5.0, svgD2y - dirY*16.0 - dirX*5.0);
        }

        double svgFSize = 20.0;
        double text_w = strlen(buf) * svgFSize * 0.55;

        fprintf(f, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"#FFFFFF\" />\n",
            svgMidX - text_w/2.0 - 2.0, svgMidY - svgFSize/2.0 - 2.0, text_w + 4.0, svgFSize + 4.0);
        fprintf(f, "<text x=\"%.2f\" y=\"%.2f\" font-family=\"Arial\" font-size=\"%.2f\" fill=\"#0080FF\" text-anchor=\"middle\">%s</text>\n",
            svgMidX, svgMidY + svgFSize/3.0, svgFSize, buf);
    }
}

void DrawSVGShape(FILE* f, Shape FAR* sh, double scale, double minX, double minY, double iconPageScale) {
    int p;
    static char refPath_stack[4][512];
    static char absPath_stack[4][512];
    static char subPath_stack[4][512];
    static char tagBuf_stack[4][512];
    static char dispT_stack[4][512];
    static char escT_stack[4][1024];

    if (sh->type == 3) {
        char* refPath = refPath_stack[g_RenderRefDepth];
        char* absPath = absPath_stack[g_RenderRefDepth];

        double rScale = 1.0, rRot = 0.0; char *pScale, *pRot, *pEnd; int pathLen;
        if (strncmp(sh->text, "{{EXT_REF=", 10) != 0) return;

        pScale = strstr(sh->text, " scale="); pRot = strstr(sh->text, " rot="); pEnd = strstr(sh->text, "}}");
        if (pScale) pathLen = (int)(pScale - (sh->text + 10)); else if (pRot) pathLen = (int)(pRot - (sh->text + 10)); else if (pEnd) pathLen = (int)(pEnd - (sh->text + 10)); else pathLen = strlen(sh->text + 10);
        if (pathLen <= 0 || pathLen >= 512) return;

        strncpy(refPath, sh->text + 10, pathLen); refPath[pathLen] = '\0';
        while (pathLen > 0 && isspace((unsigned char)refPath[pathLen - 1])) refPath[--pathLen] = '\0';
        if (pScale) sscanf(pScale, " scale=%lf", &rScale); if (pRot) sscanf(pRot, " rot=%lf", &rRot);
        
        ResolvePath(loadedCFile, refPath, absPath);
        if (loadedCFile[0] && stricmp(absPath, loadedCFile) == 0) return; 

        int rIdx = EnsureRefLoaded(absPath);
        if (rIdx != -1) {
            double lcx = (refCache[rIdx]->minX + refCache[rIdx]->maxX) / 2.0;
            double lcy = (refCache[rIdx]->minY + refCache[rIdx]->maxY) / 2.0;
            double objCx = sh->ptsX[0] + lcx * rScale;
            double objCy = sh->ptsY[0] + lcy * rScale;
            double rRad = rRot * PI / 180.0, cosR = cos(rRad), sinR = sin(rRad);
            int refTagIdx = 0, r;
            
            for (r = 0; r < refCache[rIdx]->shapeCount; r++) {
                Shape FAR* sub = refCache[rIdx]->shapes[r];
                if (sub->type == 3) {
                    if (g_RenderRefDepth < 3 && strncmp(sub->text, "{{EXT_REF=", 10) == 0) {
                        Shape FAR* tRef = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                        char* subPath = subPath_stack[g_RenderRefDepth];

                        if (tRef) {
                            double subSc = 1.0, subRot = 0.0;
                            char *spS = strstr(sub->text, " scale="), *spR = strstr(sub->text, " rot="), *spE = strstr(sub->text, "}}");
                            int sLen;
                            
                            *tRef = *sub;
                            if (spS) sLen = (int)(spS - (sub->text + 10)); else if (spE) sLen = (int)(spE - (sub->text + 10)); else sLen = strlen(sub->text + 10);
                            if (sLen > 0 && sLen < 512) {
                                strncpy(subPath, sub->text + 10, sLen); subPath[sLen] = '\0';
                                while (sLen > 0 && isspace((unsigned char)subPath[sLen - 1])) subPath[--sLen] = '\0';
                                if (spS) sscanf(spS, " scale=%lf", &subSc);
                                if (spR) sscanf(spR, " rot=%lf", &subRot);
                                
                                double dx = (sub->ptsX[0] - lcx) * rScale, dy = (sub->ptsY[0] - lcy) * rScale;
                                tRef->ptsX[0] = objCx + dx * cosR - dy * sinR; tRef->ptsY[0] = objCy + dx * sinR + dy * cosR;
                                sprintf(tRef->text, "{{EXT_REF=%.450s scale=%.2f rot=%.2f}}", subPath, subSc * rScale, subRot + rRot);
                                
                                g_RenderRefDepth++;
                                DrawSVGShape(f, tRef, scale, minX, minY, iconPageScale);
                                g_RenderRefDepth--;
                            }
                        }
                        if (tRef) GlobalFreePtr(tRef);
                    }
                    continue;
                }
                
                if (sub->type == 4) {
                    Shape FAR* tShp = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    char* tagBuf = tagBuf_stack[g_RenderRefDepth];
                    
                    if (tShp) {
                        double dx = (sub->ptsX[0] - lcx) * rScale, dy = (sub->ptsY[0] - lcy) * rScale;
                        GetPipeValue(sh->tagData, refTagIdx, tagBuf, 512);
                        
                        if (strlen(tagBuf) == 0) {
                            strncpy(tagBuf, sub->text, 511); tagBuf[511] = '\0'; 
                        }
                        
                        if (tagBuf[0] == '{' && tagBuf[1] == '{') {
                            char* pEndClean = strstr(tagBuf + 2, "}}");
                            if (pEndClean) *pEndClean = '\0'; 
                            
                            int stIdx = 0;
                            while (tagBuf[stIdx + 2] != '\0') {
                                tagBuf[stIdx] = tagBuf[stIdx + 2];
                                stIdx++;
                            }
                            tagBuf[stIdx] = '\0';
                        }
                        
                        if (strcmp(tagBuf, "SHEETSCALE") == 0) {
                            if (iconPageScale > 0 && iconPageScale <= 1.0) sprintf(tagBuf, "1:%g", 1.0 / iconPageScale);
                            else sprintf(tagBuf, "%g", iconPageScale);
                        }

                        *tShp = *sub; tShp->ptsX[0] = objCx + dx * cosR - dy * sinR; tShp->ptsY[0] = objCy + dx * sinR + dy * cosR;
                        strcpy(tShp->text, tagBuf); tShp->fontSize = sub->fontSize > 0 ? (int)(sub->fontSize * rScale) : (int)(24 * rScale);
                        tShp->strokeWidth = sub->strokeWidth > 0 ? (int)(sub->strokeWidth * rScale) : (int)(1 * rScale);
                        DrawSVGShape(f, tShp, scale, minX, minY, iconPageScale);
                    }
                    if (tShp) GlobalFreePtr(tShp);
                    refTagIdx++;
                } else {
                    Shape FAR* tmpShp = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tmpShp) {
                        int pIdx;
                        *tmpShp = *sub;
                        for (pIdx = 0; pIdx < sub->ptCount; pIdx++) {
                            double dx = (sub->ptsX[pIdx] - lcx) * rScale, dy = (sub->ptsY[pIdx] - lcy) * rScale;
                            tmpShp->ptsX[pIdx] = objCx + dx * cosR - dy * sinR; tmpShp->ptsY[pIdx] = objCy + dx * sinR + dy * cosR;
                        }
                        tmpShp->strokeWidth = sub->strokeWidth > 0 ? (int)(sub->strokeWidth * rScale) : (int)(1 * rScale);
                        DrawSVGShape(f, tmpShp, scale, minX, minY, iconPageScale);
                        GlobalFreePtr(tmpShp);
                    }
                }
            }
            if (refCache[rIdx]->dimCount > 0) {
                DrawSVGDimensions(f, refCache[rIdx]->dims, refCache[rIdx]->dimCount, refCache[rIdx]->shapes, refCache[rIdx]->shapeCount, scale, 0.0, 0.0, minX, minY, refCache[rIdx]->unitName, rScale, cosR, sinR, objCx, objCy, lcx, lcy);
            }
        }
        return;
    }
    
    if (sh->type == 4) {
        char* dispT = dispT_stack[0];
        char* escT = escT_stack[0];
        char *src, *dst; 
        double tx, ty, svgFSize;
        
        if (sh->text[0] == '{' && sh->text[1] == '{') {
            strcpy(dispT, sh->text + 2); char *pEnd = strstr(dispT, "}}"); if (pEnd) *pEnd = '\0';
        } else { strcpy(dispT, sh->text); }

        src = dispT; dst = escT;
        while (*src && (dst - escT) < 1000) {
            if (*src == '<') { strcpy(dst, "&lt;"); dst += 4; }
            else if (*src == '>') { strcpy(dst, "&gt;"); dst += 4; }
            else if (*src == '&') { strcpy(dst, "&amp;"); dst += 5; }
            else { *dst++ = *src; }
            src++;
        }
        *dst = '\0';
        
        if (strcmp(escT, "SHEETSCALE") == 0) {
            if (iconPageScale > 0 && iconPageScale <= 1.0) sprintf(escT, "1:%g", 1.0 / iconPageScale);
            else sprintf(escT, "%g", iconPageScale);
        }

        tx = (sh->ptsX[0] - minX) * scale; ty = (sh->ptsY[0] - minY) * scale;
        
        svgFSize = ((double)(sh->fontSize > 0 ? sh->fontSize : 24) * scale) / 10.0;
        if (svgFSize < 2.0) svgFSize = 2.0;
        
        fprintf(f, "<text x=\"%.2f\" y=\"%.2f\" font-family=\"Arial\" font-size=\"%.2f\"", tx, ty + (svgFSize * 0.8), svgFSize);
        if (sh->useFill == 1) fprintf(f, " text-anchor=\"middle\"");
        else if (sh->useFill == 2) fprintf(f, " text-anchor=\"end\"");
        fprintf(f, " fill=\"#%02X%02X%02X\"", (int)(sh->stroke & 0xFF), (int)((sh->stroke >> 8) & 0xFF), (int)((sh->stroke >> 16) & 0xFF));
        fprintf(f, ">%s</text>\n", escT);
        return;
    }

    if (sh->type == 0) fprintf(f, "<polygon points=\"");
    else fprintf(f, "<polyline points=\"");

    for (p = 0; p < sh->ptCount; p++) {
        fprintf(f, "%.2f,%.2f ", (sh->ptsX[p] - minX) * scale, (sh->ptsY[p] - minY) * scale);
    }
    fprintf(f, "\"");
    
    if (sh->useFill && sh->type == 0) {
        fprintf(f, " fill=\"#%02X%02X%02X\"", (int)(sh->fill & 0xFF), (int)((sh->fill >> 8) & 0xFF), (int)((sh->fill >> 16) & 0xFF));
    } else {
        fprintf(f, " fill=\"none\"");
    }
    
    if (sh->useStroke) {
        fprintf(f, " stroke=\"#%02X%02X%02X\"", (int)(sh->stroke & 0xFF), (int)((sh->stroke >> 8) & 0xFF), (int)((sh->stroke >> 16) & 0xFF));
    } else {
        fprintf(f, " stroke=\"none\"");
    }
    
    fprintf(f, " stroke-width=\"%.2f\" />\n", ((sh->strokeWidth > 0 ? sh->strokeWidth : 1.0) * scale) / 10.0);
}

void ExportCtoSVG(const char* c_path, const char* save_path, double scale) {
    char drive[3], dir[260], fname[260], ext[16]; int i, j;
    if (!LoadCFile(c_path)) { SetWindowText(hStatus, " Error: Failed to load C file."); return; }
    if (parsedCount <= 0) { SetWindowText(hStatus, " Error: No icons found in C file."); return; }
    
    _splitpath(save_path, drive, dir, fname, ext);
    if (!ext[0]) strcpy(ext, ".svg");
    
    for (i=0; i<parsedCount; i++) {
        char final_path[MAX_PATH];
        double minX, minY, maxX, maxY; FILE* f;
        
        if (parsedCount == 1) {
            strcpy(final_path, save_path);
        } else {
            char numStr[16]; char newFname[260]; int maxNameLen;
            sprintf(numStr, "%d", i+1);
            maxNameLen = 8 - strlen(numStr);
            if (maxNameLen < 1) maxNameLen = 1;
            strncpy(newFname, fname, maxNameLen); newFname[maxNameLen] = '\0';
            strcat(newFname, numStr);
            _makepath(final_path, drive, dir, newFname, ext);
        }
        
        f = fopen(final_path, "w");
        if (!f) continue;
        
        CalcBoundingBox(parsedIcons[i]->shapes, parsedIcons[i]->shapeCount, parsedIcons[i]->dims, parsedIcons[i]->dimCount, &minX, &minY, &maxX, &maxY);
        
        fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.2f\" height=\"%.2f\" viewBox=\"0 0 %.2f %.2f\">\n",
            (maxX - minX)*scale, (maxY - minY)*scale, (maxX - minX)*scale, (maxY - minY)*scale);
        
        for (j=0; j<parsedIcons[i]->shapeCount; j++) {
            DrawSVGShape(f, parsedIcons[i]->shapes[j], scale, minX, minY, parsedIcons[i]->pageScale);
        }
        
        if (parsedIcons[i]->dimCount > 0) {
            DrawSVGDimensions(f, parsedIcons[i]->dims, parsedIcons[i]->dimCount, parsedIcons[i]->shapes, parsedIcons[i]->shapeCount, scale, 0.0, 0.0, minX, minY, parsedIcons[i]->unitName, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        }
        
        fprintf(f, "</svg>\n");
        fclose(f);
    }
    SetWindowText(hStatus, " Success: SVG Exported!");
}

void ImportSVGtoC(const char* svg_path, const char* out_c_path, double scale) {
    FILE* f; int i;
    if (!LoadSVG(svg_path, scale)) { SetWindowText(hStatus, " Error: Failed to parse SVG."); return; }
    if (tempShapeCount == 0) { SetWindowText(hStatus, " Error: No valid shapes found in SVG."); return; }
    
    f = fopen(out_c_path, "w");
    if (!f) { SetWindowText(hStatus, " Error: Failed to create C file."); return; }
    
    fprintf(f, "case 1: {\n");
    fprintf(f, "    PAGE_DEF(\"Imported\", \"mm\", 1.0);\n");
    for (i = 0; i < tempShapeCount; i++) {
        WriteShapeToC(f, tempShapes[i], i);
    }
    fprintf(f, "    break;\n}\n");
    fclose(f);
    FreeTempShapes();
    SetWindowText(hStatus, " Success: SVG Imported to C!");
}

/* --- GUI Actions --- */
void ImportAction(HWND hwnd, int isDXF) {
    char in_path[MAX_PATH] = ""; char out_path[MAX_PATH] = ""; 
    char scaleStr[32]; double scale;
    
    GetWindowText(hTxtFile, in_path, MAX_PATH);
    if (strlen(in_path) == 0) {
        OPENFILENAME ofn;
        memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = isDXF ? "DXF Files (*.dxf)\0*.dxf\0All Files (*.*)\0*.*\0" : "SVG Files (*.svg)\0*.svg\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = in_path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST;
        if (!GetOpenFileName(&ofn)) return;
        SetWindowText(hTxtFile, in_path);
    }

    GetWindowText(hTxtOutFile, out_path, MAX_PATH);
    if (strlen(out_path) == 0) {
        strcpy(out_path, in_path);
        char* ext = strrchr(out_path, '.');
        if (ext) strcpy(ext, ".c"); else strcat(out_path, ".c");
        
        OPENFILENAME ofn;
        memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = "C Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = out_path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileName(&ofn)) return;
    } else {
        if (strchr(out_path, '\\') || strchr(out_path, '/') || strchr(out_path, ':')) {
            // Full path - keep as is
        } else {
            char local_dir[MAX_PATH];
            _getcwd(local_dir, MAX_PATH);
            strcat(local_dir, "\\");
            strcat(local_dir, out_path);
            strcpy(out_path, local_dir);
        }
    }
    
    GetWindowText(hTxtScale, scaleStr, 32); scale = atof(scaleStr); if (scale <= 0) scale = 1.0;
    
    if (isDXF) ImportDXFtoC(in_path, out_path, scale);
    else ImportSVGtoC(in_path, out_path, scale);
}

void ExportAction(HWND hwnd, int isDXF) {
    char in_path[MAX_PATH] = ""; char out_path[MAX_PATH] = ""; 
    char scaleStr[32]; double scale;
    
    GetWindowText(hTxtFile, in_path, MAX_PATH);
    if (strlen(in_path) == 0) {
        OPENFILENAME ofn;
        memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = "C Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = in_path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST;
        if (!GetOpenFileName(&ofn)) return;
        SetWindowText(hTxtFile, in_path);
    }

    GetWindowText(hTxtOutFile, out_path, MAX_PATH);
    if (strlen(out_path) == 0) {
        strcpy(out_path, in_path);
        char* ext = strrchr(out_path, '.');
        if (ext) strcpy(ext, isDXF ? ".dxf" : ".svg"); else strcat(out_path, isDXF ? ".dxf" : ".svg");
        
        OPENFILENAME ofn;
        memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = isDXF ? "DXF Files (*.dxf)\0*.dxf\0All Files (*.*)\0*.*\0" : "SVG Files (*.svg)\0*.svg\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = out_path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileName(&ofn)) return;
    } else {
        if (strchr(out_path, '\\') || strchr(out_path, '/') || strchr(out_path, ':')) {
            // Full path - keep as is
        } else {
            char local_dir[MAX_PATH];
            _getcwd(local_dir, MAX_PATH);
            strcat(local_dir, "\\");
            strcat(local_dir, out_path);
            strcpy(out_path, local_dir);
        }
    }
    
    GetWindowText(hTxtScale, scaleStr, 32); scale = atof(scaleStr); if (scale <= 0) scale = 1.0;
    
    if (isDXF) ExportCtoDXF(in_path, out_path, scale);
    else ExportCtoSVG(in_path, out_path, scale);
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "Input File:", WS_VISIBLE | WS_CHILD, 10, 15, 70, 20, hwnd, NULL, NULL, NULL);
            hTxtFile = CreateWindow("EDIT", "icons.c", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 80, 15, 190, 22, hwnd, NULL, NULL, NULL);
            hBtnBrowse = CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD, 280, 15, 80, 22, hwnd, (HMENU)1, NULL, NULL);

            CreateWindow("STATIC", "Output File:", WS_VISIBLE | WS_CHILD, 10, 45, 70, 20, hwnd, NULL, NULL, NULL);
            hTxtOutFile = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 80, 45, 190, 22, hwnd, NULL, NULL, NULL);

            CreateWindow("STATIC", "Scale:", WS_VISIBLE | WS_CHILD, 10, 75, 60, 20, hwnd, NULL, NULL, NULL);
            hTxtScale = CreateWindow("EDIT", "1.0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 80, 75, 60, 22, hwnd, NULL, NULL, NULL);

            hBtnExportSVG = CreateWindow("BUTTON", "Export C -> SVG", WS_VISIBLE | WS_CHILD, 10, 115, 130, 30, hwnd, (HMENU)3, NULL, NULL);
            hBtnImportSVG = CreateWindow("BUTTON", "Import SVG -> C", WS_VISIBLE | WS_CHILD, 150, 115, 130, 30, hwnd, (HMENU)2, NULL, NULL);
            
            hBtnExportDXF = CreateWindow("BUTTON", "Export C -> DXF", WS_VISIBLE | WS_CHILD, 10, 155, 130, 30, hwnd, (HMENU)5, NULL, NULL);
            hBtnImportDXF = CreateWindow("BUTTON", "Import DXF -> C", WS_VISIBLE | WS_CHILD, 150, 155, 130, 30, hwnd, (HMENU)6, NULL, NULL);
            
            hBtnExit = CreateWindow("BUTTON", "Exit", WS_VISIBLE | WS_CHILD, 290, 135, 70, 30, hwnd, (HMENU)4, NULL, NULL);
            hStatus = CreateWindow("STATIC", " Ready.", WS_VISIBLE | WS_CHILD | WS_BORDER | SS_LEFT, 0, 195, 390, 20, hwnd, NULL, NULL, NULL);

            DragAcceptFiles(hwnd, TRUE);
            break;
        }
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char filePath[MAX_PATH];
            if (DragQueryFile(hDrop, 0, filePath, MAX_PATH)) {
                SetWindowText(hTxtFile, filePath);
                SetWindowText(hStatus, " File loaded via Drag & Drop.");
            }
            DragFinish(hDrop);
            break;
        }
        case WM_COMMAND: {
            int id = wParam;
            if (id == 1) { 
                OPENFILENAME ofn; char path[MAX_PATH] = "";
                memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "C Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileName(&ofn)) {
                    SetWindowText(hTxtFile, path);
                    SetWindowText(hStatus, " Ready to convert.");
                }
            } else if (id == 2) { 
                ImportAction(hwnd, 0);
            } else if (id == 3) { 
                ExportAction(hwnd, 0);
            } else if (id == 5) {
                ExportAction(hwnd, 1);
            } else if (id == 6) {
                ImportAction(hwnd, 1);
            } else if (id == 4) { PostQuitMessage(0); }
            break;
        }
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Entry Point ---
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc; MSG msg;
    
    memset(parsedIcons, 0, sizeof(parsedIcons));
    memset(refCache, 0, sizeof(refCache));
    memset(tempShapes, 0, sizeof(tempShapes));

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "IMEX_C_CLASS";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow("IMEX_C_CLASS", "IMEX-C SVG/DXF Converter", 
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 390, 260, NULL, NULL, hInstance, NULL);

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    while(GetMessage(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    FreeAllData();
    FreeTempShapes();
    return msg.wParam;
}
/* EOF */