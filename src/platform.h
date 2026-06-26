#ifndef PLATFORM_H
#define PLATFORM_H

#include "utils.h"

#ifdef _WIN32
#define OS_WINDOWS 1
#define DEFAULT_TARGET TARGET_WINDOWS
#else
#define OS_LINUX 1
#define DEFAULT_TARGET TARGET_LINUX
#endif

const char *target_os_name(TargetPlatform p);
const char *target_object_ext(TargetPlatform p);
const char *target_output_ext(TargetPlatform p);
const char *target_asm_ext(TargetPlatform p);
const char *target_comment(TargetPlatform p);
const char *target_label_suffix(TargetPlatform p);

#endif
