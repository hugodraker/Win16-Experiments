/* ============================================================================
 * Volume Control - Win16 OpenWatcom Implementation 
 *
 * COMPILATION INSTRUCTIONS (OpenWatcom):
 *
 *   Using two-step WCC / WLINK:
 *     wcl -q -l=windows -bt=windows -fe=sndvol.exe sndvol.c
 *
 *
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <mmsystem.h>
#include <string.h>

#pragma library("mmsystem.lib")

#ifndef DEFAULT_GUI_FONT
#define DEFAULT_GUI_FONT 17
#endif

#define ID_SLIDER  101
#define ID_MUTE    102

#define WIN_WIDTH  80
#define WIN_HEIGHT 140

#define CM_SETPOS  (WM_USER + 1)
#define CM_GETPOS  (WM_USER + 2)
#define WM_SHOWVOL (WM_USER + 101)

/* Mixer API Constants */
#define MIXERLINE_COMPONENTTYPE_DST_FIRST    0x00000000L
#define MIXERLINE_COMPONENTTYPE_DST_SPEAKERS (MIXERLINE_COMPONENTTYPE_DST_FIRST + 4)
#define MIXERLINE_COMPONENTTYPE_SRC_FIRST    0x00001000L
#define MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT  (MIXERLINE_COMPONENTTYPE_SRC_FIRST + 3)

#define MIXER_GETLINEINFOF_COMPONENTTYPE     0x00000003L
#define MIXER_GETLINECONTROLSF_ONEBYTYPE     0x00000002L
#define MIXER_GETCONTROLDETAILSF_VALUE       0x00000000L
#define MIXER_SETCONTROLDETAILSF_VALUE       0x00000000L

#define MIXERCONTROL_CT_CLASS_FADER          0x50000000L
#define MIXERCONTROL_CT_UNITS_UNSIGNED       0x00030000L
#define MIXERCONTROL_CONTROLTYPE_VOLUME      (MIXERCONTROL_CT_CLASS_FADER | MIXERCONTROL_CT_UNITS_UNSIGNED | 0x0001)

#define MIXERCONTROL_CT_CLASS_SWITCH         0x20000000L
#define MIXERCONTROL_CT_UNITS_BOOLEAN        0x00010000L
#define MIXERCONTROL_CONTROLTYPE_BOOLEAN     (MIXERCONTROL_CT_CLASS_SWITCH | MIXERCONTROL_CT_UNITS_BOOLEAN)
#define MIXERCONTROL_CONTROLTYPE_MUTE        (MIXERCONTROL_CONTROLTYPE_BOOLEAN + 2)

#pragma pack(1)
typedef struct {
    DWORD cbStruct;
    DWORD dwDestination;
    DWORD dwSource;
    DWORD dwLineID;
    DWORD fdwLine;
    DWORD dwUser;
    DWORD dwComponentType;
    DWORD cChannels;
    DWORD cConnections;
    DWORD cControls;
    char  szShortName[16];
    char  szName[64];
    struct {
        DWORD dwType;
        DWORD dwDeviceID;
        WORD  wMid;
        WORD  wPid;
        WORD  vDriverVersion;
        char  szPname[32];
    } Target;
} WIN16_MIXERLINE;

typedef struct {
    DWORD cbStruct;
    DWORD dwControlID;
    DWORD dwControlType;
    DWORD fdwControl;
    DWORD cMultipleItems;
    char  szShortName[16];
    char  szName[64];
    DWORD Bounds[6];
    DWORD Metrics[6];
} WIN16_MIXERCONTROL;

typedef struct {
    DWORD cbStruct;
    DWORD dwLineID;
    DWORD dwControlID;
    DWORD cControls;
    DWORD cbmxctrl;
    void FAR *pamxctrl;
} WIN16_MIXERLINECONTROLS;

typedef struct {
    DWORD cbStruct;
    DWORD dwControlID;
    DWORD cChannels;
    HWND  hwndOwner;
    DWORD cbDetails;
    void FAR *paDetails;
} WIN16_MIXERCONTROLDETAILS;
#pragma pack()

typedef UINT (WINAPI *LPFN_GETLINEINFO)(UINT, WIN16_MIXERLINE FAR*, DWORD);
typedef UINT (WINAPI *LPFN_GETLINECONTROLS)(UINT, WIN16_MIXERLINECONTROLS FAR*, DWORD);
typedef UINT (WINAPI *LPFN_GETCONTROLDETAILS)(UINT, WIN16_MIXERCONTROLDETAILS FAR*, DWORD);
typedef UINT (WINAPI *LPFN_SETCONTROLDETAILS)(UINT, WIN16_MIXERCONTROLDETAILS FAR*, DWORD);

