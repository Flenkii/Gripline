// Gripline Lite installer .exe for Forza Horizon 1: ReImagined
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// A small starter: it writes the installer (installer\GriplineLite.ps1, the logo and the five
// Gripline Lite source files, all built in, see files.h) to %LOCALAPPDATA%\GriplineLite\installer
// and opens it with Windows PowerShell. It needs no admin rights and downloads nothing.
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wchar.h>
#include "files.h"

static int WriteAll(const wchar_t* dir) {
  for (unsigned i = 0; i < sizeof(kFiles) / sizeof(kFiles[0]); ++i) {
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, kFiles[i].path);
    wchar_t folder[MAX_PATH * 2];
    wcscpy(folder, path);
    *wcsrchr(folder, L'\\') = 0;
    SHCreateDirectoryExW(NULL, folder, NULL);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    BOOL ok = WriteFile(f, kFiles[i].data, kFiles[i].size, &written, NULL);
    CloseHandle(f);
    if (!ok || written != kFiles[i].size) return 0;
  }
  return 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
  (void)inst, (void)prev, (void)cmd, (void)show;
  wchar_t local[MAX_PATH];
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local))) return 1;
  wchar_t dir[MAX_PATH * 2];
  swprintf(dir, MAX_PATH * 2, L"%ls\\GriplineLite", local);
  if (!WriteAll(dir)) {
    MessageBoxW(NULL, L"Gripline Lite: could not write the installer files.", L"Gripline Lite", MB_ICONERROR);
    return 1;
  }
  wchar_t args[MAX_PATH * 3];
  swprintf(args, MAX_PATH * 3,
           L"-NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File \"%ls\\installer\\GriplineLite.ps1\"", dir);
  SHELLEXECUTEINFOW sei = {sizeof(sei)};
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpFile = L"powershell.exe";
  sei.lpParameters = args;
  sei.nShow = SW_HIDE;
  if (!ShellExecuteExW(&sei)) {
    MessageBoxW(NULL, L"Gripline Lite: Windows PowerShell could not be started.", L"Gripline Lite", MB_ICONERROR);
    return 1;
  }
  WaitForSingleObject(sei.hProcess, INFINITE);
  CloseHandle(sei.hProcess);
  return 0;
}
