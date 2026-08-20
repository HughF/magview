/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of magview. magview is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
/*
 * plat_win32.c — Windows implementation of plat.h.
 *
 * The POSIX file covers Linux and macOS; the two are never compiled
 * together. Serial ports are opened in overlapped-free mode with a
 * zero-timeout read, which is the Win32 way to get the non-blocking
 * behaviour the frame loop needs.
 */
#if defined(_WIN32)

#include "plat.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- time ---------------------------------------------------------- */

uint64_t plat_now_ms(void)
{
    LARGE_INTEGER freq, now;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
        return (uint64_t)GetTickCount64();
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart * 1000) / freq.QuadPart);
}

void plat_utc_now(PlatUtc *out)
{
    if (!out)
        return;
    SYSTEMTIME st;
    GetSystemTime(&st);                    /* already UTC */
    out->year   = st.wYear;
    out->month  = st.wMonth;
    out->day    = st.wDay;
    out->hour   = st.wHour;
    out->minute = st.wMinute;
    out->second = st.wSecond;
    out->millis = st.wMilliseconds;
}

/* ---- serial -------------------------------------------------------- */

struct PlatSerial { HANDLE h; };

int plat_serial_list(PlatPortInfo *out, int max)
{
    int n = 0;
    /* Probe COM1..COM255. QueryDosDevice would enumerate, but this is simple,
     * needs no registry parsing, and only lists ports that actually open for
     * query. */
    for (int i = 1; i <= 255 && n < max; i++) {
        char name[16];
        snprintf(name, sizeof name, "COM%d", i);

        char full[24];
        snprintf(full, sizeof full, "\\\\.\\COM%d", i);

        HANDLE h = CreateFileA(full, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                /* Exists but in use — still worth showing. */
                snprintf(out[n].path, sizeof out[n].path, "%s", name);
                snprintf(out[n].label, sizeof out[n].label, "%s (busy)", name);
                n++;
            }
            continue;
        }
        CloseHandle(h);
        snprintf(out[n].path, sizeof out[n].path, "%s", name);
        snprintf(out[n].label, sizeof out[n].label, "%s", name);
        n++;
    }
    return n;
}

PlatSerial *plat_serial_open(const char *path, int baud)
{
    char full[32];
    /* Accept either "COM7" or a pre-decorated "\\.\COM7". */
    if (strncmp(path, "\\\\.\\", 4) == 0)
        snprintf(full, sizeof full, "%s", path);
    else
        snprintf(full, sizeof full, "\\\\.\\%s", path);

    HANDLE h = CreateFileA(full, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return NULL;
    }
    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fInX = dcb.fOutX = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return NULL;
    }

    /* Zero timeouts: ReadFile returns immediately with whatever is buffered,
     * which is the non-blocking read the caller wants. */
    COMMTIMEOUTS to;
    memset(&to, 0, sizeof to);
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    PlatSerial *s = calloc(1, sizeof *s);
    if (!s) {
        CloseHandle(h);
        return NULL;
    }
    s->h = h;
    return s;
}

void plat_serial_close(PlatSerial *s)
{
    if (!s)
        return;
    CloseHandle(s->h);
    free(s);
}

int plat_serial_read(PlatSerial *s, void *buf, size_t cap)
{
    if (!s || cap == 0)
        return -1;
    DWORD got = 0;
    if (!ReadFile(s->h, buf, (DWORD)cap, &got, NULL))
        return -1;
    return (int)got;
}

int plat_serial_write(PlatSerial *s, const void *buf, size_t len)
{
    if (!s)
        return -1;
    DWORD put = 0;
    if (!WriteFile(s->h, buf, (DWORD)len, &put, NULL))
        return -1;
    return (int)put;
}

/* ---- UDP ----------------------------------------------------------- */

struct PlatUdp { SOCKET s; };

static void wsa_once(void)
{
    static bool done = false;
    if (!done) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        done = true;                 /* single-threaded; no lock needed */
    }
}

PlatUdp *plat_udp_listen(int port)
{
    wsa_once();

    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return NULL;

    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
        closesocket(s);
        return NULL;
    }

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    PlatUdp *u = calloc(1, sizeof *u);
    if (!u) {
        closesocket(s);
        return NULL;
    }
    u->s = s;
    return u;
}

void plat_udp_close(PlatUdp *u)
{
    if (!u)
        return;
    closesocket(u->s);
    free(u);
}

int plat_udp_recv(PlatUdp *u, void *buf, size_t cap)
{
    if (!u)
        return -1;
    int r = recvfrom(u->s, (char *)buf, (int)cap, 0, NULL, NULL);
    if (r == SOCKET_ERROR)
        return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
    return r;
}

/* ---- filesystem ---------------------------------------------------- */

static bool appdata_join(char *buf, size_t cap, const char *leaf)
{
    char base[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, base) != S_OK)
        return false;
    return (size_t)snprintf(buf, cap, "%s\\%s", base, leaf) < cap;
}

bool plat_config_dir(char *buf, size_t cap)
{
    if (!appdata_join(buf, cap, "magview"))
        return false;
    CreateDirectoryA(buf, NULL);
    return true;
}

bool plat_documents_dir(char *buf, size_t cap)
{
    char base[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, base) == S_OK)
        return (size_t)snprintf(buf, cap, "%s", base) < cap;

    const char *up = getenv("USERPROFILE");
    if (up && *up)
        return (size_t)snprintf(buf, cap, "%s", up) < cap;
    return false;
}

bool plat_path_join(char *buf, size_t cap, const char *dir, const char *leaf)
{
    return (size_t)snprintf(buf, cap, "%s\\%s", dir, leaf) < cap;
}

bool plat_mkdir(const char *path)
{
    if (CreateDirectoryA(path, NULL))
        return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

#endif /* _WIN32 */
