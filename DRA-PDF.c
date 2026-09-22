/* ============================================================================
 * DRA-PDF C-Icon to PDF Generator - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL:
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s dra-pdf.c
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <direct.h>
#include <stdarg.h>

#pragma library("commdlg.lib")
#pragma library("shell.lib")

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

/* --- Data Structures --- */
typedef struct {
    int s1, p1, s2, p2; 
    double offset; 
    double textPos; 
    int mode; 
} Dimension;

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
    int caseId; 
    char name[64]; 
    Shape FAR* shapes[MAX_SHAPES]; 
    int shapeCount; 
    Dimension dims[MAX_DIMS];
    int dimCount;
    char pageName[64];
    char unitName[32];
    double pageScale;
    int gridW;
    int gridH;
} IconDef;

typedef struct {
    char path[260];
    Shape FAR* shapes[MAX_SHAPES];
    int shapeCount;
    double minX, minY, maxX, maxY;
    Dimension dims[MAX_DIMS];
    int dimCount;
    char unitName[32];
} RefCache;

IconDef FAR* parsedIcons[MAX_ICONS] = {0};
int parsedCount = 0;
RefCache FAR* refCache[MAX_REFS] = {0};
int refCacheCount = 0;
char loadedCFile[260] = "";
int g_RenderRefDepth = 0;

/* --- GUI Globals --- */
HWND hMain, hTxtCFile, hTxtOutFile, hBtnBrowse;
HWND hTxtWidth, hTxtHeight, hBtnToggle, hTxtMargH, hTxtMargV;
HWND hBtnCreate, hBtnExit, hStatus;

int isLandscape = 1;
float page_w_mm = 279.4f; 
float page_h_mm = 215.9f;

/* --- Forward Declarations --- */
void FormatDimension(double val, const char* unit, char* outBuf);
void pdf_out(FILE* f, long* stream_len, const char* fmt, ...);
void pdf_color(FILE* f, long* stream_len, int is_stroke, long hex_color);
void pdf_text_color(FILE* f, long* stream_len, long hex_color);
void DrawPDFDimensions(FILE* f, long* stream_len, Dimension* dims, int dimCount, Shape FAR* FAR* shapes, int shapeCount, double pageScale, double offX, double offY, double minX, double minY, double pt_h, const char* unitName, double rScale, double cosR, double sinR, double objCx, double objCy, double lcx, double lcy);

