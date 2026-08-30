/* ============================================================================
 * CSV SQL - Command Line Remote Client (Win16 Port)
 *
 * COMPILATION INSTRUCTIONS (Open Watcom C/C++):
 *   wcl -ml -bcl=windows -Os -s -fe=csvsqlc.exe csvsqlc16.c winsock.lib
 * ============================================================================ */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Capped well below 32,767 to prevent 16-bit signed integer overflow in Winsock */
#define QUERY_BUFFER_SIZE   4000
#define MAX_OUTPUT_SIZE     30000 

static const char telnet_magic[] = "\xA6";

/* ============================================================================
 * 6-BIT PACKING & RLE COMPRESSION ENGINE
 * ============================================================================ */
unsigned char CharTo6Bit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == ' ') return 62;
    return 63; 
}

char Bit6ToChar(unsigned char b) {
    if (b < 26) return 'A' + b;
    if (b < 52) return 'a' + (b - 26);
    if (b < 62) return '0' + (b - 52);
    if (b == 62) return ' ';
    return '\0'; 
}

long Pack6Bit(const char FAR *in, long in_len, char FAR *out, long out_max) {
    unsigned long bit_buf = 0; 
    int bit_len = 0; 
    long o = 0;
    long i;
    unsigned char c, b6;

    for(i = 0; i < in_len; i++) {
        c = (unsigned char)in[i];
        b6 = CharTo6Bit(c);
        
        bit_buf = (bit_buf << 6) | b6; 
        bit_len += 6;
        if (b6 == 63) { 
            bit_buf = (bit_buf << 8) | c; 
            bit_len += 8; 
        }
        while (bit_len >= 8) {
            if (o >= out_max) return -1;
            bit_len -= 8; 
            out[o++] = (char)((bit_buf >> bit_len) & 0xFF);
        }
    }
    if (bit_len > 0) {
        if (o >= out_max) return -1;
        out[o++] = (char)((bit_buf << (8 - bit_len)) & 0xFF);
    }
    return o;
}

long Unpack6Bit(const char FAR *in, long in_len, char FAR *out, long out_max) {
    unsigned long bit_buf = 0; 
    int bit_len = 0; 
    long i = 0, o = 0;
    unsigned char b6, raw;

    while (i < in_len || bit_len >= 6) {
        while (bit_len < 14 && i < in_len) { 
            bit_buf = (bit_buf << 8) | ((unsigned char)in[i++]); 
            bit_len += 8; 
        }
        if (bit_len < 6) break;
        
        b6 = (unsigned char)((bit_buf >> (bit_len - 6)) & 0x3F); 
        bit_len -= 6;
        
        if (b6 == 63) {
            if (bit_len < 8) {
                if (i < in_len) { 
                    bit_buf = (bit_buf << 8) | ((unsigned char)in[i++]); 
                    bit_len += 8; 
                } 
                else break;
            }
            raw = (unsigned char)((bit_buf >> (bit_len - 8)) & 0xFF); 
            bit_len -= 8;
            if (o < out_max) out[o++] = (char)raw;
            if (raw == '\0') break; 
        } else {
            if (o < out_max) out[o++] = Bit6ToChar(b6);
        }
    }
    if (o < out_max) out[o] = '\0';
    return o;
}

long CompressRLE(const char FAR *in, long in_len, char FAR *out, long out_max) {
    long i = 0, o = 0, j;
    int run;

    while (i < in_len && o < out_max - 4) {
        run = 1;
        while (i + run < in_len && in[i + run] == in[i] && run < 255) run++;
        if (run >= 3 || in[i] == '\x1B') {
            out[o++] = '\x1B'; 
            out[o++] = (char)run; 
            out[o++] = in[i];
            i += run;
        } else {
            for (j = 0; j < run; j++) out[o++] = in[i];
            i += run;
        }
    }
    return o;
}

