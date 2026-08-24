/* ============================================================================
 * CSV-PDF Calendar Generator - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s CSV-PDF.c commdlg.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s CSV-PDF.c
 *     wlink system windows option quiet option packcode option stack=16k name CSV-PDF.exe file CSV-PDF.obj library windows.lib library commdlg.lib
 *
 * REQUIREMENTS: Windows 3.1x (Win16)
 * DEPENDENCIES: USER, GDI, COMDLG
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define MAX_EVENTS 2048

// --- Data Structures ---
typedef struct {
    char id[64];
    char title[128];
    int startMin;
    int duration;
    long color;
    int year, month, day;
    int personIdx;
    int version;
    int lastModifiedBy;
} Event;

Event FAR* events[MAX_EVENTS];
int event_count = 0;

char people[32][64];
int people_count = 0;

const char* months[] = {"", "January", "February", "March", "April", "May", "June", 
                        "July", "August", "September", "October", "November", "December"};
const char* short_months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// --- GUI Globals ---
HWND hMain, hTxtCsv, hBtnBrowse, hCboMode, hCboFirstDay, hDateStart, hDateEnd, hCboNode;
HWND hBtnPrevMonth, hBtnNextMonth, hTxtMargH, hTxtMargV;
HWND hTxtWidth, hTxtHeight, hBtnToggle, hBtnCreate, hBtnExit;
int isLandscape = 0;
float page_w_mm = 215.9f; 
float page_h_mm = 279.4f;

const char* view_modes[] = {
    "1 Day View", "4 Day View", "Week View", 
    "4 Person View", "7 Person View", "Month View", "Upcoming Schedule"
};

const char* days_of_week[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// --- Math & Date Utilities (Bypasses 16-bit time.h limitations) ---
long DateToAbsolute(int y, int m, int d) {
    if (m < 3) { y--; m += 12; }
    return 365L * y + y/4 - y/100 + y/400 + (153L * m - 457)/5 + d - 306;
}

void AbsoluteToDate(long absDate, int* y, int* m, int* d) {
    long l = absDate + 68569 + 306;
    long n = (4 * l) / 146097;
    long i, j;
    l = l - (146097 * n + 3) / 4;
    i = (4000 * (l + 1)) / 1461001;
    l = l - (1461 * i) / 4 + 31;
    j = (80 * l) / 2447;
    *d = (int)(l - (2447 * j) / 80);
    l = j / 11;
    *m = (int)(j + 2 - (12 * l));
    *y = (int)(100 * (n - 49) + i + l);
}

int day_of_week(int y, int m, int d) {
    long absDate = DateToAbsolute(y, m, d);
    return (int)((absDate % 7L) + 1L); /* 1=Sun, 2=Mon... */
}

int days_in_month(int y, int m) {
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    if (m==4 || m==6 || m==9 || m==11) return 30;
    return 31;
}

void format_time(int min_since_midnight, char* buf) {
    int h = min_since_midnight / 60;
    int m = min_since_midnight % 60;
    const char* ampm = "AM";
    if (h >= 12) { ampm = "PM"; if (h > 12) h -= 12; }
    if (h == 0) h = 12;
    sprintf(buf, "%02d:%02d %s", h, m, ampm);
}

// --- String Utilities ---
void sanitize_pdf_string(char* str) {
    while(*str) {
        if(*str == '(' || *str == ')' || *str == '\\') *str = '_';
        str++;
    }
}

void url_decode(char *str) {
    char *p = str;
    char hex[3] = {0};
    while (*str) {
        if (*str == '%' && *(str+1) && *(str+2)) {
            hex[0] = *(str+1); hex[1] = *(str+2);
            *p++ = (char)strtol(hex, NULL, 16);
            str += 3;
        } else {
            *p++ = *str++;
        }
    }
    *p = '\0';
}

char* next_token(char** ptr) {
    char* start;
    char* p;
    
    if (!ptr || !*ptr) return NULL;
    start = *ptr;
    p = start;
    
    while (*p && (unsigned char)*p != 0xA6 && *p != '|') p++;

    if ((unsigned char)*p == 0xA6 || *p == '|') {
        *p = '\0';
        *ptr = p + 1;
    } else {
        char* nl = strpbrk(start, "\r\n");
        if (nl) *nl = '\0';
        *ptr = NULL;
    }
    return start;
}

