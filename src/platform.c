#include "platform.h"

const char *target_os_name(TargetPlatform p) {
    return p == TARGET_LINUX ? "linux" : "windows";
}

const char *target_object_ext(TargetPlatform p) {
    return p == TARGET_LINUX ? ".o" : ".obj";
}

const char *target_output_ext(TargetPlatform p) {
    return p == TARGET_LINUX ? "" : ".exe";
}

const char *target_asm_ext(TargetPlatform p) {
    (void)p;
    return ".asm";
}

const char *target_comment(TargetPlatform p) {
    (void)p;
    return ";";
}

const char *target_label_suffix(TargetPlatform p) {
    (void)p;
    return ":";
}