LPFN_GETLINEINFO       fnMixerGetLineInfo = NULL;
LPFN_GETLINECONTROLS   fnMixerGetLineControls = NULL;
LPFN_GETCONTROLDETAILS fnMixerGetControlDetails = NULL;
LPFN_SETCONTROLDETAILS fnMixerSetControlDetails = NULL;

HWND hSlider, hMute, hLabel;
HBRUSH g_hbrBtnFace;
BOOL g_bMuted = FALSE;
DWORD g_dwSavedVol = 0x80008000L;

DWORD g_dwVolCtrlID = (DWORD)-1;
DWORD g_dwMuteCtrlID = (DWORD)-1;

void InitMixer(void) {
    HINSTANCE hMM = GetModuleHandle("MMSYSTEM");
    if (!hMM) return;

    fnMixerGetLineInfo       = (LPFN_GETLINEINFO)GetProcAddress(hMM, "MIXERGETLINEINFO");
    fnMixerGetLineControls   = (LPFN_GETLINECONTROLS)GetProcAddress(hMM, "MIXERGETLINECONTROLS");
    fnMixerGetControlDetails = (LPFN_GETCONTROLDETAILS)GetProcAddress(hMM, "MIXERGETCONTROLDETAILS");
    fnMixerSetControlDetails = (LPFN_SETCONTROLDETAILS)GetProcAddress(hMM, "MIXERSETCONTROLDETAILS");

    if (fnMixerGetLineInfo && fnMixerGetLineControls && 
        fnMixerGetControlDetails && fnMixerSetControlDetails) {
        
        WIN16_MIXERLINE ml;
        WIN16_MIXERLINECONTROLS mlc;
        WIN16_MIXERCONTROL mc;

        _fmemset(&ml, 0, sizeof(WIN16_MIXERLINE));
        ml.cbStruct = sizeof(WIN16_MIXERLINE);
        ml.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

        if (fnMixerGetLineInfo(0, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE) == 0) {
            _fmemset(&mlc, 0, sizeof(WIN16_MIXERLINECONTROLS));
            mlc.cbStruct = sizeof(WIN16_MIXERLINECONTROLS);
            mlc.dwLineID = ml.dwLineID;
            mlc.cControls = 1;
            mlc.cbmxctrl = sizeof(WIN16_MIXERCONTROL);
            mlc.pamxctrl = &mc;

            mlc.dwControlID = MIXERCONTROL_CONTROLTYPE_VOLUME;
            if (fnMixerGetLineControls(0, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE) == 0) {
                g_dwVolCtrlID = mc.dwControlID;
            }

            mlc.dwControlID = MIXERCONTROL_CONTROLTYPE_MUTE;
            if (fnMixerGetLineControls(0, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE) == 0) {
                g_dwMuteCtrlID = mc.dwControlID;
            }
        }

        if (g_dwMuteCtrlID == (DWORD)-1) {
            _fmemset(&ml, 0, sizeof(WIN16_MIXERLINE));
            ml.cbStruct = sizeof(WIN16_MIXERLINE);
            ml.dwComponentType = MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT;

            if (fnMixerGetLineInfo(0, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE) == 0) {
                _fmemset(&mlc, 0, sizeof(WIN16_MIXERLINECONTROLS));
                mlc.cbStruct = sizeof(WIN16_MIXERLINECONTROLS);
                mlc.dwLineID = ml.dwLineID;
                mlc.cControls = 1;
                mlc.cbmxctrl = sizeof(WIN16_MIXERCONTROL);
                mlc.pamxctrl = &mc;

                mlc.dwControlID = MIXERCONTROL_CONTROLTYPE_MUTE;
                if (fnMixerGetLineControls(0, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE) == 0) {
                    g_dwMuteCtrlID = mc.dwControlID;
                }
            }
        }
    }
}

