/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/* plattform.c – Betriebssystem-Schicht von ls64.
 *   Linux  : SG_IO-ioctl auf /dev/srN
 *   Windows: IOCTL_SCSI_PASS_THROUGH_DIRECT auf \\.\X:  (Administratorrechte noetig)
 */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "plattform.h"

#define MAX_H 8

/* =================================================================== WINDOWS */
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <malloc.h>
#include <stddef.h>
#include <io.h>
#include <fcntl.h>
#include <signal.h>
#include <direct.h>
#include <wchar.h>

/* eigene Definition (entspricht SCSI_PASS_THROUGH_DIRECT aus ntddscsi.h) */
typedef struct {
    USHORT Length; UCHAR ScsiStatus, PathId, TargetId, Lun, CdbLength, SenseInfoLength, DataIn;
    ULONG DataTransferLength, TimeOutValue; PVOID DataBuffer; ULONG SenseInfoOffset; UCHAR Cdb[16];
} LS_SPTD;
typedef struct { LS_SPTD s; ULONG pad; UCHAR sense[32]; } LS_SPTD_SENSE;
#define LS_IOCTL_SCSI_PASS_THROUGH_DIRECT 0x4D014
#define LS_DATA_OUT 0
#define LS_DATA_IN 1
#define LS_DATA_UNSPEC 2

static HANDLE g_h[MAX_H];

static wchar_t *zu_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc(sizeof(wchar_t) * (n > 0 ? n : 1));
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); else w[0] = 0;
    return w;
}
static char *zu_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = malloc(n > 0 ? n : 1);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL); else s[0] = 0;
    return s;
}

int geraet_oeffnen(const char *name, char *err, int errlen) {
    char pfad[300];
    if (!strncmp(name, "\\\\.\\", 4)) snprintf(pfad, sizeof pfad, "%s", name);
    else if (strlen(name) >= 1 && strlen(name) <= 3 && ((name[0] | 32) >= 'a' && (name[0] | 32) <= 'z')) snprintf(pfad, sizeof pfad, "\\\\.\\%c:", name[0] & ~32);
    else snprintf(pfad, sizeof pfad, "\\\\.\\%s", name);
    int i; for (i = 0; i < MAX_H && g_h[i]; i++) ;
    if (i == MAX_H) { snprintf(err, errlen, "zu viele offene Laufwerke"); return -1; }
    wchar_t *w = zu_wide(pfad);
    HANDLE h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        snprintf(err, errlen, "Kann %s nicht oeffnen (Fehler %lu)%s", name, (unsigned long)e,
                 e == ERROR_ACCESS_DENIED ? " – bitte als Administrator starten" : "");
        return -1;
    }
    g_h[i] = h;
    return i;
}
void geraet_schliessen(int h) { if (h >= 0 && h < MAX_H && g_h[h]) { CloseHandle(g_h[h]); g_h[h] = NULL; } }

void geraet_scsi(int h, const uint8_t *cdb, int cdb_len, int dir, uint8_t *buf, int buf_len, int timeout_ms, scsi_ergebnis *e) {
    memset(e, 0, sizeof *e);
    if (h < 0 || h >= MAX_H || !g_h[h]) { e->fehler = ERROR_INVALID_HANDLE; return; }
    LS_SPTD_SENSE s; memset(&s, 0, sizeof s);
    uint8_t *bb = NULL;
    if (buf_len) { bb = _aligned_malloc(buf_len, 64); if (!bb) { e->fehler = ERROR_OUTOFMEMORY; return; }
                   if (dir == 2) memcpy(bb, buf, buf_len); else memset(bb, 0, buf_len); }
    s.s.Length = sizeof(LS_SPTD);
    s.s.CdbLength = (UCHAR)cdb_len; memcpy(s.s.Cdb, cdb, cdb_len > 16 ? 16 : cdb_len);
    s.s.SenseInfoLength = sizeof s.sense;
    s.s.SenseInfoOffset = (ULONG)offsetof(LS_SPTD_SENSE, sense);
    s.s.DataIn = !buf_len ? LS_DATA_UNSPEC : dir == 2 ? LS_DATA_OUT : LS_DATA_IN;
    s.s.DataTransferLength = buf_len; s.s.DataBuffer = bb;
    s.s.TimeOutValue = (timeout_ms + 999) / 1000;
    DWORD ret = 0; double t0 = pf_zeit();
    BOOL ok = DeviceIoControl(g_h[h], LS_IOCTL_SCSI_PASS_THROUGH_DIRECT, &s, sizeof s, &s, sizeof s, &ret, NULL);
    e->dauer_ms = (unsigned)((pf_zeit() - t0) * 1000);
    if (!ok) { e->fehler = (int)GetLastError(); if (bb) _aligned_free(bb); return; }
    e->status = s.s.ScsiStatus;
    e->transport_ok = 1;
    e->resid = buf_len - (int)s.s.DataTransferLength; if (e->resid < 0) e->resid = 0;
    if (e->status == 2) { e->sense_len = s.s.SenseInfoLength > 32 ? 32 : s.s.SenseInfoLength; memcpy(e->sense, s.sense, e->sense_len); if (!e->sense_len) { e->sense_len = 18; memcpy(e->sense, s.sense, 18); } }
    if (buf_len && dir != 2) memcpy(buf, bb, buf_len);
    if (bb) _aligned_free(bb);
}