// --- Data Loading ---
int load_ini() {
    char path[MAX_PATH];
    char* p;
    char buf[1024] = {0};
    char* token;

    GetModuleFileName(NULL, path, MAX_PATH);
    p = strrchr(path, '.');
    if (p) lstrcpy(p, ".ini");
    else lstrcat(path, ".ini");

    GetPrivateProfileString("People", "Names", "Unknown", buf, sizeof(buf), path);
    
    token = strtok(buf, ",");
    people_count = 0;
    while(token && people_count < 32) {
        strncpy(people[people_count++], token, 63);
        token = strtok(NULL, ",");
    }
    return people_count;
}

int parse_csv(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    char FAR* line;
    int i;
    
    if (!f) return 0;
    
    line = (char FAR*)malloc(2048);
    if (!line) { fclose(f); return 0; }
    
    for (i = 0; i < event_count; i++) {
        if (events[i]) free(events[i]);
    }
    event_count = 0;
    
    if(fgets(line, 2048, f)) {} /* skip header */

    while (fgets(line, 2048, f)) {
        char* ptr = line;
        char* t_id = next_token(&ptr);
        char* t_title = next_token(&ptr);
        char* t_start = next_token(&ptr);
        char* t_dur = next_token(&ptr);
        char* t_col = next_token(&ptr);
        char* t_date = next_token(&ptr);
        char* t_pidx = next_token(&ptr);
        char* t_ver = next_token(&ptr);
        char* t_lmod = next_token(&ptr);
        Event FAR* e;

        if(!t_date) continue;
        if(event_count >= MAX_EVENTS) break;

        events[event_count] = (Event FAR*)malloc(sizeof(Event));
        if (!events[event_count]) continue;
        memset(events[event_count], 0, sizeof(Event));

        e = events[event_count];
        strncpy(e->id, t_id ? t_id : "", 63);
        strncpy(e->title, t_title ? t_title : "New Event", 127);
        
        url_decode(e->title);
        sanitize_pdf_string(e->title);
        
        e->startMin = t_start ? atoi(t_start) : 0;
        e->duration = t_dur ? atoi(t_dur) : 60;
        e->color = t_col ? atol(t_col) : 0;
        
        if (t_date) sscanf(t_date, "%d/%d/%d", &e->year, &e->month, &e->day);
        e->personIdx = t_pidx ? atoi(t_pidx) : 0;
        e->version = t_ver ? atoi(t_ver) : 1;
        e->lastModifiedBy = t_lmod ? atoi(t_lmod) : 0;
        
        event_count++;
    }
    free(line);
    fclose(f);
    return event_count;
}

int CompareEvents(const void* a, const void* b) {
    Event FAR* ea = *(Event FAR**)a;
    Event FAR* eb = *(Event FAR**)b;
    if (ea->year != eb->year) return ea->year - eb->year;
    if (ea->month != eb->month) return ea->month - eb->month;
    if (ea->day != eb->day) return ea->day - eb->day;
    return ea->startMin - eb->startMin;
}

// --- PDF Generation Engine ---
long pdf_objects[4096];
int pdf_obj_cnt = 1;

typedef struct { char FAR* data; int len; int cap; } Stream;

void s_app(Stream* s, const char* fmt, ...) {
    char FAR* buf = (char FAR*)malloc(4096);
    va_list args;
    int n;
    
    if (!buf) return;
    
    va_start(args, fmt);
    n = vsprintf(buf, fmt, args);
    va_end(args);
    
    if (s->len + n >= s->cap) {
        s->cap = (s->cap == 0 ? 8192 : s->cap * 2) + n + 4096;
        if (s->data == NULL) s->data = (char FAR*)malloc(s->cap);
        else s->data = (char FAR*)realloc(s->data, s->cap);
    }
    
    if (s->data) {
        memcpy(s->data + s->len, buf, n);
        s->len += n;
        s->data[s->len] = 0;
    }
    free(buf);
}

void pdf_color(Stream* s, int is_stroke, long hex_color) {
    float r = ((hex_color >> 16) & 0xFF) / 255.0f;
    float g = ((hex_color >> 8) & 0xFF) / 255.0f;
    float b = (hex_color & 0xFF) / 255.0f;
    s_app(s, "%.3f %.3f %.3f %s\n", r, g, b, is_stroke ? "RG" : "rg");
}

void pdf_text_color(Stream* s, long hex_color) {
    int r = (int)((hex_color >> 16) & 0xFF);
    int g = (int)((hex_color >> 8) & 0xFF);
    int b = (int)(hex_color & 0xFF);
    float luma = 0.299f*r + 0.587f*g + 0.114f*b;
    if (luma > 186.0f) s_app(s, "0 0 0 rg\n"); 
    else s_app(s, "1 1 1 rg\n"); 
}