void SyncUIFromSystem(void) {
    int pos = 127;
    g_bMuted = FALSE;

    if (g_dwVolCtrlID != (DWORD)-1) {
        WIN16_MIXERCONTROLDETAILS mcd;
        DWORD dwVal = 0;
        _fmemset(&mcd, 0, sizeof(WIN16_MIXERCONTROLDETAILS));
        mcd.cbStruct = sizeof(WIN16_MIXERCONTROLDETAILS);
        mcd.dwControlID = g_dwVolCtrlID;
        mcd.cChannels = 1;
        mcd.cbDetails = sizeof(DWORD);
        mcd.paDetails = &dwVal;
        if (fnMixerGetControlDetails(0, &mcd, MIXER_GETCONTROLDETAILSF_VALUE) == 0) {
            pos = 255 - (int)(dwVal * 255L / 65535L);
        }
    } else {
        DWORD dwVol = 0;
        if (waveOutGetVolume(0, &dwVol) == 0) {
            WORD wLeft = LOWORD(dwVol);
            pos = 255 - (int)(wLeft * 255L / 65535L);
            g_dwSavedVol = dwVol;
        }
    }

    if (g_dwMuteCtrlID != (DWORD)-1) {
        WIN16_MIXERCONTROLDETAILS mcd;
        DWORD dwVal = 0;
        _fmemset(&mcd, 0, sizeof(WIN16_MIXERCONTROLDETAILS));
        mcd.cbStruct = sizeof(WIN16_MIXERCONTROLDETAILS);
        mcd.dwControlID = g_dwMuteCtrlID;
        mcd.cChannels = 1;
        mcd.cbDetails = sizeof(DWORD);
        mcd.paDetails = &dwVal;
        if (fnMixerGetControlDetails(0, &mcd, MIXER_GETCONTROLDETAILSF_VALUE) == 0) {
            g_bMuted = (dwVal != 0);
        }
    } else {
        DWORD dwVol = 0;
        waveOutGetVolume(0, &dwVol);
        if (dwVol == 0) g_bMuted = TRUE;
    }

    SendMessage(hMute, BM_SETCHECK, g_bMuted ? 1 : 0, 0);
    SendMessage(hSlider, CM_SETPOS, pos, 0);
}

void SetSystemVolume(int pos) {
    DWORD dwVal = (DWORD)((255L - pos) * 65535L / 255L);
    if (g_dwVolCtrlID != (DWORD)-1) {
        WIN16_MIXERCONTROLDETAILS mcd;
        _fmemset(&mcd, 0, sizeof(WIN16_MIXERCONTROLDETAILS));
        mcd.cbStruct = sizeof(WIN16_MIXERCONTROLDETAILS);
        mcd.dwControlID = g_dwVolCtrlID;
        mcd.cChannels = 1;
        mcd.cbDetails = sizeof(DWORD);
        mcd.paDetails = &dwVal;
        fnMixerSetControlDetails(0, &mcd, MIXER_SETCONTROLDETAILSF_VALUE);
    } else {
        WORD wVol = (WORD)dwVal;
        DWORD dwVol = ((DWORD)wVol << 16) | wVol;
        waveOutSetVolume(0, dwVol);
        g_dwSavedVol = dwVol;
    }
}

void SetSystemMute(BOOL bMuted) {
    g_bMuted = bMuted;
    if (g_dwMuteCtrlID != (DWORD)-1) {
        WIN16_MIXERCONTROLDETAILS mcd;
        DWORD dwVal = bMuted ? 1 : 0;
        _fmemset(&mcd, 0, sizeof(WIN16_MIXERCONTROLDETAILS));
        mcd.cbStruct = sizeof(WIN16_MIXERCONTROLDETAILS);
        mcd.dwControlID = g_dwMuteCtrlID;
        mcd.cChannels = 1;
        mcd.cbDetails = sizeof(DWORD);
        mcd.paDetails = &dwVal;
        fnMixerSetControlDetails(0, &mcd, MIXER_SETCONTROLDETAILSF_VALUE);
    }
    if (bMuted) {
        DWORD curVol = 0;
        waveOutGetVolume(0, &curVol);
        if (curVol != 0) g_dwSavedVol = curVol;
        waveOutSetVolume(0, 0);
    } else {
        waveOutSetVolume(0, g_dwSavedVol);
    }
}

