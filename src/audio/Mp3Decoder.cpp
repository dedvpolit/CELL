// minimp3 is single-header: its implementation must live in exactly one translation unit (this
// one), or every file including AudioMixer.h would define the same functions and fail to link.
// AudioMixer.h includes minimp3_ex.h without the macro for declarations only.
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