int geraete_liste(char namen[][64], int n) {
    DWORD m = GetLogicalDrives(); int k = 0;
    for (int i = 0; i < 26 && k < n; i++) {
        if (!(m & (1u << i))) continue;
        char wurzel[4] = {(char)('A' + i), ':', '\\', 0};
        if (GetDriveTypeA(wurzel) == DRIVE_CDROM) { snprintf(namen[k], 64, "%c:", 'A' + i); k++; }
    }
    return k;
}
const char *geraet_standard(void) {
    static char n[8][64]; return geraete_liste(n, 8) > 0 ? n[0] : "D:";
}

double pf_zeit(void) {
    static LARGE_INTEGER f; LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
void pf_warte_ms(int ms) { Sleep(ms); }

static volatile int *g_flag;
static BOOL WINAPI strg_c(DWORD t) { (void)t; if (g_flag) *g_flag = 1; return TRUE; }
static void sig_h(int s) { (void)s; if (g_flag) *g_flag = 1; signal(SIGINT, sig_h); }
void pf_abbruch_einrichten(volatile int *flag) { g_flag = flag; SetConsoleCtrlHandler(strg_c, TRUE); signal(SIGINT, sig_h); signal(SIGTERM, sig_h); }
void pf_wach_halten(int an) { SetThreadExecutionState(an ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS); }

FILE *pf_fopen(const char *p, const char *m) {
    wchar_t *wp = zu_wide(p), *wm = zu_wide(m);
    FILE *f = _wfopen(wp, wm); free(wp); free(wm); return f;
}
int pf_existiert(const char *p) { wchar_t *w = zu_wide(p); DWORD a = GetFileAttributesW(w); free(w); return a != INVALID_FILE_ATTRIBUTES; }
void pf_loeschen(const char *p) { wchar_t *w = zu_wide(p); DeleteFileW(w); free(w); }
int pf_ordner_anlegen(const char *p) {
    char t[1024]; snprintf(t, sizeof t, "%s", p);
    for (char *c = t + 3; *c; c++) if (*c == '\\' || *c == '/') { char k = *c; *c = 0; wchar_t *w = zu_wide(t); _wmkdir(w); free(w); *c = k; }
    wchar_t *w = zu_wide(t); _wmkdir(w); free(w);
    return pf_existiert(p) ? 0 : -1;
}
void pf_start(int *argc, char ***argv) {
    SetConsoleOutputCP(CP_UTF8);
    int n = 0; wchar_t **wa = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!wa) return;
    char **a = malloc(sizeof(char *) * (n + 1));
    for (int i = 0; i < n; i++) a[i] = zu_utf8(wa[i]);
    a[n] = NULL; *argc = n; *argv = a;
    LocalFree(wa);
}
const char *pf_datenordner(void) {
    static char p[1024];
    if (!p[0]) { const wchar_t *w = _wgetenv(L"LOCALAPPDATA"); char *s = w ? zu_utf8(w) : NULL;
        snprintf(p, sizeof p, "%s\\LSLabelStudio", s ? s : "."); free(s); }
    return p;
}
const char *pf_programmordner(void) {
    static char p[1024];
    if (!p[0]) { wchar_t w[1024]; DWORD n = GetModuleFileNameW(NULL, w, 1024); w[n < 1024 ? n : 1023] = 0;
        char *s = zu_utf8(w); char *e = strrchr(s, '\\'); if (e) *e = 0; snprintf(p, sizeof p, "%s", s); free(s); }
    return p;
}
char pf_trenner(void) { return '\\'; }