long DecompressRLE(const char FAR *in, long in_len, char FAR *out, long out_max) {
    long i = 0, o = 0, j;
    unsigned char run;
    char val;

    while (i < in_len && o < out_max - 1) {
        if (in[i] == '\x1B') {
            if (i + 2 >= in_len) break;
            run = (unsigned char)in[i+1];
            val = in[i+2];
            for (j = 0; j < run && o < out_max - 1; j++) out[o++] = val;
            i += 3;
        } else { 
            out[o++] = in[i++]; 
        }
    }
    out[o] = '\0';
    return o;
}

/* ============================================================================
 * MAIN CLIENT ROUTINE
 * ============================================================================ */
int main(int argc, char** argv) {
    /* Scoped Variables */
    HWND hwnd, parent;
    char target[256];
    char query[2048];
    char password[128];
    int userid = 1;
    int i, port = 23;
    char ip[256];
    char FAR *colon;
    WSADATA wsa;
    SOCKET client_sock;
    struct sockaddr_in addr;
    struct hostent FAR *he;
    int timeout = 10000;
    
    char FAR *temp = NULL;
    unsigned long sum = 0; /* Forced to 32-bit to match modern server checksums */
    char FAR *p;
    char FAR *uncomp = NULL;
    char FAR *packed_buf = NULL;
    char FAR *comp_buf = NULL;
    char FAR *send_buf = NULL;
    char FAR *recv_buf = NULL;
    char FAR *unpacked_rle = NULL;
    char FAR *decomp_buf = NULL;
    
    long packed_size, comp_size, unpacked_rle_size, decomp_size;
    int recv_len, rle_len; /* Signed 16-bit limits */
    int magic_len;
    char FAR *rle_payload;

    /* Force Watcom's text window to render so we can grab and maximize it */
    printf("Starting 16-Bit CSV SQL Client...\n");
    fflush(stdout);
    
    hwnd = GetActiveWindow();
    if (hwnd) {
        parent = GetParent(hwnd);
        ShowWindow(parent ? parent : hwnd, SW_SHOWMAXIMIZED);
    }

    strcpy(target, "127.0.0.1:23");
    strcpy(password, "admin");
    query[0] = '\0';

    if (argc < 2) {
        printf("Usage: csvsqlc16.exe <ip:port> -q:\"<query>\" [-n:<userid>] [-p:<password>]\n");
        printf("Example: csvsqlc16.exe 127.0.0.1:23 -q:\"SELECT * FROM table;\" -n:1\n");
        return 1;
    }

    /* Parse Arguments */
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-q:", 3) == 0) {
            strncpy(query, argv[i] + 3, sizeof(query) - 1);
            query[sizeof(query) - 1] = '\0';
        } else if (strncmp(argv[i], "-n:", 3) == 0) {
            userid = atoi(argv[i] + 3);
        } else if (strncmp(argv[i], "-p:", 3) == 0) {
            strncpy(password, argv[i] + 3, sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
        } else {
            strncpy(target, argv[i], sizeof(target) - 1);
            target[sizeof(target) - 1] = '\0';
        }
    }

    if (strlen(query) == 0) {
        printf("Error: No query provided. Use -q:\"query\"\n");
        return 1;
    }

    strncpy(ip, target, sizeof(ip));
    ip[sizeof(ip) - 1] = '\0';
    colon = strchr(ip, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    /* Heap Allocation */
    temp = (char FAR *)malloc(QUERY_BUFFER_SIZE);
    uncomp = (char FAR *)malloc(QUERY_BUFFER_SIZE * 2);
    packed_buf = (char FAR *)malloc(QUERY_BUFFER_SIZE * 2);
    comp_buf = (char FAR *)malloc(QUERY_BUFFER_SIZE * 2);
    send_buf = (char FAR *)malloc(QUERY_BUFFER_SIZE * 2 + 256);
    recv_buf = (char FAR *)malloc(MAX_OUTPUT_SIZE);
    unpacked_rle = (char FAR *)malloc(MAX_OUTPUT_SIZE);
    decomp_buf = (char FAR *)malloc(MAX_OUTPUT_SIZE);

    if (!temp || !uncomp || !packed_buf || !comp_buf || !send_buf || !recv_buf || !unpacked_rle || !decomp_buf) {
        printf("Error: Memory allocation failed. Ensure sufficient heap limits.\n");
        goto cleanup;
    }

    /* Initialize Winsock 1.1 */
    if (WSAStartup(0x0101, &wsa) != 0) {
        printf("Error: Failed to initialize Winsock.\n");
        goto cleanup;
    }

    client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        he = gethostbyname(ip);
        if (he) addr.sin_addr.s_addr = *(unsigned long FAR*)he->h_addr_list[0];
    }

    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char FAR *)&timeout, sizeof(timeout));

    printf("Connecting to %s:%d (Node: %d)...\n", ip, port, userid);
    if (connect(client_sock, (struct sockaddr FAR *)&addr, sizeof(addr)) != 0) {
        printf("Error: Could not connect to %s:%d\n", ip, port);
        closesocket(client_sock);
        WSACleanup();
        goto cleanup;
    }

    /* Build Payload & Checksum */
    sprintf(temp, "%d\xA6%s\xA6%s", userid, password, query);
    
    for (p = temp; *p; p++) sum += (unsigned char)*p;
    
    /* Ensure %lu is used for 32-bit unsigned long interpolation */
    sprintf(uncomp, "%s\xA6%lu", temp, sum);
    
    /* Pack & Compress */
    packed_size = Pack6Bit(uncomp, strlen(uncomp) + 1, packed_buf, QUERY_BUFFER_SIZE * 2);
    comp_size = CompressRLE(packed_buf, packed_size, comp_buf, QUERY_BUFFER_SIZE * 2);
    
    if (comp_size <= 0) {
        printf("Error: Failed to compress payload.\n");
        closesocket(client_sock); 
        WSACleanup(); 
        goto cleanup;
    }

    /* Send Payload */
    magic_len = strlen(telnet_magic); 
    memcpy(send_buf, telnet_magic, magic_len); 
    memcpy(send_buf + magic_len, comp_buf, (size_t)comp_size);
    
    if (send(client_sock, send_buf, magic_len + (int)comp_size, 0) <= 0) {
        printf("Error: Connection lost while sending.\n");
        closesocket(client_sock); 
        WSACleanup(); 
        goto cleanup;
    }
    
    /* Receive Response (MAX_OUTPUT_SIZE strictly < 32767) */
    recv_len = recv(client_sock, recv_buf, MAX_OUTPUT_SIZE - 1, 0);
    
    if (recv_len > 0) {
        if (recv_len >= magic_len && strncmp(recv_buf, telnet_magic, magic_len) == 0) {
            rle_payload = recv_buf + magic_len; 
            rle_len = recv_len - magic_len;
            
            unpacked_rle_size = DecompressRLE(rle_payload, rle_len, unpacked_rle, MAX_OUTPUT_SIZE);
            decomp_size = Unpack6Bit(unpacked_rle, unpacked_rle_size, decomp_buf, MAX_OUTPUT_SIZE);
            
            if (decomp_size > 0) {
                printf("\n%s\n", decomp_buf);
            } else {
                printf("Error: Failed to decompress response.\n");
            }
        } else {
            printf("Error: Invalid response magic string from server.\n");
        }
    } else {
        printf("Error: No response or connection dropped during receive.\n");
    }
    
    closesocket(client_sock);
    WSACleanup();

cleanup:
    if (temp) free(temp);
    if (uncomp) free(uncomp);
    if (packed_buf) free(packed_buf);
    if (comp_buf) free(comp_buf);
    if (send_buf) free(send_buf);
    if (recv_buf) free(recv_buf);
    if (unpacked_rle) free(unpacked_rle);
    if (decomp_buf) free(decomp_buf);

    return 0;
}