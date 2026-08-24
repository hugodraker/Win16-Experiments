/* ============================================================================
 * CSV HTTP Server GUI - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *   Using single-step WCL (Recommended):
 *     wcl -ml -za99 -bt=windows -l=windows -k16k -zq -os -s csvhttp.c winsock.lib
 *
 *   Using two-step WCC / WLINK:
 *     wcc -ml -za99 -bt=windows -zq -os -s csvhttp.c
 *     wlink system windows option quiet option packcode option stack=16k name csvhttp.exe file csvhttp.obj library windows.lib library winsock.lib
 *
 * REQUIREMENTS: Windows 3.1x (Win16)
 * DEPENDENCIES: USER, GDI, WINSOCK
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef MAX_PATH
#define MAX_PATH 128
#endif

#ifndef MAKEWORD
#define MAKEWORD(low, high) ((WORD)(((BYTE)(low)) | (((WORD)((BYTE)(high))) << 8)))
#endif

#ifndef WSAGETSELECTERROR
#define WSAGETSELECTERROR(lParam) HIWORD(lParam)
#endif

#ifndef WSAGETSELECTEVENT
#define WSAGETSELECTEVENT(lParam) LOWORD(lParam)
#endif

#define WM_SOCKET_NOTIFY (WM_USER + 1)
#define ID_EDIT_LOG 101

#define SERVER_PORT 8080
#define BUFFER_SIZE 4096
#define MAX_FIELD_LEN 256

typedef enum {
    ENCODING_UNKNOWN,
    ENCODING_UTF8,
    ENCODING_UTF8_BOM,
    ENCODING_UTF16_LE,
    ENCODING_UTF16_BE,
    ENCODING_ASCII
} Encoding;

static char FAR* csv_data = NULL;
static long csv_data_len = 0;
static int name_col_idx = -1;
static int id_col_idx = 0;
static int birthdate_col_idx = -1;
static int sex_col_idx = -1;

HWND hMain = NULL;
HWND hEditOut = NULL;
HFONT hFixedFont = NULL;
HFONT hGuiFont = NULL;
SOCKET server_socket = INVALID_SOCKET;

void AppendLog(const char* text) {
    int len;
    if (!hEditOut) return;
    
    len = GetWindowTextLength(hEditOut);
    if (len > 30000) {
        SetWindowText(hEditOut, "");
        len = 0;
    }
    SendMessage(hEditOut, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEditOut, EM_REPLACESEL, 0, (LPARAM)(LPSTR)text);
    SendMessage(hEditOut, EM_SETSEL, (WPARAM)32767, (LPARAM)32767);
}

void url_decode(const char* src, char* dest, int max_len) {
    int out = 0;
    int i;
    for (i = 0; src[i] && out < max_len - 1; i++) {
        if (src[i] == '+') {
            dest[out++] = ' ';
        } else if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3];
            hex[0] = src[i+1];
            hex[1] = src[i+2];
            hex[2] = '\0';
            dest[out++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dest[out++] = src[i];
        }
    }
    dest[out] = '\0';
}

Encoding detect_encoding(const unsigned char* buf, long len) {
    long i;
    if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) return ENCODING_UTF8_BOM;
    if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) return ENCODING_UTF16_LE;
    if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) return ENCODING_UTF16_BE;
    for (i = 0; i < (len > 1024 ? 1024 : len); i++) {
        if (buf[i] > 127) return ENCODING_UTF8;
    }
    return ENCODING_ASCII;
}

int utf16le_to_utf8(const wchar_t* input, char* output, int max_output) {
    int out_pos = 0, in_idx = 0;
    while (input[in_idx] != L'\0' && out_pos < max_output - 4) {
        wchar_t ch = input[in_idx++];
        if (ch < 0x80) output[out_pos++] = (char)ch;
        else if (ch < 0x800) {
            output[out_pos++] = (char)(0xC0 | (ch >> 6));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        } else {
            output[out_pos++] = (char)(0xE0 | (ch >> 12));
            output[out_pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    output[out_pos] = '\0';
    return out_pos;
}

int utf16be_to_utf8(const unsigned char* input, char* output, int max_output) {
    int out_pos = 0, in_idx = 0;
    while (in_idx + 1 < max_output * 2) {
        unsigned char hi = input[in_idx++];
        unsigned char lo = input[in_idx++];
        wchar_t ch = (wchar_t)((hi << 8) | lo);
        if (ch == 0) break;
        if (ch < 0x80) output[out_pos++] = (char)ch;
        else if (ch < 0x800) {
            output[out_pos++] = (char)(0xC0 | (ch >> 6));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        } else {
            output[out_pos++] = (char)(0xE0 | (ch >> 12));
            output[out_pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            output[out_pos++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    output[out_pos] = '\0';
    return out_pos;
}

void trim_whitespace(char* str) {
    char* start = str;
    int len;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != str) memmove(str, start, lstrlen(start) + 1);
    len = (int)lstrlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\r' || str[len-1] == '\n')) {
        str[--len] = '\0';
    }
}

int str_iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

int str_icontains(const char* haystack, const char* needle) {
    int i;
    if (!*needle) return 1;
    for (i = 0; haystack[i] != '\0'; i++) {
        int j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' && 
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (needle[j] == '\0') return 1;
    }
    return 0;
}

int get_csv_field(const char* line, int target_col, char* out_buf, int max_len) {
    int current_col = 0, in_quotes = 0, pos = 0, out_pos = 0;
    while (line[pos] != '\0' && current_col <= target_col) {
        char c = line[pos];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            if (current_col == target_col) break;
            current_col++;
        } else {
            if (current_col == target_col && out_pos < max_len - 1) {
                out_buf[out_pos++] = c;
            }
        }
        pos++;
    }
    out_buf[out_pos] = '\0';
    trim_whitespace(out_buf);
    return (current_col == target_col);
}

char FAR* load_and_convert_csv(const char* filename, long* data_len) {
    FILE* fp = fopen(filename, "rb");
    long file_size;
    unsigned char* raw_buf;
    size_t bytes_read;
    Encoding enc;
    char FAR* converted;

    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);
    
    raw_buf = (unsigned char*)malloc((size_t)file_size + 4);
    if (!raw_buf) { fclose(fp); return NULL; }
    bytes_read = fread(raw_buf, 1, (size_t)file_size, fp);
    fclose(fp);
    if (bytes_read == 0) { free(raw_buf); return NULL; }
    
    raw_buf[bytes_read] = '\0';
    enc = detect_encoding(raw_buf, (long)bytes_read);
    converted = (char FAR*)malloc((size_t)bytes_read + 1);
    if (!converted) { free(raw_buf); return NULL; }
    
    switch (enc) {
        case ENCODING_UTF8_BOM:
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = (long)bytes_read - 3;
            memmove(converted, converted + 3, bytes_read - 2);
            break;
        case ENCODING_UTF8:
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = (long)bytes_read;
            break;
        case ENCODING_UTF16_LE:
            *data_len = (long)utf16le_to_utf8((wchar_t*)(raw_buf + 2), converted, (int)bytes_read);
            break;
        case ENCODING_UTF16_BE:
            *data_len = (long)utf16be_to_utf8(raw_buf + 2, converted, (int)bytes_read);
            break;
        default:
            memcpy(converted, raw_buf, bytes_read + 1);
            *data_len = (long)bytes_read;
            break;
    }
    free(raw_buf);
    return converted;
}

long get_line_end(const char FAR* data, long start, long len) {
    long pos = start;
    while (pos < len && data[pos] != '\r' && data[pos] != '\n') pos++;
    return pos;
}

void skip_newline(const char FAR* data, long* pos, long len) {
    while (*pos < len && (data[*pos] == '\r' || data[*pos] == '\n')) (*pos)++;
}

int initialize_csv_parser(void) {
    if (!csv_data) {
        long h_end;
        char header[512];
        long h_len;
        int col_idx;
        char field_buf[MAX_FIELD_LEN];

        csv_data = load_and_convert_csv("patients.csv", &csv_data_len);
        if (!csv_data || csv_data_len == 0) {
            AppendLog("[ERROR] Cannot load patients.csv\r\n");
            return -1;
        }
        h_end = get_line_end(csv_data, 0, csv_data_len);
        h_len = (h_end > 511) ? 511 : h_end;
        _fmemcpy(header, csv_data, (size_t)h_len);
        header[h_len] = '\0';
        trim_whitespace(header);
        
        col_idx = 0;
        while (get_csv_field(header, col_idx, field_buf, sizeof(field_buf))) {
            if (str_iequals(field_buf, "Name")) name_col_idx = col_idx;
            else if (str_iequals(field_buf, "Birthdate") || str_iequals(field_buf, "DOB")) birthdate_col_idx = col_idx;
            else if (str_iequals(field_buf, "Sex") || str_iequals(field_buf, "Gender")) sex_col_idx = col_idx;
            col_idx++;
        }
        
        if (name_col_idx < 0) return -1;
    }
    return 0;
}

int get_patient_names(const char* filter, char FAR* output, int max_output, int max_count) {
    long data_start;
    int count = 0;
    long line_start;

    if (initialize_csv_parser() != 0) {
        lstrcpy(output, "{\"error\": \"Failed to load CSV\"}");
        return -1;
    }
    
    output[0] = '\0';
    data_start = get_line_end(csv_data, 0, csv_data_len);
    skip_newline(csv_data, &data_start, csv_data_len);
    
    lstrcat(output, "[");
    line_start = data_start;
    
    while (line_start < csv_data_len && (max_count == 0 || count < max_count)) {
        long line_end = get_line_end(csv_data, line_start, csv_data_len);
        if (line_end <= line_start) {
            line_start = line_end + 1;
            skip_newline(csv_data, &line_start, csv_data_len);
            continue;
        }
        
        {
            long len = line_end - line_start;
            char line[512];
            char name_buf[MAX_FIELD_LEN];

            if (len > 511) len = 511;
            
            _fmemcpy(line, &csv_data[line_start], (size_t)len);
            line[len] = '\0';
            trim_whitespace(line);
            
            if (get_csv_field(line, name_col_idx, name_buf, sizeof(name_buf))) {
                if (filter && lstrlen(filter) > 0) {
                    if (!str_icontains(name_buf, filter)) {
                        line_start = line_end;
                        skip_newline(csv_data, &line_start, csv_data_len);
                        continue;
                    }
                }
                
                if (count > 0) lstrcat(output, ",");
                
                {
                    char escaped_name[MAX_FIELD_LEN * 2];
                    int ei = 0;
                    int ni;
                    char formatted_name[MAX_FIELD_LEN * 2 + 4];

                    for (ni = 0; name_buf[ni] && ei < sizeof(escaped_name) - 1; ni++) {
                        if (name_buf[ni] == '"' || name_buf[ni] == '\\') escaped_name[ei++] = '\\';
                        escaped_name[ei++] = name_buf[ni];
                    }
                    escaped_name[ei] = '\0';
                    
                    sprintf(formatted_name, "\"%s\"", (LPSTR)escaped_name);
                    lstrcat(output, formatted_name);
                    count++;
                }
            }
        }
        line_start = line_end;
        skip_newline(csv_data, &line_start, csv_data_len);
    }
    
    lstrcat(output, "]");
    return count;
}

int get_patient_details(const char* patient_name, char FAR* output, int max_output) {
    long data_start;
    long line_start;

    if (initialize_csv_parser() != 0) return -1;
    
    data_start = get_line_end(csv_data, 0, csv_data_len);
    skip_newline(csv_data, &data_start, csv_data_len);
    
    line_start = data_start;
    while (line_start < csv_data_len) {
        long line_end = get_line_end(csv_data, line_start, csv_data_len);
        if (line_end <= line_start) {
            line_start = line_end + 1;
            skip_newline(csv_data, &line_start, csv_data_len);
            continue;
        }
        
        {
            long len = line_end - line_start;
            char line[512];
            char name_buf[MAX_FIELD_LEN];

            if (len > 511) len = 511;
            
            _fmemcpy(line, &csv_data[line_start], (size_t)len);
            line[len] = '\0';
            trim_whitespace(line);
            
            if (get_csv_field(line, name_col_idx, name_buf, sizeof(name_buf))) {
                if (str_iequals(name_buf, patient_name)) {
                    char id_buf[MAX_FIELD_LEN] = {0};
                    char birthdate_buf[MAX_FIELD_LEN] = {0};
                    char sex_buf[MAX_FIELD_LEN] = {0};

                    get_csv_field(line, id_col_idx, id_buf, sizeof(id_buf));
                    if (birthdate_col_idx >= 0) get_csv_field(line, birthdate_col_idx, birthdate_buf, sizeof(birthdate_buf));
                    if (sex_col_idx >= 0) get_csv_field(line, sex_col_idx, sex_buf, sizeof(sex_buf));
                    
                    sprintf(output, "{\"patient_id\":\"%s\",\"name\":\"%s\",\"birthdate\":\"%s\",\"sex\":\"%s\"}", 
                            (LPSTR)id_buf, (LPSTR)name_buf, (LPSTR)birthdate_buf, (LPSTR)sex_buf);
                    return 0;
                }
            }
        }
        line_start = line_end;
        skip_newline(csv_data, &line_start, csv_data_len);
    }
    lstrcpy(output, "{\"error\": \"Patient not found\"}");
    return -1;
}

int send_http_response(SOCKET client, const char* status, const char* content_type, const char FAR* body, int body_len) {
    char response_header[512];
    int header_len = sprintf(response_header,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);
    
    send(client, response_header, header_len, 0);
    send(client, (const char FAR*)body, body_len, 0);
    return 0;
}

void get_query_param(const char* url, const char* param_name, char* value, int max_value) {
    const char* start = strstr(url, param_name);
    char raw_val[128];
    int i = 0;

    value[0] = '\0';
    if (!start) return;
    start += strlen(param_name);
    if (*start != '=') return;
    start++;
    
    memset(raw_val, 0, sizeof(raw_val));
    while (*start && *start != '&' && *start != '#' && i < sizeof(raw_val) - 1) {
        raw_val[i++] = *start++;
    }
    raw_val[i] = '\0';
    url_decode(raw_val, value, max_value);
}

void route_request(SOCKET client, const char* request_line) {
    char method[16] = {0};
    char path[256] = {0};
    char query[128] = {0};
    char* qmark;
    char logMsg[300];

    sscanf(request_line, "%15s %255s", method, path);
    
    qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
        sprintf(query, "%s", qmark + 1);
    }
    
    sprintf(logMsg, "[REQUEST] %s %s\r\n", (LPSTR)method, (LPSTR)path);
    AppendLog(logMsg);
    
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        const char* html_content = 
            "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>Patient Directory</title>"
            "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f5f5f5}.container{max-width:600px;margin:0 auto;background:#fff;padding:20px;border-radius:8px}"
            "input[type='text']{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
            "#patientList{width:100%;height:180px;margin-top:10px;padding:5px;border:1px solid #ddd}"
            ".fields{margin-top:20px;padding:15px;background:#f9f9f9;border-radius:4px}.field-row{margin-bottom:10px}label{display:inline-block;width:100px;font-weight:bold}"
            "input.field-value{width:calc(100% - 105px);padding:8px;border:1px solid #ccc;border-radius:4px}</style></head>"
            "<body><div class='container'><div>"
            "<input type='text' id='filterInput' placeholder='Type to filter patients...'>"
            "<select id='patientList' size='10' onchange='loadPatientDetails(this.value)' onclick='loadPatientDetails(this.value)'></select></div>"
            "<div class='fields'>"
            "<div class='field-row'><label>Name:</label><input id='displayName' class='field-value' type='text'></div>"
            "<div class='field-row'><label>Patient ID:</label><input id='displayId' class='field-value' type='text'></div>"
            "<div class='field-row'><label>Birthdate:</label><input id='displayBirthdate' class='field-value' type='text'></div>"
            "<div class='field-row'><label>Sex:</label><input id='displaySex' class='field-value' type='text'></div>"
            "</div></div>"
            "<script>"
            "async function filterPatients() {"
            "  const filter = document.getElementById('filterInput').value.trim();"
            "  const res = await fetch('/api/patients?filter=' + encodeURIComponent(filter));"
            "  const names = await res.json();"
            "  const select = document.getElementById('patientList');"
            "  select.options.length = 0;"
            "  names.forEach(name => { const opt = document.createElement('option'); opt.value = name; opt.textContent = name; select.appendChild(opt); });"
            "}"
            "async function loadPatientDetails(name) {"
            "  if (!name) return;"
            "  const res = await fetch('/api/patient/' + encodeURIComponent(name));"
            "  const data = await res.json();"
            "  if (!data.error) {"
            "    document.getElementById('displayName').value = data.name || '';"
            "    document.getElementById('displayId').value = data.patient_id || '';"
            "    document.getElementById('displayBirthdate').value = data.birthdate || '';"
            "    document.getElementById('displaySex').value = data.sex || '';"
            "  }"
            "}"
            "document.getElementById('filterInput').addEventListener('input', filterPatients);"
            "window.onload = filterPatients;"
            "</script></body></html>";
        send_http_response(client, "200 OK", "text/html", html_content, (int)strlen(html_content));
    }
    else if (strncmp(path, "/api/patient/", 13) == 0) {
        char* raw_name = path + 13;
        char decoded_name[256] = {0};
        char FAR* json_response = (char FAR*)malloc(BUFFER_SIZE);
        int result;

        url_decode(raw_name, decoded_name, sizeof(decoded_name));
        if (!json_response) return;
        
        result = get_patient_details(decoded_name, json_response, BUFFER_SIZE);
        if (result == 0) {
            send_http_response(client, "200 OK", "application/json", json_response, (int)lstrlen(json_response));
        } else {
            send_http_response(client, "404 Not Found", "application/json", json_response, (int)lstrlen(json_response));
        }
        free(json_response);
    }
    else if (strncmp(path, "/api/patients", 13) == 0) {
        char filter[128] = {0};
        char FAR* json_response = (char FAR*)malloc(BUFFER_SIZE);
        int max_results;
        int count;

        get_query_param(query, "filter", filter, sizeof(filter));
        if (!json_response) return;
        
        max_results = (lstrlen(filter) > 0) ? 50 : 10;
        count = get_patient_names(filter, json_response, BUFFER_SIZE, max_results);
        
        if (count >= 0) {
            send_http_response(client, "200 OK", "application/json", json_response, (int)lstrlen(json_response));
        } else {
            send_http_response(client, "500 Internal Server Error", "application/json", 
                "{\"error\": \"Server error\"}", 22);
        }
        free(json_response);
    }
    else {
        send_http_response(client, "404 Not Found", "text/plain", "Not Found", 9);
    }
}

LRESULT CALLBACK __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            hFixedFont = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    FIXED_PITCH | FF_MODERN, "Courier");

            hEditOut = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
                                    10, 10, 605, 340, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);

            if (hFixedFont) {
                SendMessage(hEditOut, WM_SETFONT, (WPARAM)hFixedFont, TRUE);
            }

            // Initialize CSV Parser
            if (initialize_csv_parser() != 0) {
                AppendLog("[ERROR] Failed to initialize CSV parser on startup.\r\n");
            } else {
                char msgBuf[128];
                WSADATA wsaData;
                struct sockaddr_in server_addr;

                AppendLog("CSV Parser initialized successfully.\r\n");

                if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
                    AppendLog("[ERROR] WSAStartup failed.\r\n");
                    break;
                }

                server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (server_socket == INVALID_SOCKET) {
                    AppendLog("[ERROR] Failed to create server socket.\r\n");
                    WSACleanup();
                    break;
                }

                server_addr.sin_family = AF_INET;
                server_addr.sin_addr.s_addr = INADDR_ANY;
                server_addr.sin_port = htons(SERVER_PORT);

                if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
                    AppendLog("[ERROR] Failed to bind port 8080.\r\n");
                    closesocket(server_socket);
                    server_socket = INVALID_SOCKET;
                    WSACleanup();
                    break;
                }

                if (listen(server_socket, 5) == SOCKET_ERROR) {
                    AppendLog("[ERROR] Failed to listen on socket.\r\n");
                    closesocket(server_socket);
                    server_socket = INVALID_SOCKET;
                    WSACleanup();
                    break;
                }

                if (WSAAsyncSelect(server_socket, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT) == SOCKET_ERROR) {
                    AppendLog("[ERROR] WSAAsyncSelect failed for server socket.\r\n");
                    closesocket(server_socket);
                    server_socket = INVALID_SOCKET;
                    WSACleanup();
                    break;
                }

                sprintf(msgBuf, "HTTP Server started on port %d.\r\nOpen http://localhost:%d in browser.\r\n", SERVER_PORT, SERVER_PORT);
                AppendLog(msgBuf);
            }
            break;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 20 && h > 20) {
                MoveWindow(hEditOut, 10, 10, w - 20, h - 20, TRUE);
            }
            break;
        }
        case WM_SOCKET_NOTIFY: {
            SOCKET s = (SOCKET)wParam;
            if (WSAGETSELECTERROR(lParam)) {
                break;
            }

            if (s == server_socket) {
                if (WSAGETSELECTEVENT(lParam) == FD_ACCEPT) {
                    struct sockaddr_in client_addr;
                    int addr_len = sizeof(client_addr);
                    SOCKET client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
                    if (client_fd != INVALID_SOCKET) {
                        WSAAsyncSelect(client_fd, hwnd, WM_SOCKET_NOTIFY, FD_READ | FD_CLOSE);
                    }
                }
            } else {
                int event = WSAGETSELECTEVENT(lParam);
                if (event == FD_READ) {
                    char FAR* request_buffer = (char FAR*)malloc(BUFFER_SIZE);
                    if (request_buffer) {
                        int received = recv(s, request_buffer, BUFFER_SIZE - 1, 0);
                        if (received > 0) {
                            request_buffer[received] = '\0';
                            {
                                char request_line[256];
                                sscanf(request_buffer, "%255[^\r\n]", request_line);
                                route_request(s, request_line);
                            }
                        }
                        free(request_buffer);
                    }
                    shutdown(s, 1);
                    closesocket(s);
                } else if (event == FD_CLOSE) {
                    closesocket(s);
                }
            }
            break;
        }
        case WM_DESTROY:
            if (server_socket != INVALID_SOCKET) {
                closesocket(server_socket);
                WSACleanup();
            }
            if (hFixedFont) DeleteObject(hFixedFont);
            if (hGuiFont) DeleteObject(hGuiFont);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "Win16CsvServerClass";
    WNDCLASS wc;
    MSG msg;

    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hMain = CreateWindow(CLASS_NAME, "CSV HTTP Server (Win16)", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
                         NULL, NULL, hInstance, NULL);

    if (!hMain) return 0;

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    memset(&msg, 0, sizeof(MSG));
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
/* EOF */