/* =================================================================== LINUX */
#else
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <scsi/sg.h>

static int g_fd[MAX_H] = {-1, -1, -1, -1, -1, -1, -1, -1};

int geraet_oeffnen(const char *name, char *err, int errlen) {
    int i; for (i = 0; i < MAX_H && g_fd[i] >= 0; i++) ;
    if (i == MAX_H) { snprintf(err, errlen, "zu viele offene Laufwerke"); return -1; }
    int fd = open(name, O_RDWR | O_NONBLOCK);
    if (fd < 0) fd = open(name, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        snprintf(err, errlen, "Kann %s nicht oeffnen: %s%s", name, strerror(errno),
                 errno == EACCES ? "  (Rechte fehlen – installieren.sh ausfuehren oder Benutzer in die Laufwerksgruppe)" : "");
        return -1;
    }
    g_fd[i] = fd;
    return i;
}
void geraet_schliessen(int h) { if (h >= 0 && h < MAX_H && g_fd[h] >= 0) { close(g_fd[h]); g_fd[h] = -1; } }

void geraet_scsi(int h, const uint8_t *cdb, int cdb_len, int dir, uint8_t *buf, int buf_len, int timeout_ms, scsi_ergebnis *e) {
    memset(e, 0, sizeof *e);
    if (h < 0 || h >= MAX_H || g_fd[h] < 0) { e->fehler = EBADF; return; }
    sg_io_hdr_t s; memset(&s, 0, sizeof s);
    s.interface_id = 'S';
    s.cmdp = (unsigned char *)cdb; s.cmd_len = cdb_len;
    s.dxferp = buf; s.dxfer_len = buf_len;
    s.dxfer_direction = !buf_len ? SG_DXFER_NONE : dir == 2 ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV;
    s.sbp = e->sense; s.mx_sb_len = sizeof e->sense;
    s.timeout = timeout_ms;
    if (buf_len && dir != 2) memset(buf, 0, buf_len);
    if (ioctl(g_fd[h], SG_IO, &s) < 0) { e->fehler = errno; return; }
    e->status = s.status;
    e->transport_ok = s.host_status == 0 && (s.driver_status & 0x0f) == 0;
    e->resid = s.resid;
    e->sense_len = s.sb_len_wr;
    e->dauer_ms = s.duration;
}

int geraete_liste(char namen[][64], int n) {
    glob_t g; int k = 0;
    if (glob("/dev/sr[0-9]*", 0, NULL, &g) != 0) return 0;
    for (size_t i = 0; i < g.gl_pathc && k < n; i++) { snprintf(namen[k], 64, "%s", g.gl_pathv[i]); k++; }
    globfree(&g);
    return k;
}
const char *geraet_standard(void) { return "/dev/sr0"; }

double pf_zeit(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
void pf_warte_ms(int ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&t, NULL); }

static volatile int *g_flag;
static void bei_signal(int s) { (void)s; if (g_flag) *g_flag = 1; }
void pf_abbruch_einrichten(volatile int *flag) {
    g_flag = flag;
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = bei_signal;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL); sigaction(SIGHUP, &sa, NULL);
}
void pf_wach_halten(int an) { (void)an; /* Linux: GUI nutzt systemd-inhibit */ }

FILE *pf_fopen(const char *p, const char *m) { return fopen(p, m); }
int pf_existiert(const char *p) { struct stat st; return stat(p, &st) == 0; }
void pf_loeschen(const char *p) { unlink(p); }
int pf_ordner_anlegen(const char *p) {
    char t[1024]; snprintf(t, sizeof t, "%s", p);
    for (char *c = t + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(t, 0755); *c = '/'; }
    mkdir(t, 0755);
    return pf_existiert(p) ? 0 : -1;
}
void pf_start(int *argc, char ***argv) { (void)argc; (void)argv; }
const char *pf_datenordner(void) {
    static char p[1024];
    if (!p[0]) {
        const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
        if (x && *x) snprintf(p, sizeof p, "%s/ls-label-studio", x);
        else snprintf(p, sizeof p, "%s/.local/share/ls-label-studio", h ? h : ".");
    }
    return p;
}
const char *pf_programmordner(void) {
    static char p[1024];
    if (!p[0]) { ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1); if (n < 0) n = 0; p[n] = 0;
        char *e = strrchr(p, '/'); if (e) *e = 0; else strcpy(p, "."); }
    return p;
}
char pf_trenner(void) { return '/'; }
#endif
