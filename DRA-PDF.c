/* ============================================================================
 * DRA-PDF C-Icon to PDF Generator - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s dra-pdf.c commdlg.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s dra-pdf.c
 *     wlink system windows option quiet option packcode option stack=16k name dra-pdf.exe file dra-pdf.obj library windows.lib library commdlg.lib
 *
 * REQUIREMENTS: Windows 3.1x (Win16)
 * DEPENDENCIES: USER, GDI, COMDLG
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <direct.h>
#include <stdarg.h>

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
    int caseId; 
    char name[64]; 
    Shape* shapes; 
    int shapeCount; 
} IconDef;

typedef struct {
    char path[260];
    Shape* shapes;
    int shapeCount;
    double minX, minY, maxX, maxY;
} RefCache;

IconDef* parsedIcons = NULL;
int parsedCount = 0;
RefCache* refCache = NULL;
int refCacheCount = 0;
char loadedCFile[260] = "";

/* --- GUI Globals --- */
HWND hMain, hTxtCFile, hBtnBrowse;
HWND hTxtWidth, hTxtHeight, hBtnToggle, hTxtMargH, hTxtMargV;
HWND hBtnCreate, hBtnExit;

int isLandscape = 0;
float page_w_mm = 215.9f; 
float page_h_mm = 279.4f;

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

/* --- C Parsing Engine --- */
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
            char *tag = strstr(st, "TAG_TEXT("), *dim = strstr(st, "DIMENSION(");
            char *ext = strstr(st, "EXT_REF(");
            char *colorSearch, *next = pt ? pt : lStr;
            Shape* s = &ref->shapes[ref->shapeCount];

            if (lStr && (!next || lStr < next)) next = lStr;
            if (ext && (!next || ext < next)) next = ext;
            if (tag && (!next || tag < next)) next = tag;
            if (dim && (!next || dim < next)) next = dim;
            if (!next || next >= endBlock) break;
            if (next == dim) { st = strchr(next, ';'); if (!st) st = next + 1; continue; }

            memset(s, 0, sizeof(Shape));
            s->useFill = 1; s->useStroke = 1; s->fill = RGB(128, 128, 128); s->stroke = RGB(0, 0, 0);
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
                s->type = (polyChk && polyChk < endBlock) ? 2 : 0;
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
    for (i = 0; i < refCacheCount; i++) if (stricmp(refCache[i].path, path) == 0) return i;
    if (refCacheCount >= MAX_REFS) return -1;
    
    if (!refCache[refCacheCount].shapes) {
        refCache[refCacheCount].shapes = (Shape*)GlobalAllocPtr(GHND, MAX_SHAPES * sizeof(Shape));
        if (!refCache[refCacheCount].shapes) return -1;
    }
    strncpy(refCache[refCacheCount].path, path, 259); refCache[refCacheCount].path[259] = '\0';
    SilentLoadC(path, &refCache[refCacheCount]);

    minX = 99999.0; minY = 99999.0; maxX = -99999.0; maxY = -99999.0;
    for (i = 0; i < refCache[refCacheCount].shapeCount; i++) {
        Shape* sh = &refCache[refCacheCount].shapes[i];
        for (j = 0; j < sh->ptCount; j++) {
            minX = fmin(minX, sh->ptsX[j]); maxX = fmax(maxX, sh->ptsX[j]);
            minY = fmin(minY, sh->ptsY[j]); maxY = fmax(maxY, sh->ptsY[j]);
        }
    }
    if (minX > maxX) { minX = 0; minY = 0; maxX = 32; maxY = 32; }
    refCache[refCacheCount].minX = minX; refCache[refCacheCount].minY = minY;
    refCache[refCacheCount].maxX = maxX; refCache[refCacheCount].maxY = maxY;
    return refCacheCount++;
}