void pdf_center_text(Stream* s, const char* text, float x, float y, float w, int font_size, int is_bold) {
    float text_w = lstrlen(text) * font_size * 0.5f; 
    float offset = x + (w - text_w) / 2.0f;
    if (offset < x) offset = x; 
    s_app(s, "BT %s %d Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", is_bold ? "/F2" : "/F1", font_size, offset, y, text);
}

void pdf_rounded_rect(Stream* s, float x, float y, float w, float h, float r) {
    float k = 0.55228f * r;
    s_app(s, "%.2f %.2f m\n", x + r, y + h);
    s_app(s, "%.2f %.2f l\n", x + w - r, y + h); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + w - r + k, y + h, x + w, y + h - r + k, x + w, y + h - r);
    s_app(s, "%.2f %.2f l\n", x + w, y + r); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + w, y + r - k, x + w - r + k, y, x + w - r, y);
    s_app(s, "%.2f %.2f l\n", x + r, y); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + r - k, y, x, y + r - k, x, y + r);
    s_app(s, "%.2f %.2f l\n", x, y + h - r); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x, y + h - r + k, x + r - k, y + h, x + r, y + h);
    s_app(s, "f\n");
}

void generate_pdf(const char* out_file, int mode, int first_day, int node_filter, float margH_pct, float margV_pct, int sY, int sM, int sD, int eY, int eM, int eD) {
    FILE* f = fopen(out_file, "wb");
    int info_obj, catalog_obj, pages_obj, font1_obj, font2_obj;
    float pt_w, pt_h, m_x, m_y;
    const char* title_file;
    Event FAR** v_events;
    int v_count = 0;
    int page_list[1024];
    int page_cnt = 0;
    Stream s;
    long xref_pos;
    int i;
    
    if (!f) return;
    
    pt_w = page_w_mm * 72.0f / 25.4f;
    pt_h = page_h_mm * 72.0f / 25.4f;
    m_x = pt_w * (margH_pct / 100.0f);
    m_y = pt_h * (margV_pct / 100.0f);

    /* Enforce requested PDF 1.0 format */
    fprintf(f, "%%PDF-1.0\n");
    
    info_obj = 1;
    catalog_obj = 2;
    pages_obj = 3;
    font1_obj = 4;
    font2_obj = 5;
    pdf_obj_cnt = 6;

    title_file = strrchr(out_file, '\\');
    if (!title_file) title_file = strrchr(out_file, '/');
    if (!title_file) title_file = out_file; else title_file++;

    pdf_objects[info_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Title (%s) /Creator (CSV-PDF) /Producer (CSV-PDF) /Author (CSV-PDF) >>\nendobj\n", info_obj, title_file);

    pdf_objects[catalog_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n", catalog_obj, pages_obj);

    pdf_objects[font1_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n", font1_obj);

    pdf_objects[font2_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n", font2_obj);

    v_events = (Event FAR**)malloc(event_count * sizeof(Event FAR*));
    if (!v_events) { fclose(f); return; }
    
    for (i = 0; i < event_count; i++) {
        long d1 = DateToAbsolute(sY, sM, sD);
        long d2 = DateToAbsolute(eY, eM, eD);
        long de = DateToAbsolute(events[i]->year, events[i]->month, events[i]->day);
        
        if (de >= d1 && de <= d2) {
            if (node_filter == -1 || events[i]->personIdx == node_filter) {
                v_events[v_count++] = events[i];
            }
        }
    }
    qsort(v_events, v_count, sizeof(Event FAR*), CompareEvents);

    memset(&s, 0, sizeof(Stream));
    
    if (mode == 6) { /* --- Upcoming Schedule --- */
        float y = pt_h - m_y - 40;
        int page_obj = pdf_obj_cnt++;
        int stream_obj = pdf_obj_cnt++;
        int last_y = -1, last_m = -1, last_d = -1;
        
        page_list[page_cnt++] = page_obj;

        pdf_objects[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
            page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

        pdf_center_text(&s, "Upcoming Schedule", m_x, pt_h - m_y - 20, pt_w - m_x*2, 24, 1);

        for (i = 0; i < v_count; i++) {
            Event FAR* ev = v_events[i];
            int is_new_day = (ev->year != last_y || ev->month != last_m || ev->day != last_d);
            float space_needed = 60;
            
            if (is_new_day) space_needed += 40;

            if (y < m_y + space_needed) {
                pdf_objects[stream_obj] = ftell(f);
                fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);
                s.len = 0; 
                
                page_obj = pdf_obj_cnt++;
                stream_obj = pdf_obj_cnt++;
                page_list[page_cnt++] = page_obj;

                pdf_objects[page_obj] = ftell(f);
                fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
                    page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);
                
                y = pt_h - m_y - 40;
                pdf_center_text(&s, "Upcoming Schedule (Cont.)", m_x, pt_h - m_y - 20, pt_w - m_x*2, 16, 1);
                is_new_day = 1;
            }

            if (is_new_day) {
                char date_hd[64];
                y -= 10;
                sprintf(date_hd, "%s %d, %d", months[ev->month], ev->day, ev->year);
                s_app(&s, "BT /F2 14 Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", m_x, y, date_hd);
                y -= 5;
                s_app(&s, "0.5 0.5 0.5 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x, y, pt_w - m_x, y);
                y -= 25;
                last_y = ev->year; last_m = ev->month; last_d = ev->day;
            }

            pdf_color(&s, 0, ev->color);
            pdf_rounded_rect(&s, m_x, y - 2, 12, 12, 2);

            s_app(&s, "BT /F2 12 Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", m_x + 20, y, ev->title);
            y -= 15;

            {
                char time_start[16], time_end[16], details[256], pname[64];
                format_time(ev->startMin, time_start);
                format_time(ev->startMin + ev->duration, time_end);
                
                if (ev->lastModifiedBy < people_count && lstrlen(people[ev->lastModifiedBy]) > 0) {
                    lstrcpy(pname, people[ev->lastModifiedBy]);
                } else {
                    sprintf(pname, "Person %d", ev->lastModifiedBy + 1);
                }

                sprintf(details, "%s - %s   \\225   %s", time_start, time_end, pname);

                s_app(&s, "BT /F1 10 Tf 0.3 0.3 0.3 rg %.2f %.2f Td (%s) Tj ET\n", m_x + 20, y, details);
                y -= 20;
            }
        }

        pdf_objects[stream_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);

    } else if (mode == 5) { /* --- Month View --- */
        float gw, gh, cw, ch;
        int m_days, start_dow, start_cell, row, col, d;
        char mo_title[64];
        const char* day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        
        int page_obj = pdf_obj_cnt++;
        int stream_obj = pdf_obj_cnt++;
        page_list[page_cnt++] = page_obj;

        pdf_objects[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
            page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

        gw = pt_w - m_x*2;
        gh = pt_h - m_y*2 - 60; 
        cw = gw / 7.0f;
        ch = gh / 6.0f;

        sprintf(mo_title, "%s %d", months[sM], sY);
        pdf_center_text(&s, mo_title, m_x, pt_h - m_y - 20, gw, 24, 1);

        for(i=0; i<7; i++) {
            int d_idx = (i + first_day) % 7;
            s_app(&s, "BT /F2 12 Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", m_x + i*cw + 5, pt_h - m_y - 40, day_names[d_idx]);
        }

        m_days = days_in_month(sY, sM);
        start_dow = day_of_week(sY, sM, 1) - 1; /* 0-based for arrays */
        start_cell = (start_dow - first_day + 7) % 7;

        for (i = 0; i < 42; i++) {
            if (i < start_cell || i >= start_cell + m_days) {
                int c = i % 7;
                int r = 5 - (i / 7);
                float cx = m_x + c * cw;
                float cy = m_y + r * ch;
                s_app(&s, "0.9 0.9 0.9 rg %.2f %.2f %.2f %.2f re f\n", cx, cy, cw, ch);
            }
        }

        s_app(&s, "0.7 0.7 0.7 RG 1 w\n");
        for(i=0; i<=7; i++) { s_app(&s, "%.2f %.2f m %.2f %.2f l S\n", m_x + i*cw, m_y, m_x + i*cw, m_y + gh); }
        for(i=0; i<=6; i++) { s_app(&s, "%.2f %.2f m %.2f %.2f l S\n", m_x, m_y + i*ch, m_x + gw, m_y + i*ch); }

        row = 5; col = start_cell;
        for(d=1; d<=m_days; d++) {
            float cx = m_x + col * cw;
            float cy = m_y + row * ch;
            char dstr[4]; 
            int ev_in_cell = 0;
            float ey = cy + ch - 25;
            
            sprintf(dstr, "%d", d);
            s_app(&s, "BT /F1 10 Tf 0.2 0.2 0.2 rg %.2f %.2f Td (%s) Tj ET\n", cx + cw - 20, cy + ch - 12, dstr);

            for(i=0; i<v_count; i++) {
                if(v_events[i]->year == sY && v_events[i]->month == sM && v_events[i]->day == d) {
                    if (ev_in_cell < 4) {
                        char tt[128];
                        pdf_color(&s, 0, v_events[i]->color);
                        pdf_rounded_rect(&s, cx + 2, ey - 12, cw - 4, 14, 4);
                        
                        pdf_text_color(&s, v_events[i]->color);
                        strncpy(tt, v_events[i]->title, 127); tt[127]=0;
                        s_app(&s, "BT /F1 8 Tf %.2f %.2f Td (%s) Tj ET\n", cx + 4, ey - 8, tt);
                        ey -= 16;
                    } else if (ev_in_cell == 4) {
                        s_app(&s, "BT /F2 8 Tf 0 0 0 rg %.2f %.2f Td (+ more) Tj ET\n", cx + 4, ey - 8);
                    }
                    ev_in_cell++;
                }
            }
            col++;
            if(col > 6) { col = 0; row--; }
        }

        pdf_objects[stream_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);

    } else { /* --- Dayplanner / Column Views --- */
        long curr_abs, end_abs;
        float m_top, time_w, m_x_grid, gw, gh, cw;
        int num_cols = 1;
        int c, h;
        int is_person_view = (mode == 3 || mode == 4);
        
        if (mode == 1 || mode == 3) num_cols = 4;
        if (mode == 2 || mode == 4) num_cols = 7;
        
        m_top = m_y + 50;
        time_w = 42.0f; 
        m_x_grid = m_x + time_w;
        gw = pt_w - m_x*2 - time_w; 
        gh = pt_h - m_y - m_top;
        cw = gw / num_cols;
        
        curr_abs = DateToAbsolute(sY, sM, sD);
        end_abs = DateToAbsolute(eY, eM, eD);
        
        if (end_abs < curr_abs) end_abs = curr_abs;
        
        while (curr_abs <= end_abs) {
            int cy, cm, cd;
            int page_obj = pdf_obj_cnt++;
            int stream_obj = pdf_obj_cnt++;
            char planner_title[128];
            
            page_list[page_cnt++] = page_obj;
            AbsoluteToDate(curr_abs, &cy, &cm, &cd);

            pdf_objects[page_obj] = ftell(f);
            fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
                page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

            if (is_person_view) {
                sprintf(planner_title, "Schedule - %s %d, %d", months[cm], cd, cy);
            } else {
                if (num_cols == 1) sprintf(planner_title, "%s %d, %d", months[cm], cd, cy);
                else sprintf(planner_title, "Week of %s %d, %d", short_months[cm], cd, cy);
            }
            pdf_center_text(&s, planner_title, m_x_grid, pt_h - m_y - 20, gw, 20, 1);

            for(c=0; c<num_cols; c++) {
                char chd[64];
                if(is_person_view) {
                    if (c < people_count && lstrlen(people[c]) > 0) {
                        sprintf(chd, "%s", people[c]);
                    } else {
                        sprintf(chd, "Person %d", c + 1);
                    }
                } else {
                    int ty, tm, td;
                    AbsoluteToDate(curr_abs + c, &ty, &tm, &td);
                    sprintf(chd, "%s %d", short_months[tm], td);
                }
                pdf_center_text(&s, chd, m_x_grid + c*cw, pt_h - m_top + 10, cw, 12, 1);
                
                s_app(&s, "0.5 0.5 0.5 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x_grid + c*cw, m_y, m_x_grid + c*cw, m_y + gh);
            }
            s_app(&s, "0.5 0.5 0.5 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x_grid + gw, m_y, m_x_grid + gw, m_y + gh);

            for(h=0; h<=24; h++) {
                float hy = m_y + gh - (h / 24.0f) * gh;
                s_app(&s, "0.8 0.8 0.8 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x_grid, hy, m_x_grid + gw, hy);
                if(h < 24 && h % 2 == 0) {
                    char hl[16]; format_time(h*60, hl);
                    s_app(&s, "BT /F1 8 Tf 0.5 0.5 0.5 rg %.2f %.2f Td (%s) Tj ET\n", m_x_grid - 39, hy - 3, hl);
                }
            }
            
            for(i=0; i<v_count; i++) {
                Event FAR* ev = v_events[i];
                int col = -1;
                long ev_abs = DateToAbsolute(ev->year, ev->month, ev->day);
                
                if(is_person_view) {
                    if(ev_abs == curr_abs) {
                        if(ev->lastModifiedBy < num_cols) col = ev->lastModifiedBy;
                    }
                } else {
                    long diff = ev_abs - curr_abs;
                    if(diff >= 0 && diff < num_cols) col = (int)diff;
                }
                
                if(col >= 0) {
                    float ev_y_start = m_y + gh - (ev->startMin / 1440.0f) * gh;
                    float ev_h = (ev->duration / 1440.0f) * gh;
                    float ev_y;
                    char tt[128]; 
                    
                    if (ev_h < 12) ev_h = 12; 
                    ev_y = ev_y_start - ev_h;
                    
                    pdf_color(&s, 0, ev->color);
                    pdf_rounded_rect(&s, m_x_grid + col*cw + 2, ev_y, cw - 4, ev_h, 3);
                    
                    pdf_text_color(&s, ev->color);
                    strncpy(tt, ev->title, 127); tt[127]=0;
                    s_app(&s, "BT /F2 8 Tf %.2f %.2f Td (%s) Tj ET\n", m_x_grid + col*cw + 4, ev_y_start - 10, tt);
                }
            }

            pdf_objects[stream_obj] = ftell(f);
            fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);
            s.len = 0; 
            
            if (mode == 0 || mode == 3 || mode == 4) curr_abs += 1;
            else if (mode == 1) curr_abs += 4;
            else if (mode == 2) curr_abs += 7;
        }
    }

    pdf_objects[pages_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Pages /Count %d /Kids [ ", pages_obj, page_cnt);
    for (i = 0; i < page_cnt; i++) {
        fprintf(f, "%d 0 R ", page_list[i]);
    }
    fprintf(f, "] >>\nendobj\n");

    xref_pos = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", pdf_obj_cnt);
    for(i = 1; i < pdf_obj_cnt; i++) {
        fprintf(f, "%010ld 00000 n \n", pdf_objects[i]);
    }
    
    fprintf(f, "trailer\n<< /Size %d /Root %d 0 R /Info %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n", pdf_obj_cnt, catalog_obj, info_obj, xref_pos);
    
    if(s.data) free(s.data);
    free(v_events);
    fclose(f);
}

// --- GUI Callbacks & Init ---
void CreatePDFAction(HWND hwnd) {
    char csv_path[MAX_PATH];
    char out_path[MAX_PATH];
    char view_text[64];
    int mode, first_day, sel_node;
    char s_buf[32], e_buf[32];
    int sY, sM, sD, eY, eM, eD;
    OPENFILENAME ofn;

    GetWindowText(hTxtCsv, csv_path, MAX_PATH);
    if (!parse_csv(csv_path)) {
        MessageBox(hwnd, "Failed to load/parse CSV.", "Error", MB_ICONHAND);
        return;
    }

    mode = (int)SendMessage(hCboMode, CB_GETCURSEL, 0, 0);
    SendMessage(hCboMode, CB_GETLBTEXT, mode, (LPARAM)view_text);
    
    GetWindowText(hDateStart, s_buf, 32);
    sscanf(s_buf, "%d/%d/%d", &sY, &sM, &sD);
    GetWindowText(hDateEnd, e_buf, 32);
    sscanf(e_buf, "%d/%d/%d", &eY, &eM, &eD);
    
    sprintf(out_path, "%04d-%02d_%s.pdf", sY, sM, view_text);

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = out_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    
    if (GetSaveFileName(&ofn)) {
        char margH_str[16], margV_str[16];
        float margH_pct, margV_pct;

        first_day = (int)SendMessage(hCboFirstDay, CB_GETCURSEL, 0, 0);
        sel_node = (int)SendMessage(hCboNode, CB_GETCURSEL, 0, 0) - 1; 
        
        GetWindowText(hTxtMargH, margH_str, 16);
        GetWindowText(hTxtMargV, margV_str, 16);
        margH_pct = (float)atof(margH_str);
        margV_pct = (float)atof(margV_str);

        generate_pdf(out_path, mode, first_day, sel_node, margH_pct, margV_pct,
            sY, sM, sD, eY, eM, eD);
        
        MessageBox(hwnd, "PDF Created Successfully!", "Success", MB_ICONASTERISK);
    }
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            int i;
            char buf[32];
            time_t t = time(NULL);
            struct tm* tm = localtime(&t);
            int cy = tm->tm_year + 1900;
            int cm = tm->tm_mon + 1;

            CreateWindow("STATIC", "CSV File:", WS_VISIBLE | WS_CHILD, 10, 15, 80, 20, hwnd, NULL, NULL, NULL);
            hTxtCsv = CreateWindow("EDIT", "calendar.csv", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 90, 15, 200, 22, hwnd, NULL, NULL, NULL);
            hBtnBrowse = CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD, 300, 15, 80, 22, hwnd, (HMENU)1, NULL, NULL);

            CreateWindow("STATIC", "View:", WS_VISIBLE | WS_CHILD, 10, 50, 40, 20, hwnd, NULL, NULL, NULL);
            hCboMode = CreateWindow("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VISIBLE | WS_CHILD, 55, 50, 150, 200, hwnd, NULL, NULL, NULL);
            for(i = 0; i < 7; i++) SendMessage(hCboMode, CB_ADDSTRING, 0, (LPARAM)view_modes[i]);
            SendMessage(hCboMode, CB_SETCURSEL, 5, 0); 

            CreateWindow("STATIC", "1st Day:", WS_VISIBLE | WS_CHILD, 215, 50, 55, 20, hwnd, NULL, NULL, NULL);
            hCboFirstDay = CreateWindow("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VISIBLE | WS_CHILD, 275, 50, 100, 200, hwnd, NULL, NULL, NULL);
            for(i = 0; i < 7; i++) SendMessage(hCboFirstDay, CB_ADDSTRING, 0, (LPARAM)days_of_week[i]);
            SendMessage(hCboFirstDay, CB_SETCURSEL, 0, 0); 

            CreateWindow("STATIC", "Date Range:", WS_VISIBLE | WS_CHILD, 10, 85, 80, 20, hwnd, NULL, NULL, NULL);
            hDateStart = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER, 90, 85, 95, 22, hwnd, NULL, NULL, NULL);
            hDateEnd = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER, 195, 85, 95, 22, hwnd, NULL, NULL, NULL);
            
            sprintf(buf, "%04d/%02d/01", cy, cm);
            SetWindowText(hDateStart, buf);
            sprintf(buf, "%04d/%02d/%02d", cy, cm, days_in_month(cy, cm));
            SetWindowText(hDateEnd, buf);

            hBtnPrevMonth = CreateWindow("BUTTON", "< Prev", WS_VISIBLE | WS_CHILD, 300, 85, 55, 22, hwnd, (HMENU)5, NULL, NULL);
            hBtnNextMonth = CreateWindow("BUTTON", "Next >", WS_VISIBLE | WS_CHILD, 365, 85, 55, 22, hwnd, (HMENU)6, NULL, NULL);

            CreateWindow("STATIC", "Size(mm):", WS_VISIBLE | WS_CHILD, 10, 120, 65, 20, hwnd, NULL, NULL, NULL);
            hTxtWidth = CreateWindow("EDIT", "215.9", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 75, 120, 45, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "x", WS_VISIBLE | WS_CHILD, 123, 120, 10, 20, hwnd, NULL, NULL, NULL);
            hTxtHeight = CreateWindow("EDIT", "279.4", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 135, 120, 45, 22, hwnd, NULL, NULL, NULL);
            
            hBtnToggle = CreateWindow("BUTTON", "Land", WS_VISIBLE | WS_CHILD, 185, 120, 45, 22, hwnd, (HMENU)2, NULL, NULL);

            CreateWindow("STATIC", "Marg(%):", WS_VISIBLE | WS_CHILD, 240, 120, 55, 20, hwnd, NULL, NULL, NULL);
            hTxtMargH = CreateWindow("EDIT", "4", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 295, 120, 30, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "H", WS_VISIBLE | WS_CHILD, 330, 120, 15, 20, hwnd, NULL, NULL, NULL);
            hTxtMargV = CreateWindow("EDIT", "4", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 345, 120, 30, 22, hwnd, NULL, NULL, NULL);
            CreateWindow("STATIC", "V", WS_VISIBLE | WS_CHILD, 380, 120, 15, 20, hwnd, NULL, NULL, NULL);

            CreateWindow("STATIC", "Node Filter:", WS_VISIBLE | WS_CHILD, 10, 155, 80, 20, hwnd, NULL, NULL, NULL);
            hCboNode = CreateWindow("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VISIBLE | WS_CHILD, 90, 155, 150, 200, hwnd, NULL, NULL, NULL);
            SendMessage(hCboNode, CB_ADDSTRING, 0, (LPARAM)"Show All");
            for(i = 0; i < 32; i++) {
                char nodeStr[32];
                sprintf(nodeStr, "Node %d", i);
                SendMessage(hCboNode, CB_ADDSTRING, 0, (LPARAM)nodeStr);
            }
            SendMessage(hCboNode, CB_SETCURSEL, 0, 0);

            hBtnCreate = CreateWindow("BUTTON", "Create PDF", WS_VISIBLE | WS_CHILD, 120, 195, 100, 30, hwnd, (HMENU)3, NULL, NULL);
            hBtnExit = CreateWindow("BUTTON", "Exit", WS_VISIBLE | WS_CHILD, 240, 195, 100, 30, hwnd, (HMENU)4, NULL, NULL);
            break;
        }
        case WM_COMMAND: {
            int id = wParam;
            if (id == 1) { 
                OPENFILENAME ofn;
                char path[MAX_PATH] = "";
                memset(&ofn, 0, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = path;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileName(&ofn)) {
                    SetWindowText(hTxtCsv, path);
                }
            } else if (id == 2) { 
                char w_str[32], h_str[32];
                isLandscape = !isLandscape;
                SetWindowText(hBtnToggle, isLandscape ? "Port" : "Land");
                GetWindowText(hTxtWidth, w_str, 32);
                GetWindowText(hTxtHeight, h_str, 32);
                SetWindowText(hTxtWidth, h_str);
                SetWindowText(hTxtHeight, w_str);
                
                page_w_mm = (float)atof(h_str);
                page_h_mm = (float)atof(w_str);
            } else if (id == 3) { 
                char w_str[32], h_str[32];
                GetWindowText(hTxtWidth, w_str, 32); GetWindowText(hTxtHeight, h_str, 32);
                page_w_mm = (float)atof(w_str); page_h_mm = (float)atof(h_str);
                CreatePDFAction(hwnd);
            } else if (id == 4) { 
                PostQuitMessage(0);
            } else if (id == 5 || id == 6) { 
                char buf[32];
                int y, m, d;
                GetWindowText(hDateStart, buf, 32);
                sscanf(buf, "%d/%d/%d", &y, &m, &d);
                if (id == 5) {
                    if (m == 1) { m = 12; y--; }
                    else { m--; }
                } else {
                    if (m == 12) { m = 1; y++; }
                    else { m++; }
                }
                sprintf(buf, "%04d/%02d/01", y, m);
                SetWindowText(hDateStart, buf);
                sprintf(buf, "%04d/%02d/%02d", y, m, days_in_month(y, m));
                SetWindowText(hDateEnd, buf);
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Entry Point ---
extern int _argc;
extern char **_argv;

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc;
    MSG msg;
    int i;
    
    load_ini();

    if (_argc > 1) {
        const char* in_csv = "calendar.csv";
        const char* out_pdf = "calendar.pdf";
        int mode = 5; 
        int first_day = 0;
        int filter_node = -1;
        float margH_pct = 4.0f, margV_pct = 4.0f;
        int sY=2026, sM=1, sD=1, eY=2026, eM=1, eD=31;
        
        if (_argc >= 3) { in_csv = _argv[1]; out_pdf = _argv[2]; }
        
        for (i = 3; i < _argc; i++) {
            if (strncmp(_argv[i], "-mode=", 6) == 0) {
                mode = atoi(_argv[i] + 6);
            } else if (strncmp(_argv[i], "-node=", 6) == 0) {
                filter_node = atoi(_argv[i] + 6);
            } else if (strncmp(_argv[i], "-start=", 7) == 0) {
                sscanf(_argv[i] + 7, "%4d%2d%2d", &sY, &sM, &sD);
            } else if (strncmp(_argv[i], "-end=", 5) == 0) {
                sscanf(_argv[i] + 5, "%4d%2d%2d", &eY, &eM, &eD);
            }
        }
        
        if (parse_csv(in_csv)) {
            generate_pdf(out_pdf, mode, first_day, filter_node, margH_pct, margV_pct, sY, sM, sD, eY, eM, eD);
        }
        return 0;
    }

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "CSV_PDF_CLASS";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow("CSV_PDF_CLASS", "CSV to Calendar PDF", 
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 440, 290, NULL, NULL, hInstance, NULL);

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    while(GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    for (i = 0; i < event_count; i++) {
        if (events[i]) free(events[i]);
    }
    return msg.wParam;
}
/* EOF */