/* Precise mouse-relative positioning with strict screen boundary clamping */
void PositionNearMouse(HWND hwnd) {
    POINT pt;
    int screenX, screenY, screenW, screenH, winX, winY;

    GetCursorPos(&pt);

    /* Use Virtual Screen metrics to support multi-monitor bounds */
    screenX = GetSystemMetrics(76); /* SM_XVIRTUALSCREEN */
    screenY = GetSystemMetrics(77); /* SM_YVIRTUALSCREEN */
    screenW = GetSystemMetrics(78); /* SM_CXVIRTUALSCREEN */
    screenH = GetSystemMetrics(79); /* SM_CYVIRTUALSCREEN */

    /* Fallback for older systems where virtual metrics return 0 */
    if (screenW == 0) {
        screenX = 0;
        screenY = 0;
        screenW = GetSystemMetrics(SM_CXSCREEN);
        screenH = GetSystemMetrics(SM_CYSCREEN);
    }

    /* Center horizontally relative to cursor */
    winX = pt.x - (WIN_WIDTH / 2);
    /* Position above the mouse pointer */
    winY = pt.y - WIN_HEIGHT - 8;

    /* Flip below the cursor if it hits the top screen edge */
    if (winY < screenY) {
        winY = pt.y + 8;
    }

    /* Strict screen clamping to virtual display boundaries */
    if (winX < screenX) {
        winX = screenX;
    } else if (winX + WIN_WIDTH > screenX + screenW) {
        winX = (screenX + screenW) - WIN_WIDTH;
    }

    if (winY < screenY) {
        winY = screenY;
    } else if (winY + WIN_HEIGHT > screenY + screenH) {
        winY = (screenY + screenH) - WIN_HEIGHT;
    }

    SetWindowPos(hwnd, HWND_TOPMOST, winX, winY, WIN_WIDTH, WIN_HEIGHT, SWP_SHOWWINDOW);
}

void ShowVolumeWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_RESTORE);
    SyncUIFromSystem();
    PositionNearMouse(hwnd);

    SetActiveWindow(hwnd);
    BringWindowToTop(hwnd);
    SetFocus(hwnd);
}

