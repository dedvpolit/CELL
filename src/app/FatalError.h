#pragma once
#include <cstdio>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// The GUI build has no console, so an unrecoverable startup error also gets a message box.
inline void ReportFatalError(const std::string& message)
{
    std::fprintf(stderr, "CELL: %s\n", message.c_str());
#ifdef _WIN32
    MessageBoxA(nullptr, message.c_str(), "CELL", MB_OK | MB_ICONERROR);
#endif
}