int LoadCFile(const char* path) {
    FILE* f; long sz; char *d, *cur, *endBlock, *st, *pt, *lStr, *ext, *tag, *next;
    int i, cId, tmpCount; Shape* exact;
    Shape* tmpShapes = (Shape*)GlobalAllocPtr(GHND, MAX_SHAPES * sizeof(Shape));

    if (!tmpShapes) return 0;
    strcpy(loadedCFile, path);
    f = fopen(path, "rb"); 
    if (!f) { loadedCFile[0] = '\0'; GlobalFreePtr(tmpShapes); return 0; }
    
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 60000L) { fclose(f); loadedCFile[0] = '\0'; GlobalFreePtr(tmpShapes); return 0; }

    for (i = 0; i < MAX_ICONS; i++) {
        if (parsedIcons[i].shapes) { GlobalFreePtr(parsedIcons[i].shapes); parsedIcons[i].shapes = NULL; }
        parsedIcons[i].shapeCount = 0;
    }
    parsedCount = 0; refCacheCount = 0;

    d = (char*)GlobalAllocPtr(GHND, (size_t)sz + 1);
    if (d) {
        fread(d, 1, (size_t)sz, f); d[sz] = '\0'; fclose(f); cur = d;
        while ((cur = strstr(cur, "case ")) != NULL && parsedCount < MAX_ICONS) {
            cId = atoi(cur + 5); endBlock = strstr(cur, "break;");
            tmpCount = 0; st = cur;
            if (!endBlock) endBlock = cur + strlen(cur);

            while (st < endBlock && tmpCount < MAX_SHAPES) {
                char* colorSearch; Shape* s = &tmpShapes[tmpCount];
                char *dim = strstr(st, "DIMENSION(");
                pt = strstr(st, "POINT "); lStr = strstr(st, "L(");
                ext = strstr(st, "EXT_REF("); tag = strstr(st, "TAG_TEXT(");

                next = pt ? pt : lStr;
                if (lStr && (!next || lStr < next)) next = lStr;
                if (ext && (!next || ext < next)) next = ext;
                if (tag && (!next || tag < next)) next = tag;
                if (dim && (!next || dim < next)) next = dim;
                if (!next || next >= endBlock) break;

                if (next == dim) { st = strchr(next, ';'); if (!st) st = next + 1; continue; }

                memset(s, 0, sizeof(Shape));
                s->useFill = 1; s->useStroke = 1; s->fill = RGB(128, 128, 128); s->stroke = RGB(0, 0, 0);
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
                    s->type = (polyChk && polyChk < endBlock) ? 2 : 0;
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

            parsedIcons[parsedCount].caseId = cId;
            if (tmpCount > 0) {
                exact = (Shape*)GlobalAllocPtr(GHND, tmpCount * sizeof(Shape));
                if (exact) { 
                    memcpy(exact, tmpShapes, tmpCount * sizeof(Shape)); 
                    parsedIcons[parsedCount].shapes = exact; 
                    parsedIcons[parsedCount].shapeCount = tmpCount;
                } else {
                    parsedIcons[parsedCount].shapes = NULL;
                    parsedIcons[parsedCount].shapeCount = 0;
                }
            } else {
                parsedIcons[parsedCount].shapes = NULL;
                parsedIcons[parsedCount].shapeCount = 0;
            }
            parsedCount++; cur = endBlock;
        }
        GlobalFreePtr(d);
    } else {
        loadedCFile[0] = '\0'; fclose(f); GlobalFreePtr(tmpShapes); return 0;
    }
    GlobalFreePtr(tmpShapes);
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

void DrawPDFShape(FILE* f, long* len, Shape* sh, double pageScale, double offX, double offY, double minX, double minY, double pt_h) {
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
            double lcx = (refCache[rIdx].minX + refCache[rIdx].maxX) / 2.0;
            double lcy = (refCache[rIdx].minY + refCache[rIdx].maxY) / 2.0;
            double objCx = sh->ptsX[0] + lcx * rScale;
            double objCy = sh->ptsY[0] + lcy * rScale;
            double rRad = rRot * PI / 180.0, cosR = cos(rRad), sinR = sin(rRad);
            int refTagIdx = 0, r;

            for (r = 0; r < refCache[rIdx].shapeCount; r++) {
                Shape* sub = &refCache[rIdx].shapes[r];
                if (sub->type == 3) continue;

                if (sub->type == 4) {
                    char tagBuf[128]; Shape tShp;
                    double dx = (sub->ptsX[0] - lcx) * rScale, dy = (sub->ptsY[0] - lcy) * rScale;
                    double wx = objCx + dx * cosR - dy * sinR, wy = objCy + dx * sinR + dy * cosR;
                    char instVal[128]; GetPipeValue(sh->tagData, refTagIdx, instVal, 128);
                    
                    if (strlen(instVal) > 0) { strncpy(tagBuf, instVal, 127); tagBuf[127] = '\0'; } 
                    else { strncpy(tagBuf, sub->text, 127); tagBuf[127] = '\0'; }
                    
                    if (tagBuf[0] == '{' && tagBuf[1] == '{') {
                        char tempBuf[128];
                        char *pEndClean = strstr(tagBuf + 2, "}}");
                        if (pEndClean) *pEndClean = '\0';
                        strcpy(tempBuf, tagBuf + 2);
                        strcpy(tagBuf, tempBuf);
                    }
                    
                    tShp = *sub; tShp.ptsX[0] = wx; tShp.ptsY[0] = wy; strcpy(tShp.text, tagBuf);
                    tShp.fontSize = sub->fontSize > 0 ? (int)(sub->fontSize * rScale) : (int)(24 * rScale);
                    tShp.strokeWidth = sub->strokeWidth > 0 ? (int)(sub->strokeWidth * rScale) : (int)(1 * rScale);
                    DrawPDFShape(f, len, &tShp, pageScale, offX, offY, minX, minY, pt_h);
                    refTagIdx++;
                } else {
                    Shape tmpShp = *sub; int pIdx;
                    for (pIdx = 0; pIdx < sub->ptCount; pIdx++) {
                        double dx = (sub->ptsX[pIdx] - lcx) * rScale, dy = (sub->ptsY[pIdx] - lcy) * rScale;
                        tmpShp.ptsX[pIdx] = objCx + dx * cosR - dy * sinR; tmpShp.ptsY[pIdx] = objCy + dx * sinR + dy * cosR;
                    }
                    tmpShp.strokeWidth = sub->strokeWidth > 0 ? (int)(sub->strokeWidth * rScale) : (int)(1 * rScale);
                    DrawPDFShape(f, len, &tmpShp, pageScale, offX, offY, minX, minY, pt_h);
                }
            }
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

void CalcBoundingBox(Shape* shapes, int count, double* outMinX, double* outMinY, double* outMaxX, double* outMaxY) {
    int i, p; double minX = 99999.0, minY = 99999.0, maxX = -99999.0, maxY = -99999.0;
    for (i=0; i<count; i++) {
        Shape* sh = &shapes[i];
        if (sh->type == 3) {
            char refPath[260]; double rScale = 1.0; char *pScale, *pEnd; int pathLen;
            if (strncmp(sh->text, "{{EXT_REF=", 10) == 0) {
                pScale = strstr(sh->text, " scale="); pEnd = strstr(sh->text, "}}");
                if (pScale) pathLen = (int)(pScale - (sh->text + 10)); else if (pEnd) pathLen = (int)(pEnd - (sh->text + 10)); else pathLen = strlen(sh->text + 10);
                if (pathLen > 0 && pathLen < 260) {
                    strncpy(refPath, sh->text + 10, pathLen); refPath[pathLen] = '\0';
                    while (pathLen > 0 && isspace((unsigned char)refPath[pathLen - 1])) refPath[--pathLen] = '\0';
                    if (pScale) sscanf(pScale, " scale=%lf", &rScale);
                    char absPath[260]; int rIdx; ResolvePath(loadedCFile, refPath, absPath);
                    if (!loadedCFile[0] || stricmp(absPath, loadedCFile) != 0) {
                        rIdx = EnsureRefLoaded(absPath);
                        if (rIdx != -1) {
                            double lcx = (refCache[rIdx].minX + refCache[rIdx].maxX) / 2.0;
                            double lcy = (refCache[rIdx].minY + refCache[rIdx].maxY) / 2.0;
                            double objCx = sh->ptsX[0] + lcx * rScale;
                            double objCy = sh->ptsY[0] + lcy * rScale;
                            double bx[4], by[4]; int r;
                            bx[0] = refCache[rIdx].minX; by[0] = refCache[rIdx].minY;
                            bx[1] = refCache[rIdx].maxX; by[1] = refCache[rIdx].minY;
                            bx[2] = refCache[rIdx].maxX; by[2] = refCache[rIdx].maxY;
                            bx[3] = refCache[rIdx].minX; by[3] = refCache[rIdx].maxY;
                            for (r = 0; r < 4; r++) {
                                double dx = (bx[r] - lcx) * rScale;
                                double dy = (by[r] - lcy) * rScale;
                                minX = fmin(minX, objCx + dx); maxX = fmax(maxX, objCx + dx);
                                minY = fmin(minY, objCy + dy); maxY = fmax(maxY, objCy + dy);
                            }
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

void generate_pdf(const char* out_file, const char* in_file, float margH_pct, float margV_pct) {
    FILE* f; int info_obj, catalog_obj, pages_obj, font1_obj, font2_obj;
    float pt_w, pt_h, m_x, m_y; const char* title_file;
    int page_list[1024]; int page_cnt = 0;
    long xref_pos; int i;
    
    if (!LoadCFile(in_file)) return;
    
    f = fopen(out_file, "wb"); if (!f) return;
    pt_w = page_w_mm * 72.0f / 25.4f; pt_h = page_h_mm * 72.0f / 25.4f;
    m_x = pt_w * (margH_pct / 100.0f); m_y = pt_h * (margV_pct / 100.0f);

    fprintf(f, "%%PDF-1.0\n");
    info_obj = 1; catalog_obj = 2; pages_obj = 3; font1_obj = 4; font2_obj = 5; pdf_obj_cnt = 6;
    title_file = strrchr(out_file, '\\'); if (!title_file) title_file = strrchr(out_file, '/');
    if (!title_file) title_file = out_file; else title_file++;

    pdf_objects[info_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Title (%s) /Creator (DRA-PDF) /Producer (DRA-PDF) /Author (DRA-PDF) >>\nendobj\n", info_obj, title_file);

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
        
        if (!parsedIcons[i].shapes || parsedIcons[i].shapeCount <= 0) continue;
        
        CalcBoundingBox(parsedIcons[i].shapes, parsedIcons[i].shapeCount, &minX, &minY, &maxX, &maxY);
        gw = pt_w - m_x*2; gh = pt_h - m_y*2;
        scaleX = gw / (maxX - minX); scaleY = gh / (maxY - minY);
        scale = fmin(scaleX, scaleY);
        offX = m_x + (gw - (maxX - minX)*scale) / 2.0;
        offY = m_y + (gh - (maxY - minY)*scale) / 2.0;

        page_obj = pdf_obj_cnt++; stream_obj = pdf_obj_cnt++; len_obj = pdf_obj_cnt++;
        page_list[page_cnt++] = page_obj;

        pdf_objects[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
            page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

        pdf_objects[stream_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %d 0 R >>\nstream\n", stream_obj, len_obj);
        
        for (j=0; j<parsedIcons[i].shapeCount; j++) {
            DrawPDFShape(f, &stream_len, &parsedIcons[i].shapes[j], scale, offX, offY, minX, minY, pt_h);
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
}

/* --- GUI Callbacks & Init --- */
void CreatePDFAction(HWND hwnd) {
    char c_path[MAX_PATH]; char out_path[MAX_PATH]; OPENFILENAME ofn;
    GetWindowText(hTxtCFile, c_path, MAX_PATH);

    strcpy(out_path, "output.pdf");
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = out_path; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    
    if (GetSaveFileName(&ofn)) {
        char margH_str[16], margV_str[16]; float margH_pct, margV_pct;
        GetWindowText(hTxtMargH, margH_str, 16); GetWindowText(hTxtMargV, margV_str, 16);
        margH_pct = (float)atof(margH_str); margV_pct = (float)atof(margV_str);
        
        generate_pdf(out_path, c_path, margH_pct, margV_pct);
        MessageBox(hwnd, "PDF Created Successfully!", "Success", MB_ICONASTERISK);
    }
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "C File:", WS_VISIBLE | WS_CHILD, 10, 15, 80, 20, hwnd, NULL, NULL, NULL);
            hTxtCFile = CreateWindow("EDIT", "icons.c", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 90, 15, 200, 22, hwnd, NULL, NULL, NULL);
            hBtnBrowse = CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD, 300, 15, 80, 22, hwnd, (HMENU)1, NULL, NULL);

            CreateWindow("STATIC", "Size(mm):", WS_VISIBLE | WS_CHILD, 10, 50, 65, 20, hwnd, NULL, NULL, NULL);
            hTxtWidth = CreateWindow("EDIT", "215.9", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 75, 50, 45, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "x", WS_VISIBLE | WS_CHILD, 123, 50, 10, 20, hwnd, NULL, NULL, NULL);
            hTxtHeight = CreateWindow("EDIT", "279.4", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 135, 50, 45, 22, hwnd, NULL, NULL, NULL);
            
            hBtnToggle = CreateWindow("BUTTON", "Land", WS_VISIBLE | WS_CHILD, 185, 50, 45, 22, hwnd, (HMENU)2, NULL, NULL);

            CreateWindow("STATIC", "Marg(%):", WS_VISIBLE | WS_CHILD, 240, 50, 55, 20, hwnd, NULL, NULL, NULL);
            hTxtMargH = CreateWindow("EDIT", "4", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 295, 50, 30, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "H", WS_VISIBLE | WS_CHILD, 330, 50, 15, 20, hwnd, NULL, NULL, NULL);
            hTxtMargV = CreateWindow("EDIT", "4", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 345, 50, 30, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "V", WS_VISIBLE | WS_CHILD, 380, 50, 15, 20, hwnd, NULL, NULL, NULL);

            hBtnCreate = CreateWindow("BUTTON", "Create PDF", WS_VISIBLE | WS_CHILD, 80, 95, 100, 30, hwnd, (HMENU)3, NULL, NULL);
            hBtnExit = CreateWindow("BUTTON", "Exit", WS_VISIBLE | WS_CHILD, 200, 95, 100, 30, hwnd, (HMENU)4, NULL, NULL);
            break;
        }
        case WM_COMMAND: {
            int id = wParam;
            if (id == 1) { 
                OPENFILENAME ofn; char path[MAX_PATH] = "";
                memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "C Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileName(&ofn)) SetWindowText(hTxtCFile, path);
            } else if (id == 2) { 
                char w_str[32], h_str[32]; isLandscape = !isLandscape;
                SetWindowText(hBtnToggle, isLandscape ? "Port" : "Land");
                GetWindowText(hTxtWidth, w_str, 32); GetWindowText(hTxtHeight, h_str, 32);
                SetWindowText(hTxtWidth, h_str); SetWindowText(hTxtHeight, w_str);
                page_w_mm = (float)atof(h_str); page_h_mm = (float)atof(w_str);
            } else if (id == 3) { 
                char w_str[32], h_str[32];
                GetWindowText(hTxtWidth, w_str, 32); GetWindowText(hTxtHeight, h_str, 32);
                page_w_mm = (float)atof(w_str); page_h_mm = (float)atof(h_str);
                CreatePDFAction(hwnd);
            } else if (id == 4) { PostQuitMessage(0); }
            break;
        }
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Entry Point ---
extern int _argc;
extern char **_argv;

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc; MSG msg; int i;
    
    parsedIcons = (IconDef*)GlobalAllocPtr(GHND, (DWORD)MAX_ICONS * sizeof(IconDef));
    refCache = (RefCache*)GlobalAllocPtr(GHND, (DWORD)MAX_REFS * sizeof(RefCache));
    if (!parsedIcons || !refCache) { MessageBox(NULL, "Allocation failed!", "Error", MB_ICONHAND); return 1; }

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DRA_PDF_CLASS";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow("DRA_PDF_CLASS", "DRA-PDF C-Icon Converter", 
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 180, NULL, NULL, hInstance, NULL);

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    while(GetMessage(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    if (parsedIcons) {
        for(i=0; i<MAX_ICONS; i++) if (parsedIcons[i].shapes) GlobalFreePtr(parsedIcons[i].shapes);
        GlobalFreePtr(parsedIcons);
    }
    if (refCache) {
        for(i=0; i<MAX_REFS; i++) if (refCache[i].shapes) GlobalFreePtr(refCache[i].shapes);
        GlobalFreePtr(refCache);
    }
    return msg.wParam;
}
/* EOF */