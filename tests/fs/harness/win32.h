#pragma once

// <windows.h> for the tests that call Win32 themselves, without the min and
// max macros that break the standard library's.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