LRESULT CALLBACK SliderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static BOOL bDragging = FALSE;
    static int pos = 0;

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            HPEN hPenShadow, hPenHilight, hPenBlack, hPenTick, hOldPen;
            int i, thumbY, tx1, tx2, ty1, ty2;

            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_hbrBtnFace);

            hPenShadow  = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
            hPenHilight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
            hPenBlack   = (HPEN)GetStockObject(BLACK_PEN);
            hPenTick    = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));

            hOldPen = SelectObject(hdc, hPenTick);
            for (i = 0; i <= 5; i++) {
                int ty = 8 + (i * 64) / 5;
                MoveTo(hdc, 2, ty); LineTo(hdc, 8, ty);
            }

            SelectObject(hdc, hPenShadow);
            MoveTo(hdc, 19, 8); LineTo(hdc, 19, 73);
            SelectObject(hdc, hPenHilight);
            MoveTo(hdc, 20, 8); LineTo(hdc, 20, 73);

            thumbY = 8 + (pos * 64) / 255;
            tx1 = 10; tx2 = 30; ty1 = thumbY - 6; ty2 = thumbY + 6;

            SetRect(&rc, tx1, ty1, tx2, ty2);
            FillRect(hdc, &rc, g_hbrBtnFace);

            SelectObject(hdc, hPenHilight);
            MoveTo(hdc, tx1, ty2 - 1); LineTo(hdc, tx1, ty1); LineTo(hdc, tx2 - 1, ty1);

            SelectObject(hdc, hPenBlack);
            MoveTo(hdc, tx1, ty2); LineTo(hdc, tx2, ty2); LineTo(hdc, tx2, ty1 - 1);

            SelectObject(hdc, hPenShadow);
            MoveTo(hdc, tx1 + 1, ty2 - 1); LineTo(hdc, tx2 - 1, ty2 - 1); LineTo(hdc, tx2 - 1, ty1);

            SelectObject(hdc, hPenShadow);
            MoveTo(hdc, tx1 + 3, thumbY); LineTo(hdc, tx2 - 3, thumbY);
            SelectObject(hdc, hPenHilight);
            MoveTo(hdc, tx1 + 3, thumbY + 1); LineTo(hdc, tx2 - 3, thumbY + 1);

            SelectObject(hdc, hOldPen);
            DeleteObject(hPenShadow); DeleteObject(hPenHilight); DeleteObject(hPenTick);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            bDragging = TRUE;
            SetCapture(hwnd);
        case WM_MOUSEMOVE:
            if (bDragging) {
                int y = (int)(short)HIWORD(lParam);
                int newPos = ((y - 8) * 255) / 64;
                if (newPos < 0) newPos = 0;
                if (newPos > 255) newPos = 255;
                if (newPos != pos) {
                    pos = newPos;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                    SendMessage(GetParent(hwnd), WM_VSCROLL, SB_THUMBPOSITION, MAKELPARAM(pos, (WORD)hwnd));
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (bDragging) { bDragging = FALSE; ReleaseCapture(); }
            return 0;

        case CM_SETPOS:
            pos = (int)wParam;
            if (pos < 0) pos = 0; if (pos > 255) pos = 255;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case CM_GETPOS:
            return pos;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont;
            HMENU hSysMenu;

            g_hbrBtnFace = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

            hFont = GetStockObject(DEFAULT_GUI_FONT);
            if (!hFont) hFont = GetStockObject(ANSI_VAR_FONT);

            hLabel = CreateWindow("STATIC", "Volume", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  5, 8, 70, 15, hwnd, (HMENU)-1,
                                  ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, 0);

            hSlider = CreateWindow("MySliderClass", "", WS_CHILD | WS_VISIBLE,
                                   24, 25, 32, 80, hwnd, (HMENU)ID_SLIDER,
                                   ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            hMute = CreateWindow("BUTTON", "Mute", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 12, 110, 60, 20, hwnd, (HMENU)ID_MUTE,
                                 ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            SendMessage(hMute, WM_SETFONT, (WPARAM)hFont, 0);

            hSysMenu = GetSystemMenu(hwnd, FALSE);
            if (hSysMenu) {
                DeleteMenu(hSysMenu, SC_RESTORE, MF_BYCOMMAND);
                DeleteMenu(hSysMenu, SC_MOVE, MF_BYCOMMAND);
                DeleteMenu(hSysMenu, SC_SIZE, MF_BYCOMMAND);
                DeleteMenu(hSysMenu, SC_MINIMIZE, MF_BYCOMMAND);
                DeleteMenu(hSysMenu, SC_MAXIMIZE, MF_BYCOMMAND);
            }

            InitMixer();
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
            if (IsIconic(hwnd)) {
                PostMessage(hwnd, WM_SHOWVOL, 0, 0);
                return 0;
            }
            break;

        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE) {
                if (IsIconic(hwnd)) {
                    PostMessage(hwnd, WM_SHOWVOL, 0, 0);
                    return 0;
                }
            } else {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_RESTORE) {
                PostMessage(hwnd, WM_SHOWVOL, 0, 0);
                return 0;
            }
            if (IsIconic(hwnd) && (wParam & 0xFFF0) != SC_CLOSE) {
                PostMessage(hwnd, WM_SHOWVOL, 0, 0);
                return 0;
            }
            break;

        case WM_SHOWVOL:
            ShowVolumeWindow(hwnd);
            return 0;

        case WM_ERASEBKGND:
            if (IsIconic(hwnd)) {
                return TRUE;
            }
            break;

        case WM_PAINT:
            if (IsIconic(hwnd)) {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;

        case WM_COMMAND:
            if (wParam == ID_MUTE) {
                BOOL bChecked = (BOOL)SendMessage(hMute, BM_GETCHECK, 0, 0);
                SetSystemMute(bChecked);
            }
            return 0;

        case WM_VSCROLL: {
            if (wParam == SB_THUMBPOSITION) {
                int pos = LOWORD(lParam);
                SetSystemVolume(pos);
            }
            return 0;
        }

        case WM_CTLCOLOR:
            if (HIWORD(lParam) == CTLCOLOR_STATIC || HIWORD(lParam) == CTLCOLOR_BTN) {
                SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
                return (LRESULT)(DWORD)(WORD)g_hbrBtnFace;
            }
            break;

        case WM_DESTROY:
            if (g_hbrBtnFace) DeleteObject(g_hbrBtnFace);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;

    HWND hExisting = FindWindow("Win95VolClass", "Volume");
    if (hExisting) {
        PostMessage(hExisting, WM_SHOWVOL, 0, 0);
        return 0;
    }

    if (!hPrevInstance) {
        wc.style = 0; 
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = 0; 
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance; 
        wc.hIcon = NULL;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszMenuName = NULL; 
        wc.lpszClassName = "Win95VolClass";
        RegisterClass(&wc);

        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SliderWndProc;
        wc.hbrBackground = NULL;
        wc.lpszClassName = "MySliderClass";
        RegisterClass(&wc);
    }

    hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
                          "Win95VolClass", "Volume",
                          WS_POPUP | WS_BORDER | WS_SYSMENU,
                          0, 0, WIN_WIDTH, WIN_HEIGHT,
                          NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, SW_MINIMIZE);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}