/* --- Utility Functions --- */
void sanitize_pdf_string(char* str) {
    while(*str) {
        if(*str == '(' || *str == ')' || *str == '\\') *str = '_';
        str++;
    }
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

/* --- Memory Cleanup Engine --- */
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

/* --- C Parsing Engine --- */
void SilentLoadC(const char* path, RefCache FAR* ref) {
    FILE* f; long sz; char *d, *cur;
    ref->shapeCount = 0; ref->dimCount = 0;
    strcpy(ref->unitName, "mm");
    
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
            char *pgDef = strstr(st, "PAGE_DEF(");
            char *colorSearch, *next = pt ? pt : lStr;
            Shape FAR* s;

            if (lStr && (!next || lStr < next)) next = lStr;
            if (ext && (!next || ext < next)) next = ext; 
            if (tag && (!next || tag < next)) next = tag;
            if (dim && (!next || dim < next)) next = dim;
            if (pgDef && (!next || pgDef < next)) next = pgDef;
            if (!next || next >= endBlock) break;

            if (next == pgDef) {
                char* pOpen = strchr(next, '(');
                char* q1 = pOpen ? FindQuote(pOpen) : NULL;
                char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                char* comma1 = q2 ? strchr(q2 + 1, ',') : NULL;
                char* comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;
                char* q3 = comma2 ? FindQuote(comma2 + 1) : NULL;
                char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                char* q5 = q4 ? FindQuote(q4 + 1) : NULL;
                char* q6 = q5 ? FindQuote(q5 + 1) : NULL;
                
                if (q5 && q6) {
                    int l2 = q6 - (q5 + 1); if (l2 > 31) l2 = 31;
                    strncpy(ref->unitName, q5 + 1, l2); ref->unitName[l2] = '\0';
                }
                st = strchr(next, ';'); if (!st) st = next + 1; continue;
            }

            if (next == dim) { 
                int s1, p1, s2, p2, md; double off, tp;
                if (sscanf(next, "DIMENSION(%d , %d , %d , %d , %lf , %lf , %d)", &s1, &p1, &s2, &p2, &off, &tp, &md) == 7) {
                    if (ref->dimCount < MAX_DIMS) {
                        ref->dims[ref->dimCount].s1 = s1; ref->dims[ref->dimCount].p1 = p1;
                        ref->dims[ref->dimCount].s2 = s2; ref->dims[ref->dimCount].p2 = p2;
                        ref->dims[ref->dimCount].offset = off; ref->dims[ref->dimCount].textPos = tp;
                        ref->dims[ref->dimCount].mode = md; ref->dimCount++;
                    }
                }
                st = strchr(next, ';'); if (!st) st = next + 1; continue; 
            }

            if (!ref->shapes[ref->shapeCount]) {
                ref->shapes[ref->shapeCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                if (!ref->shapes[ref->shapeCount]) break;
            }
            s = ref->shapes[ref->shapeCount];

            memset(s, 0, sizeof(Shape));
            s->useFill = 0; s->useStroke = 1; s->fill = RGB(128, 128, 128); s->stroke = RGB(0, 0, 0);
            s->strokeWidth = 1; s->fontSize = 24;

            colorSearch = st;
            while (colorSearch < next) {
                int cr, cg, cb, sw;
                if (sscanf(colorSearch, "CreateSolidBrush(RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) { s->fill = RGB(cr, cg, cb); s->useFill = 1; }
                if (sscanf(colorSearch, "CreatePen(PS_SOLID, %d, RGB(%d,%d,%d))", &sw, &cr, &cg, &cb) == 4) { s->stroke = RGB(cr, cg, cb); s->useStroke = 1; s->strokeWidth = sw; }
                else if (sscanf(colorSearch, "CreatePen(PS_SOLID, 1, RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) { s->stroke = RGB(cr, cg, cb); s->useStroke = 1; s->strokeWidth = 1; }
                colorSearch++;
            }

            if (next == pt) {
                char *pC = next, *bracket = strchr(next, '}');
                char *polyChk = strstr(next, "Polyline(");
                char *polyGon = strstr(next, "POLY(");
                s->type = (polyChk && (!polyGon || polyChk < polyGon)) ? 2 : 0;
                
                while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) {
                    char* comma = strchr(pC + 3, ',');
                    if (comma && comma < bracket && s->ptCount < MAX_POINTS) {
                        s->ptsX[s->ptCount] = atof(pC + 3); s->ptsY[s->ptCount] = atof(comma + 1); s->ptCount++;
                    }
                    pC += 3;
                }
                if (s->ptCount > 0) ref->shapeCount++;
            } else if (next == lStr) {
                s->type = 1; s->ptCount = 2;
                if (sscanf(next, "L(%lf,%lf,%lf,%lf)", &s->ptsX[0], &s->ptsY[0], &s->ptsX[1], &s->ptsY[1]) == 4) ref->shapeCount++;
            } else if (next == ext) {
                static char refStr[128]; static char tagStr[256]; tagStr[0] = '\0';
                char* pOpen = strchr(next, '(');
                char* q1 = pOpen ? FindQuote(pOpen) : NULL;
                char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                if (pOpen && q1 && q2) {
                    char* comma1 = strchr(pOpen + 1, ',');
                    int rLen = q2 - (q1 + 1); if (rLen > 127) rLen = 127;
                    strncpy(refStr, q1 + 1, rLen); refStr[rLen] = '\0';
                    
                    char* comma3 = q2 ? strchr(q2 + 1, ',') : NULL;
                    if (comma3) {
                        char* q3 = FindQuote(comma3); char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                        if (q3 && q4) {
                            int tLen = q4 - (q3 + 1); if (tLen > 255) tLen = 255;
                            strncpy(tagStr, q3 + 1, tLen); tagStr[tLen] = '\0';
                        }
                    }
                    s->type = 3; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = comma1 ? atof(comma1 + 1) : 0.0;
                    s->ptCount = 1; strcpy(s->text, refStr); 
                    UnescapeCString(tagStr, s->tagData, 128);
                    ref->shapeCount++;
                }
            } else if (next == tag) {
                char* pOpen = strchr(next, '(');
                char* q1 = pOpen ? FindQuote(pOpen) : NULL;
                char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                
                if (pOpen) {
                    char* comma1 = strchr(pOpen, ',');
                    if (comma1 && (!q1 || comma1 < q1)) {
                        s->type = 4; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = atof(comma1 + 1); s->ptCount = 1;
                        char* comma2 = strchr(comma1 + 1, ',');
                        char* pClose = strchr(comma2 ? comma2 : pOpen, ')');

                        if (q1 && q2 && q1 < pClose) {
                            int tLen = q2 - (q1 + 1); if (tLen > 127) tLen = 127;
                            static char rawTag[256]; strncpy(rawTag, q1 + 1, tLen); rawTag[tLen] = '\0';
                            UnescapeCString(rawTag, s->text, 128);
                            
                            char* commaAfter = strchr(q2, ',');
                            if (commaAfter && commaAfter < pClose) {
                                char jChar = 'L'; char* jPtr = commaAfter + 1;
                                while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++;
                                jChar = toupper((unsigned char)*jPtr);
                                if (jChar == 'C') s->useFill = 1; else if (jChar == 'R') s->useFill = 2; else s->useFill = 0;
                                char* comma4 = strchr(commaAfter + 1, ',');
                                if (comma4 && comma4 < pClose) s->fontSize = atoi(comma4 + 1);
                            } else s->useFill = 0;
                        } else if (comma2) {
                            char* comma3 = strchr(comma2 + 1, ',');
                            char* endMarker = comma3 ? comma3 : pClose;
                            if (endMarker) {
                                int tLen = endMarker - (comma2 + 1); if (tLen > 127) tLen = 127;
                                static char rawTag[256]; strncpy(rawTag, comma2 + 1, tLen); rawTag[tLen] = '\0';
                                char* start = rawTag; while(*start && isspace((unsigned char)*start)) start++;
                                char* end = start + strlen(start) - 1; while(end > start && isspace((unsigned char)*end)) *end-- = '\0';
                                UnescapeCString(start, s->text, 128);
                                
                                if (comma3) {
                                    char jChar = 'L'; char* jPtr = comma3 + 1;
                                    while (*jPtr && isspace((unsigned char)*jPtr)) jPtr++;
                                    jChar = toupper((unsigned char)*jPtr);
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
            st = strchr(next, ';'); if (!st) st = next + 1;
        }
    }
    GlobalFreePtr(d);
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
    FILE* f; long sz; char *d, *cur, *endBlock, *st, *pt, *lStr, *ext, *tag, *next;
    int i, cId, tmpCount, tmpDimCount;
    Shape FAR* tmpShapes[MAX_SHAPES];
    Dimension tmpDims[MAX_DIMS];

    memset(tmpShapes, 0, sizeof(tmpShapes));
    FreeAllData();

    strcpy(loadedCFile, path);
    f = fopen(path, "rb"); 
    if (!f) { loadedCFile[0] = '\0'; return 0; }
    
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 60000L) { fclose(f); loadedCFile[0] = '\0'; return 0; }

    d = (char*)GlobalAllocPtr(GHND, (size_t)sz + 1);
    if (d) {
        fread(d, 1, (size_t)sz, f); d[sz] = '\0'; fclose(f); cur = d;
        while ((cur = strstr(cur, "case ")) != NULL && parsedCount < MAX_ICONS) {
            char tmpPageName[64]; char tmpUnitName[32]; double tmpPageScale; char tmpTitle[64];
            int tmpGridH = 0, tmpGridW = 0;
            
            cId = atoi(cur + 5); endBlock = strstr(cur, "break;");
            tmpCount = 0; tmpDimCount = 0; st = cur;
            strcpy(tmpPageName, "A4 (210x297)"); strcpy(tmpUnitName, "mm"); tmpPageScale = 1.0; tmpTitle[0] = '\0';
            if (!endBlock) endBlock = cur + strlen(cur);

            while (st < endBlock && tmpCount < MAX_SHAPES) {
                char* colorSearch; 
                char *dim = strstr(st, "DIMENSION(");
                char *pgDef = strstr(st, "PAGE_DEF(");
                pt = strstr(st, "POINT "); lStr = strstr(st, "L(");
                ext = strstr(st, "EXT_REF("); tag = strstr(st, "TAG_TEXT(");
                Shape FAR* s;

                next = pt ? pt : lStr;
                if (lStr && (!next || lStr < next)) next = lStr;
                if (ext && (!next || ext < next)) next = ext;
                if (tag && (!next || tag < next)) next = tag;
                if (dim && (!next || dim < next)) next = dim;
                if (pgDef && (!next || pgDef < next)) next = pgDef;
                if (!next || next >= endBlock) break;

                if (next == pgDef) {
                    char* pOpen = strchr(next, '(');
                    char* q1 = pOpen ? FindQuote(pOpen) : NULL;
                    char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                    char* comma1 = q2 ? strchr(q2 + 1, ',') : NULL;
                    char* comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;
                    char* q3 = comma2 ? FindQuote(comma2 + 1) : NULL;
                    char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                    char* q5 = q4 ? FindQuote(q4 + 1) : NULL;
                    char* q6 = q5 ? FindQuote(q5 + 1) : NULL;
                    char* commaFinal = q6 ? strchr(q6 + 1, ',') : NULL;
                    
                    if (q1 && q2 && comma1 && comma2 && q3 && q4 && q5 && q6 && commaFinal) {
                        int l0 = q2 - (q1 + 1); if (l0 > 63) l0 = 63;
                        strncpy(tmpTitle, q1 + 1, l0); tmpTitle[l0] = '\0';
                        
                        tmpGridH = atoi(comma1 + 1);
                        tmpGridW = atoi(comma2 + 1);
                        
                        int l1 = q4 - (q3 + 1); if (l1 > 63) l1 = 63;
                        strncpy(tmpPageName, q3 + 1, l1); tmpPageName[l1] = '\0';
                        
                        int l2 = q6 - (q5 + 1); if (l2 > 31) l2 = 31;
                        strncpy(tmpUnitName, q5 + 1, l2); tmpUnitName[l2] = '\0';
                        
                        tmpPageScale = atof(commaFinal + 1);
                    }
                    st = strchr(next, ';'); if (!st) st = next + 1; continue;
                }

                if (next == dim) { 
                    int s1, p1, s2, p2, md; double off, tp;
                    if (sscanf(next, "DIMENSION(%d , %d , %d , %d , %lf , %lf , %d)", &s1, &p1, &s2, &p2, &off, &tp, &md) == 7) {
                        if (tmpDimCount < MAX_DIMS) {
                            tmpDims[tmpDimCount].s1 = s1; tmpDims[tmpDimCount].p1 = p1;
                            tmpDims[tmpDimCount].s2 = s2; tmpDims[tmpDimCount].p2 = p2;
                            tmpDims[tmpDimCount].offset = off; tmpDims[tmpDimCount].textPos = tp;
                            tmpDims[tmpDimCount].mode = md; tmpDimCount++;
                        }
                    }
                    st = strchr(next, ';'); if (!st) st = next + 1; continue; 
                }

                if (!tmpShapes[tmpCount]) tmpShapes[tmpCount] = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                s = tmpShapes[tmpCount];
                if (!s) break;

                memset(s, 0, sizeof(Shape));
                s->useFill = 0; s->useStroke = 1; s->fill = RGB(128, 128, 128); s->stroke = RGB(0, 0, 0);
                s->strokeWidth = 1; s->fontSize = 24;

                colorSearch = st;
                while (colorSearch < next) {
                    int cr, cg, cb, sw;
                    if (sscanf(colorSearch, "CreateSolidBrush(RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) { s->fill = RGB(cr, cg, cb); s->useFill = 1; }
                    if (sscanf(colorSearch, "CreatePen(PS_SOLID, %d, RGB(%d,%d,%d))", &sw, &cr, &cg, &cb) == 4) { s->stroke = RGB(cr, cg, cb); s->useStroke = 1; s->strokeWidth = sw; }
                    else if (sscanf(colorSearch, "CreatePen(PS_SOLID, 1, RGB(%d,%d,%d))", &cr, &cg, &cb) == 3) { s->stroke = RGB(cr, cg, cb); s->useStroke = 1; s->strokeWidth = 1; }
                    colorSearch++;
                }

                if (next == pt) {
                    char *pC = next, *bracket = strchr(next, '}');
                    char *polyChk = strstr(next, "Polyline(");
                    char *polyGon = strstr(next, "POLY(");
                    s->type = (polyChk && (!polyGon || polyChk < polyGon)) ? 2 : 0;

                    while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) {
                        char* comma = strchr(pC + 3, ',');
                        if (comma && comma < bracket && s->ptCount < MAX_POINTS) {
                            s->ptsX[s->ptCount] = atof(pC + 3); s->ptsY[s->ptCount] = atof(comma + 1); s->ptCount++;
                        }
                        pC += 3;
                    }
                    if (s->ptCount > 0) tmpCount++;
                } else if (next == lStr) {
                    s->type = 1; s->ptCount = 2;
                    if (sscanf(next, "L(%lf,%lf,%lf,%lf)", &s->ptsX[0], &s->ptsY[0], &s->ptsX[1], &s->ptsY[1]) == 4) tmpCount++;
                } else if (next == ext) {
                    static char refStr[128]; static char tagStr[256]; tagStr[0] = '\0';
                    char* pOpen = strchr(next, '(');
                    char* q1 = pOpen ? FindQuote(pOpen) : NULL;
                    char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                    if (pOpen && q1 && q2) {
                        char* comma1 = strchr(pOpen + 1, ',');
                        int rLen = q2 - (q1 + 1); if (rLen > 127) rLen = 127;
                        strncpy(refStr, q1 + 1, rLen); refStr[rLen] = '\0';
                        char* comma3 = q2 ? strchr(q2 + 1, ',') : NULL;
                        if (comma3) {
                            char* q3 = FindQuote(comma3); char* q4 = q3 ? FindQuote(q3 + 1) : NULL;
                            if (q3 && q4) {
                                int tLen = q4 - (q3 + 1); if (tLen > 255) tLen = 255;
                                strncpy(tagStr, q3 + 1, tLen); tagStr[tLen] = '\0';
                            }
                        }
                        s->type = 3; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = comma1 ? atof(comma1 + 1) : 0.0;
                        s->ptCount = 1; strcpy(s->text, refStr); 
                        UnescapeCString(tagStr, s->tagData, 128);
                        tmpCount++;
                    }
                } else if (next == tag) {
                    char* pOpen = strchr(next, '('); char* q1 = pOpen ? FindQuote(pOpen) : NULL; char* q2 = q1 ? FindQuote(q1 + 1) : NULL;
                    if (pOpen) {
                        char* comma1 = strchr(pOpen, ',');
                        if (comma1 && (!q1 || comma1 < q1)) {
                            s->type = 4; s->ptsX[0] = atof(pOpen + 1); s->ptsY[0] = atof(comma1 + 1); s->ptCount = 1;
                            char* comma2 = strchr(comma1 + 1, ','); char* pClose = strchr(comma2 ? comma2 : pOpen, ')');
                            if (q1 && q2 && q1 < pClose) {
                                int tLen = q2 - (q1 + 1); if (tLen > 127) tLen = 127;
                                static char rawTag[256]; strncpy(rawTag, q1 + 1, tLen); rawTag[tLen] = '\0';
                                UnescapeCString(rawTag, s->text, 128);
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
                                    int tLen = endMarker - (comma2 + 1); if (tLen > 127) tLen = 127;
                                    static char rawTag[256]; strncpy(rawTag, comma2 + 1, tLen); rawTag[tLen] = '\0';
                                    char* start = rawTag; while(*start && isspace((unsigned char)*start)) start++;
                                    char* end = start + strlen(start) - 1; while(end > start && isspace((unsigned char)*end)) *end-- = '\0';
                                    UnescapeCString(start, s->text, 128);
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
                st = strchr(next, ';'); if (!st) st = next + 1;
            }

            parsedIcons[parsedCount] = (IconDef FAR*)GlobalAllocPtr(GHND, sizeof(IconDef));
            if (parsedIcons[parsedCount]) {
                memset(parsedIcons[parsedCount], 0, sizeof(IconDef));
                parsedIcons[parsedCount]->caseId = cId;
                parsedIcons[parsedCount]->dimCount = tmpDimCount;
                for(i=0; i<tmpDimCount; i++) parsedIcons[parsedCount]->dims[i] = tmpDims[i];
                
                strcpy(parsedIcons[parsedCount]->name, tmpTitle);
                strcpy(parsedIcons[parsedCount]->pageName, tmpPageName);
                strcpy(parsedIcons[parsedCount]->unitName, tmpUnitName);
                
                parsedIcons[parsedCount]->pageScale = tmpPageScale;
                parsedIcons[parsedCount]->gridH = tmpGridH;
                parsedIcons[parsedCount]->gridW = tmpGridW;
                
                for(i = 0; i < tmpCount; i++) {
                    parsedIcons[parsedCount]->shapes[i] = tmpShapes[i];
                    tmpShapes[i] = NULL;
                }
                parsedIcons[parsedCount]->shapeCount = tmpCount;
                parsedCount++;
            }
            cur = endBlock;
        }
        GlobalFreePtr(d);
    } else {
        loadedCFile[0] = '\0'; fclose(f); return 0;
    }

    for (i = 0; i < MAX_SHAPES; i++) {
        if (tmpShapes[i]) GlobalFreePtr(tmpShapes[i]);
    }
    return parsedCount;
}

/* --- Direct PDF Disk Generation Engine --- */
long pdf_objects[2048];
int pdf_obj_cnt = 1;

void pdf_out(FILE* f, long* stream_len, const char* fmt, ...) {
    char buf[1024];
    va_list args; int n;
    if (!f) return;
    va_start(args, fmt);
    n = vsprintf(buf, fmt, args);
    va_end(args);
    if (n > 0) {
        fwrite(buf, 1, n, f);
        if (stream_len) *stream_len += n;
    }
}

void pdf_color(FILE* f, long* stream_len, int is_stroke, long hex_color) {
    double r = (hex_color & 0xFF) / 255.0;
    double g = ((hex_color >> 8) & 0xFF) / 255.0;
    double b = ((hex_color >> 16) & 0xFF) / 255.0;
    pdf_out(f, stream_len, "%.3f %.3f %.3f %s\n", r, g, b, is_stroke ? "RG" : "rg");
}

void pdf_text_color(FILE* f, long* stream_len, long hex_color) {
    double r = (hex_color & 0xFF) / 255.0;
    double g = ((hex_color >> 8) & 0xFF) / 255.0;
    double b = ((hex_color >> 16) & 0xFF) / 255.0;
    pdf_out(f, stream_len, "%.3f %.3f %.3f rg\n", r, g, b); 
}

void CalcBoundingBox(Shape FAR* FAR* shapes, int count, double* outMinX, double* outMinY, double* outMaxX, double* outMaxY) {
    int i, p; double minX = 99999.0, minY = 99999.0, maxX = -99999.0, maxY = -99999.0;
    for (i=0; i<count; i++) {
        Shape FAR* sh = shapes[i];
        if (sh->type == 3) {
            char refPath[260]; double rScale = 1.0; double rRot = 0.0; char *pScale, *pRot, *pEnd; int pathLen;
            if (strncmp(sh->text, "{{EXT_REF=", 10) == 0) {
                pScale = strstr(sh->text, " scale="); pRot = strstr(sh->text, " rot="); pEnd = strstr(sh->text, "}}");
                if (pScale) pathLen = (int)(pScale - (sh->text + 10)); 
                else if (pRot) pathLen = (int)(pRot - (sh->text + 10)); 
                else if (pEnd) pathLen = (int)(pEnd - (sh->text + 10)); 
                else pathLen = strlen(sh->text + 10);
                
                if (pathLen > 0 && pathLen < 260) {
                    strncpy(refPath, sh->text + 10, pathLen); refPath[pathLen] = '\0';
                    while (pathLen > 0 && isspace((unsigned char)refPath[pathLen - 1])) refPath[--pathLen] = '\0';
                    if (pScale) sscanf(pScale, " scale=%lf", &rScale);
                    if (pRot) sscanf(pRot, " rot=%lf", &rRot);
                    
                    char absPath[260]; int rIdx; ResolvePath(loadedCFile, refPath, absPath);
                    if (!loadedCFile[0] || stricmp(absPath, loadedCFile) != 0) {
                        rIdx = EnsureRefLoaded(absPath);
                        if (rIdx != -1) {
                            double lcx = (refCache[rIdx]->minX + refCache[rIdx]->maxX) / 2.0;
                            double lcy = (refCache[rIdx]->minY + refCache[rIdx]->maxY) / 2.0;
                            double objCx = sh->ptsX[0] + lcx * rScale;
                            double objCy = sh->ptsY[0] + lcy * rScale;
                            double rRad = rRot * PI / 180.0, cosR = cos(rRad), sinR = sin(rRad);
                            double bx[4], by[4]; int r;
                            
                            bx[0] = refCache[rIdx]->minX; by[0] = refCache[rIdx]->minY;
                            bx[1] = refCache[rIdx]->maxX; by[1] = refCache[rIdx]->minY;
                            bx[2] = refCache[rIdx]->maxX; by[2] = refCache[rIdx]->maxY;
                            bx[3] = refCache[rIdx]->minX; by[3] = refCache[rIdx]->maxY;
                            
                            for (r = 0; r < 4; r++) {
                                double dx_r = (bx[r] - lcx) * rScale;
                                double dy_r = (by[r] - lcy) * rScale;
                                double px_t = objCx + (dx_r * cosR - dy_r * sinR);
                                double py_t = objCy + (dx_r * sinR + dy_r * cosR);
                                
                                sh->ptsX[r + 1] = px_t;
                                sh->ptsY[r + 1] = py_t;

                                minX = fmin(minX, px_t); maxX = fmax(maxX, px_t);
                                minY = fmin(minY, py_t); maxY = fmax(maxY, py_t);
                            }
                            sh->ptsX[5] = objCx;
                            sh->ptsY[5] = objCy;
                            sh->ptCount = 6;
                        }
                    }
                }
            }
        } else if (sh->type == 4) {
            minX = fmin(minX, sh->ptsX[0]); maxX = fmax(maxX, sh->ptsX[0]);
            minY = fmin(minY, sh->ptsY[0]); maxY = fmax(maxY, sh->ptsY[0]);
        } else {
            for (p=0; p<sh->ptCount; p++) {
                minX = fmin(minX, sh->ptsX[p]); maxX = fmax(maxX, sh->ptsX[p]);
                minY = fmin(minY, sh->ptsY[p]); maxY = fmax(maxY, sh->ptsY[p]);
            }
        }
    }
    if (minX > maxX) { minX = 0; maxX = 32; minY = 0; maxY = 32; }
    if (maxX - minX < 1.0) maxX = minX + 32.0;
    if (maxY - minY < 1.0) maxY = minY + 32.0;
    *outMinX = minX; *outMinY = minY; *outMaxX = maxX; *outMaxY = maxY;
}

void DrawPDFShape(FILE* f, long* len, Shape FAR* sh, double pageScale, double offX, double offY, double minX, double minY, double pt_h, double iconPageScale) {
    if (sh->type == 3) {
        char refPath[260]; double rScale = 1.0, rRot = 0.0;
        char *pScale, *pRot, *pEnd; int pathLen;
        if (strncmp(sh->text, "{{EXT_REF=", 10) != 0) return;
        pScale = strstr(sh->text, " scale="); pRot = strstr(sh->text, " rot="); pEnd = strstr(sh->text, "}}");
        if (pScale) pathLen = (int)(pScale - (sh->text + 10));
        else if (pEnd) pathLen = (int)(pEnd - (sh->text + 10));
        else pathLen = strlen(sh->text + 10);
        
        if (pathLen <= 0 || pathLen >= 260) return;
        strncpy(refPath, sh->text + 10, pathLen); refPath[pathLen] = '\0';
        while (pathLen > 0 && isspace((unsigned char)refPath[pathLen - 1])) refPath[--pathLen] = '\0';
        if (pScale) sscanf(pScale, " scale=%lf", &rScale);
        if (pRot) sscanf(pRot, " rot=%lf", &rRot);
        
        char absPath[260]; int rIdx;
        ResolvePath(loadedCFile, refPath, absPath);
        if (loadedCFile[0] && stricmp(absPath, loadedCFile) == 0) return; 
        rIdx = EnsureRefLoaded(absPath);
        if (rIdx != -1) {
            double lcx = (refCache[rIdx]->minX + refCache[rIdx]->maxX) / 2.0;
            double lcy = (refCache[rIdx]->minY + refCache[rIdx]->maxY) / 2.0;
            double objCx = sh->ptsX[0] + lcx * rScale;
            double objCy = sh->ptsY[0] + lcy * rScale;
            double rRad = rRot * PI / 180.0, cosR = cos(rRad), sinR = sin(rRad);
            int refTagIdx = 0, r;
            int currentPt = 6;

            for (r = 0; r < refCache[rIdx]->shapeCount; r++) {
                Shape FAR* sub = refCache[rIdx]->shapes[r];
                
                if (sub->type == 3) {
                    if (g_RenderRefDepth < 3 && strncmp(sub->text, "{{EXT_REF=", 10) == 0) {
                        Shape FAR* tempRef = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                        if (tempRef) {
                            char subPath[260]; double subSc = 1.0, subRot = 0.0;
                            char *spS = strstr(sub->text, " scale="), *spR = strstr(sub->text, " rot="), *spE = strstr(sub->text, "}}");
                            int sLen, ptIdx;
                            
                            *tempRef = *sub;
                            if (spS) sLen = (int)(spS - (sub->text + 10)); else if (spE) sLen = (int)(spE - (sub->text + 10)); else sLen = strlen(sub->text + 10);
                            if (sLen > 0 && sLen < 260) {
                                strncpy(subPath, sub->text + 10, sLen); subPath[sLen] = '\0';
                                while (sLen > 0 && isspace((unsigned char)subPath[sLen - 1])) subPath[--sLen] = '\0';
                                if (spS) sscanf(spS, " scale=%lf", &subSc);
                                if (spR) sscanf(spR, " rot=%lf", &subRot);
                                
                                double dx = (sub->ptsX[0] - lcx) * rScale;
                                double dy = (sub->ptsY[0] - lcy) * rScale;
                                tempRef->ptsX[0] = objCx + dx * cosR - dy * sinR;
                                tempRef->ptsY[0] = objCy + dx * sinR + dy * cosR;
                                sprintf(tempRef->text, "{{EXT_REF=%s scale=%.2f rot=%.2f}}", subPath, subSc * rScale, subRot + rRot);
                                
                                g_RenderRefDepth++;
                                DrawPDFShape(f, len, tempRef, pageScale, offX, offY, minX, minY, pt_h, iconPageScale);
                                g_RenderRefDepth--;
                                
                                for (ptIdx = 0; ptIdx < tempRef->ptCount; ptIdx++) {
                                    if (currentPt < MAX_POINTS) {
                                        sh->ptsX[currentPt] = tempRef->ptsX[ptIdx];
                                        sh->ptsY[currentPt] = tempRef->ptsY[ptIdx];
                                        currentPt++;
                                    }
                                }
                            }
                            GlobalFreePtr(tempRef);
                        }
                    }
                    continue;
                }

                if (sub->type == 4) {
                    Shape FAR* tShp = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tShp) {
                        char tagBuf[128];
                        double dx = (sub->ptsX[0] - lcx) * rScale, dy = (sub->ptsY[0] - lcy) * rScale;
                        double wx = objCx + dx * cosR - dy * sinR, wy = objCy + dx * sinR + dy * cosR;
                        char instVal[128]; GetPipeValue(sh->tagData, refTagIdx, instVal, 128);
                        
                        if (strlen(instVal) > 0) { strncpy(tagBuf, instVal, 127); tagBuf[127] = '\0'; } 
                        else { strncpy(tagBuf, sub->text, 127); tagBuf[127] = '\0'; }
                        
                        if (tagBuf[0] == '{' && tagBuf[1] == '{') {
                            char tempBuf[128];
                            strcpy(tempBuf, tagBuf + 2);
                            char *pEndClean = strstr(tempBuf, "}}");
                            if (pEndClean) *pEndClean = '\0';
                            strcpy(tagBuf, tempBuf);
                        }
                        
                        if (strcmp(tagBuf, "SHEETSCALE") == 0) {
                            if (iconPageScale > 0 && iconPageScale <= 1.0) sprintf(tagBuf, "1:%g", 1.0 / iconPageScale);
                            else sprintf(tagBuf, "%g", iconPageScale);
                        }

                        if (currentPt < MAX_POINTS) {
                            sh->ptsX[currentPt] = wx; sh->ptsY[currentPt] = wy; currentPt++;
                        }

                        *tShp = *sub; tShp->ptsX[0] = wx; tShp->ptsY[0] = wy; strcpy(tShp->text, tagBuf);
                        tShp->fontSize = sub->fontSize > 0 ? (int)(sub->fontSize * rScale) : (int)(24 * rScale);
                        tShp->strokeWidth = sub->strokeWidth > 0 ? (int)(sub->strokeWidth * rScale) : (int)(1 * rScale);
                        DrawPDFShape(f, len, tShp, pageScale, offX, offY, minX, minY, pt_h, iconPageScale);
                        GlobalFreePtr(tShp);
                    }
                    refTagIdx++;
                } else {
                    Shape FAR* tmpShp = (Shape FAR*)GlobalAllocPtr(GHND, sizeof(Shape));
                    if (tmpShp) {
                        int pIdx;
                        *tmpShp = *sub;
                        for (pIdx = 0; pIdx < sub->ptCount; pIdx++) {
                            double dx = (sub->ptsX[pIdx] - lcx) * rScale, dy = (sub->ptsY[pIdx] - lcy) * rScale;
                            double wx = objCx + dx * cosR - dy * sinR;
                            double wy = objCy + dx * sinR + dy * cosR;
                            tmpShp->ptsX[pIdx] = wx; tmpShp->ptsY[pIdx] = wy;
                            if (currentPt < MAX_POINTS) {
                                sh->ptsX[currentPt] = wx; sh->ptsY[currentPt] = wy; currentPt++;
                            }
                        }
                        tmpShp->strokeWidth = sub->strokeWidth > 0 ? (int)(sub->strokeWidth * rScale) : (int)(1 * rScale);
                        DrawPDFShape(f, len, tmpShp, pageScale, offX, offY, minX, minY, pt_h, iconPageScale);
                        GlobalFreePtr(tmpShp);
                    }
                }
            }
            
            if (refCache[rIdx]->dimCount > 0) {
                DrawPDFDimensions(f, len, refCache[rIdx]->dims, refCache[rIdx]->dimCount, refCache[rIdx]->shapes, refCache[rIdx]->shapeCount, pageScale, offX, offY, minX, minY, pt_h, refCache[rIdx]->unitName, rScale, cosR, sinR, objCx, objCy, lcx, lcy);
            }
            
            sh->ptCount = currentPt;
        }
        return;
    }
    
    if (sh->type == 4) {
        char dispT[256]; double tx, ty; double pdfFSize, text_w, anchor_x;
        if (sh->text[0] == '{' && sh->text[1] == '{') {
            strcpy(dispT, sh->text + 2); char *pEnd = strstr(dispT, "}}");
            if (pEnd) *pEnd = '\0';
        } else { strcpy(dispT, sh->text); }
        sanitize_pdf_string(dispT);

        if (strcmp(dispT, "SHEETSCALE") == 0) {
            if (iconPageScale > 0 && iconPageScale <= 1.0) sprintf(dispT, "1:%g", 1.0 / iconPageScale);
            else sprintf(dispT, "%g", iconPageScale);
        }

        tx = offX + (sh->ptsX[0] - minX) * pageScale;
        ty = pt_h - (offY + (sh->ptsY[0] - minY) * pageScale);
        pdfFSize = (double)((sh->fontSize > 0 ? sh->fontSize : 24) / 10.0 * pageScale);
        if (pdfFSize < 2.0) pdfFSize = 2.0;
        
        pdf_text_color(f, len, sh->stroke);
        text_w = lstrlen(dispT) * pdfFSize * 0.5; 
        anchor_x = tx;
        if (sh->useFill == 1) anchor_x -= text_w / 2.0;
        else if (sh->useFill == 2) anchor_x -= text_w;
        
        pdf_out(f, len, "BT /F1 %.2f Tf %.2f %.2f Td (%s) Tj ET\n", pdfFSize, anchor_x, ty - pdfFSize, dispT);
        return;
    }

    pdf_color(f, len, 0, sh->fill);
    pdf_color(f, len, 1, sh->stroke);
    
    double sw = (sh->strokeWidth > 0 ? sh->strokeWidth : 1.0) / 10.0 * pageScale;
    pdf_out(f, len, "2 J\n"); 
    pdf_out(f, len, "%.2f w\n", sw);
    
    {
        int p;
        for (p=0; p < sh->ptCount; p++) {
            double px = offX + (sh->ptsX[p] - minX) * pageScale;
            double py = pt_h - (offY + (sh->ptsY[p] - minY) * pageScale);
            if (p == 0) pdf_out(f, len, "%.2f %.2f m\n", px, py);
            else pdf_out(f, len, "%.2f %.2f l\n", px, py);
        }
    }

    if (sh->type == 0) {
        if (sh->useFill && sh->useStroke) pdf_out(f, len, "B\n");
        else if (sh->useFill) pdf_out(f, len, "f\n");
        else if (sh->useStroke) pdf_out(f, len, "S\n");
    } else {
        if (sh->useStroke) pdf_out(f, len, "S\n");
    }
}

void DrawPDFDimensions(FILE* f, long* stream_len, Dimension* dims, int dimCount, Shape FAR* FAR* shapes, int shapeCount, double pageScale, double offX, double offY, double minX, double minY, double pt_h, const char* unitName, double rScale, double cosR, double sinR, double objCx, double objCy, double lcx, double lcy) {
    int i;
    for (i = 0; i < dimCount; i++) {
        double lA1x, lA1y, lA2x, lA2y, lD1x, lD1y, lD2x, lD2y;
        double gA1x, gA1y, gA2x, gA2y, gD1x, gD1y, gD2x, gD2y, gMidX, gMidY;
        double pdfA1x, pdfA1y, pdfA2x, pdfA2y, pdfD1x, pdfD1y, pdfD2x, pdfD2y, pdfMidX, pdfMidY;
        double lDx, lDy, ang, nx, ny, val, lGridOffset, lMidX, lMidY;
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
            if (lDx == 0.0 && lDy == 0.0) ang = 0.0;
            else ang = atan2(lDy, lDx);
            nx = -sin(ang); ny = cos(ang);
            lD1x = lA1x + nx * lGridOffset; lD1y = lA1y + ny * lGridOffset;
            lD2x = lA2x + nx * lGridOffset; lD2y = lA2y + ny * lGridOffset;
            val = sqrt(pow(lA2x - lA1x, 2) + pow(lA2y - lA1y, 2));
        } else if (dims[i].mode == 1) { 
            lD1x = lA1x; lD1y = lA1y - lGridOffset;
            lD2x = lA2x; lD2y = lA1y - lGridOffset;
            val = fabs(lDx);
        } else { 
            lD1x = lA1x + lGridOffset; lD1y = lA1y;
            lD2x = lA1x + lGridOffset; lD2y = lA2y;
            val = fabs(lDy);
        }

        lMidX = lD1x + (lD2x - lD1x) * dims[i].textPos;
        lMidY = lD1y + (lD2y - lD1y) * dims[i].textPos;

        #define TRANSFORM_PT(lx, ly, gx, gy) \
            do { \
                double tx = ((lx) - lcx) * rScale; \
                double ty = ((ly) - lcy) * rScale; \
                (gx) = objCx + tx * cosR - ty * sinR; \
                (gy) = objCy + tx * sinR + ty * cosR; \
            } while(0)

        TRANSFORM_PT(lA1x, lA1y, gA1x, gA1y);
        TRANSFORM_PT(lA2x, lA2y, gA2x, gA2y);
        TRANSFORM_PT(lD1x, lD1y, gD1x, gD1y);
        TRANSFORM_PT(lD2x, lD2y, gD2x, gD2y);
        TRANSFORM_PT(lMidX, lMidY, gMidX, gMidY);

        #define MAP_X(gx) (offX + ((gx) - minX) * pageScale)
        #define MAP_Y(gy) (pt_h - (offY + ((gy) - minY) * pageScale))

        pdfA1x = MAP_X(gA1x); pdfA1y = MAP_Y(gA1y);
        pdfA2x = MAP_X(gA2x); pdfA2y = MAP_Y(gA2y);
        pdfD1x = MAP_X(gD1x); pdfD1y = MAP_Y(gD1y);
        pdfD2x = MAP_X(gD2x); pdfD2y = MAP_Y(gD2y);
        pdfMidX = MAP_X(gMidX); pdfMidY = MAP_Y(gMidY);

        FormatDimension(val, unitName, buf);

        pdf_color(f, stream_len, 1, RGB(0, 128, 255));
        pdf_out(f, stream_len, "0.5 w\n");

        double L1dx = pdfD1x - pdfA1x, L1dy = pdfD1y - pdfA1y;
        double L1len = sqrt(L1dx*L1dx + L1dy*L1dy);
        if (L1len > 5.0) {
            pdf_out(f, stream_len, "%.2f %.2f m %.2f %.2f l S\n", 
                pdfA1x + L1dx/L1len*5.0, pdfA1y + L1dy/L1len*5.0,
                pdfD1x - L1dx/L1len*2.0, pdfD1y - L1dy/L1len*2.0);
        }
        
        double L2dx = pdfD2x - pdfA2x, L2dy = pdfD2y - pdfA2y;
        double L2len = sqrt(L2dx*L2dx + L2dy*L2dy);
        if (L2len > 5.0) {
            pdf_out(f, stream_len, "%.2f %.2f m %.2f %.2f l S\n", 
                pdfA2x + L2dx/L2len*5.0, pdfA2y + L2dy/L2len*5.0,
                pdfD2x - L2dx/L2len*2.0, pdfD2y - L2dy/L2len*2.0);
        }

        pdf_out(f, stream_len, "%.2f %.2f m %.2f %.2f l S\n", pdfD1x, pdfD1y, pdfD2x, pdfD2y);
        
        pdf_color(f, stream_len, 0, RGB(0, 128, 255));
        double dimDx = pdfD2x - pdfD1x, dimDy = pdfD2y - pdfD1y;
        double dimLen = sqrt(dimDx*dimDx + dimDy*dimDy);
        if (dimLen > 0) {
            double dirX = dimDx/dimLen, dirY = dimDy/dimLen;
            pdf_out(f, stream_len, "%.2f %.2f m %.2f %.2f l %.2f %.2f l f\n",
                pdfD1x, pdfD1y,
                pdfD1x + dirX*8 - dirY*2.5, pdfD1y + dirY*8 + dirX*2.5,
                pdfD1x + dirX*8 + dirY*2.5, pdfD1y + dirY*8 - dirX*2.5);
            pdf_out(f, stream_len, "%.2f %.2f m %.2f %.2f l %.2f %.2f l f\n",
                pdfD2x, pdfD2y,
                pdfD2x - dirX*8 - dirY*2.5, pdfD2y - dirY*8 + dirX*2.5,
                pdfD2x - dirX*8 + dirY*2.5, pdfD2y - dirY*8 - dirX*2.5);
        }

        double pdfFSize = 10.0;
        double text_w = lstrlen(buf) * pdfFSize * 0.55; 
        
        pdf_color(f, stream_len, 0, RGB(255, 255, 255));
        pdf_out(f, stream_len, "%.2f %.2f %.2f %.2f re f\n", pdfMidX - text_w/2.0 - 2.0, pdfMidY - pdfFSize/2.0 - 2.0, text_w + 4.0, pdfFSize + 4.0);
        
        pdf_text_color(f, stream_len, RGB(0, 128, 255));
        pdf_out(f, stream_len, "BT /F1 %.2f Tf %.2f %.2f Td (%s) Tj ET\n", pdfFSize, pdfMidX - text_w/2.0, pdfMidY - pdfFSize/3.0, buf);
        
        #undef TRANSFORM_PT
        #undef MAP_X
        #undef MAP_Y
    }
}

int generate_pdf(const char* out_file, const char* in_file, float margH_pct, float margV_pct) {
    FILE* f; int info_obj, catalog_obj, pages_obj, font1_obj, font2_obj;
    const char* title_file;
    const char* pdf_title; char safe_title[64];
    int page_list[MAX_ICONS]; int page_cnt = 0;
    long xref_pos; int i;
    
    if (!LoadCFile(in_file)) return 0;
    
    f = fopen(out_file, "wb"); if (!f) return 0;

    fprintf(f, "%%PDF-1.0\n");
    info_obj = 1; catalog_obj = 2; pages_obj = 3; font1_obj = 4; font2_obj = 5; pdf_obj_cnt = 6;
    title_file = strrchr(out_file, '\\'); if (!title_file) title_file = strrchr(out_file, '/');
    if (!title_file) title_file = out_file; else title_file++;

    pdf_title = title_file;
    if (parsedCount > 0 && parsedIcons[0]->name[0] != '\0') {
        strncpy(safe_title, parsedIcons[0]->name, 63);
        safe_title[63] = '\0';
        sanitize_pdf_string(safe_title);
        pdf_title = safe_title;
    }

    pdf_objects[info_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Title (%s) /Creator (DRA-PDF) /Producer (DRA-PDF) /Author (DRA-PDF) >>\nendobj\n", info_obj, pdf_title);

    pdf_objects[catalog_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n", catalog_obj, pages_obj);

    pdf_objects[font1_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n", font1_obj);

    pdf_objects[font2_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n", font2_obj);

    for (i=0; i<parsedCount; i++) {
        double minX, minY, maxX, maxY, scaleX, scaleY, scale, offX, offY; float gw, gh; int j;
        int page_obj, stream_obj, len_obj;
        long stream_len = 0;
        int gridW, gridH;
        double drawW, drawH;
        
        /* Default to Landscape Letter if not explicitly found */
        float page_w = 279.4f, page_h = 215.9f;
        float pt_w, pt_h, m_x, m_y;
        char* paren;
        
        if (!parsedIcons[i]->shapes || parsedIcons[i]->shapeCount <= 0) continue;
        
        /* Parse Width x Height dynamically from the PAGE_DEF name string (e.g., "Letter (279.4x215.9)") */
        paren = strchr(parsedIcons[i]->pageName, '(');
        if (paren) {
            float tw = 0.0f, th = 0.0f;
            if (sscanf(paren + 1, "%fx%f", &tw, &th) == 2 || sscanf(paren + 1, "%f x %f", &tw, &th) == 2) {
                page_w = tw;
                page_h = th;
            }
        }
        
        pt_w = page_w * 72.0f / 25.4f; 
        pt_h = page_h * 72.0f / 25.4f;
        m_x = pt_w * (margH_pct / 100.0f); 
        m_y = pt_h * (margV_pct / 100.0f);
        
        CalcBoundingBox(parsedIcons[i]->shapes, parsedIcons[i]->shapeCount, &minX, &minY, &maxX, &maxY);
        
        drawW = maxX - minX;
        drawH = maxY - minY;
        if (drawW < 1.0) drawW = 1.0;
        if (drawH < 1.0) drawH = 1.0;

        if (parsedIcons[i]->gridW > 0 && parsedIcons[i]->gridH > 0) {
            gridW = parsedIcons[i]->gridW;
            gridH = parsedIcons[i]->gridH;
        } else {
            gridW = (int)(drawW + 2.5);
            gridH = (int)(drawH + 2.5);
            if (gridW < 32) gridW = 32;
            if (gridH < 32) gridH = 32;
        }

        gw = pt_w - m_x*2; gh = pt_h - m_y*2;
        scaleX = gw / (double)gridW; 
        scaleY = gh / (double)gridH;
        scale = fmin(scaleX, scaleY);
        
        offX = m_x + (gw - drawW*scale) / 2.0;
        offY = m_y + (gh - drawH*scale) / 2.0;

        page_obj = pdf_obj_cnt++; stream_obj = pdf_obj_cnt++; len_obj = pdf_obj_cnt++;
        page_list[page_cnt++] = page_obj;

        pdf_objects[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
            page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

        pdf_objects[stream_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %d 0 R >>\nstream\n", stream_obj, len_obj);
        
        g_RenderRefDepth = 0;
        for (j=0; j<parsedIcons[i]->shapeCount; j++) {
            DrawPDFShape(f, &stream_len, parsedIcons[i]->shapes[j], scale, offX, offY, minX, minY, pt_h, parsedIcons[i]->pageScale);
        }
        
        if (parsedIcons[i]->dimCount > 0) {
            DrawPDFDimensions(f, &stream_len, parsedIcons[i]->dims, parsedIcons[i]->dimCount, parsedIcons[i]->shapes, parsedIcons[i]->shapeCount, scale, offX, offY, minX, minY, pt_h, parsedIcons[i]->unitName, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        }

        fprintf(f, "endstream\nendobj\n");
        
        pdf_objects[len_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n%ld\nendobj\n", len_obj, stream_len);
    }

    pdf_objects[pages_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Pages /Count %d /Kids [ ", pages_obj, page_cnt);
    for (i = 0; i < page_cnt; i++) fprintf(f, "%d 0 R ", page_list[i]);
    fprintf(f, "] >>\nendobj\n");

    xref_pos = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", pdf_obj_cnt);
    for(i = 1; i < pdf_obj_cnt; i++) fprintf(f, "%010ld 00000 n \n", pdf_objects[i]);
    
    fprintf(f, "trailer\n<< /Size %d /Root %d 0 R /Info %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n", pdf_obj_cnt, catalog_obj, info_obj, xref_pos);
    
    fclose(f);
    return 1;
}

/* --- GUI Callbacks & Init --- */
void CreatePDFAction(HWND hwnd) {
    char c_path[MAX_PATH]; char out_path[MAX_PATH]; char final_out[MAX_PATH]; OPENFILENAME ofn;
    GetWindowText(hTxtCFile, c_path, MAX_PATH);
    GetWindowText(hTxtOutFile, out_path, MAX_PATH);

    if (strlen(out_path) == 0) {
        strcpy(final_out, "output.pdf");
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = "PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = final_out; ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        
        if (!GetSaveFileName(&ofn)) return;
    } else {
        if (strchr(out_path, '\\') || strchr(out_path, '/') || strchr(out_path, ':')) {
            strcpy(final_out, out_path);
        } else {
            char cwd[MAX_PATH];
            if (getcwd(cwd, MAX_PATH)) {
                sprintf(final_out, "%s\\%s", cwd, out_path);
            } else {
                strcpy(final_out, out_path);
            }
        }
        if (!strrchr(final_out, '.') || stricmp(strrchr(final_out, '.'), ".pdf") != 0) {
            strcat(final_out, ".pdf");
        }
    }

    {
        char margH_str[16], margV_str[16]; float margH_pct, margV_pct;
        GetWindowText(hTxtMargH, margH_str, 16); GetWindowText(hTxtMargV, margV_str, 16);
        margH_pct = (float)atof(margH_str); margV_pct = (float)atof(margV_str);
        
        if (generate_pdf(final_out, c_path, margH_pct, margV_pct)) {
            SetWindowText(hStatus, " Status: Successfully Generated PDF.");
        } else {
            SetWindowText(hStatus, " Error: Failed to generate PDF.");
        }
    }
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            CreateWindow("STATIC", "C File:", WS_VISIBLE | WS_CHILD, 10, 15, 80, 20, hwnd, NULL, NULL, NULL);
            hTxtCFile = CreateWindow("EDIT", "icons.c", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 90, 15, 200, 22, hwnd, NULL, NULL, NULL);
            hBtnBrowse = CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD, 300, 15, 80, 22, hwnd, (HMENU)1, NULL, NULL);

            CreateWindow("STATIC", "Marg(%):", WS_VISIBLE | WS_CHILD, 10, 50, 65, 20, hwnd, NULL, NULL, NULL);
            hTxtMargH = CreateWindow("EDIT", "0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 75, 50, 45, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "H", WS_VISIBLE | WS_CHILD, 125, 50, 15, 20, hwnd, NULL, NULL, NULL);
            hTxtMargV = CreateWindow("EDIT", "0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 145, 50, 45, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "V", WS_VISIBLE | WS_CHILD, 195, 50, 15, 20, hwnd, NULL, NULL, NULL);

            CreateWindow("STATIC", "Output:", WS_VISIBLE | WS_CHILD, 10, 85, 65, 20, hwnd, NULL, NULL, NULL);
            hTxtOutFile = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 75, 85, 225, 22, hwnd, NULL, NULL, NULL);

            hBtnCreate = CreateWindow("BUTTON", "Create PDF", WS_VISIBLE | WS_CHILD, 80, 120, 100, 30, hwnd, (HMENU)3, NULL, NULL);
            hBtnExit = CreateWindow("BUTTON", "Exit", WS_VISIBLE | WS_CHILD, 200, 120, 100, 30, hwnd, (HMENU)4, NULL, NULL);
            
            hStatus = CreateWindow("STATIC", " Ready. Drag and drop a .c file here.", WS_VISIBLE | WS_CHILD | WS_BORDER | SS_LEFT, 0, 160, 420, 20, hwnd, NULL, NULL, NULL);
            break;
        }
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char dropPath[MAX_PATH];
            if (DragQueryFile(hDrop, 0, dropPath, MAX_PATH)) {
                SetWindowText(hTxtCFile, dropPath);
                SetWindowText(hStatus, " File Loaded. Ready to generate PDF.");
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
                    SetWindowText(hTxtCFile, path);
                    SetWindowText(hStatus, " File Selected. Ready to generate PDF.");
                }
            } else if (id == 3) { 
                CreatePDFAction(hwnd);
            } else if (id == 4) { 
                PostQuitMessage(0); 
            }
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
    
    /* CLI Mode: Quietly process the file without loading the GUI */
    if (lpCmdLine && lpCmdLine[0] != '\0') {
        char in_path[260];
        char out_path[260];
        char cwd[260];
        char* pExt;
        char* p = lpCmdLine;
        
        while (*p == ' ' || *p == '\t' || *p == '"') p++;
        strcpy(in_path, p);
        pExt = in_path + strlen(in_path) - 1;
        while (pExt >= in_path && (*pExt == ' ' || *pExt == '\t' || *pExt == '"')) {
            *pExt = '\0';
            pExt--;
        }
        
        if (in_path[0]) {
            if (getcwd(cwd, 260)) {
                strcat(cwd, "\\dummy.c"); 
            } else {
                cwd[0] = '\0';
            }
            
            ResolvePath(cwd, in_path, out_path);
            strcpy(in_path, out_path);
            
            pExt = strrchr(out_path, '.');
            if (pExt) strcpy(pExt, ".pdf");
            else strcat(out_path, ".pdf");
            
            generate_pdf(out_path, in_path, 0.0f, 0.0f);
            FreeAllData();
            return 0;
        }
    }

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DRA_PDF_CLASS";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow("DRA_PDF_CLASS", "DRA-PDF C-Icon Converter", 
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 220, NULL, NULL, hInstance, NULL);

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    while(GetMessage(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    FreeAllData();
    return msg.wParam;
}
/* EOF */
