#pragma once
#include <stdint.h>
typedef uint8_t OptionFlags;
#define OPTION_DETAILS 0x1
#define OPTION_RECURSIVE 0x2
#define OPTION_HIDDEN 0x4
#define OPTION_ALL 0x7
#define OPTION_ON(option, flag) ((option) |= (flag))
#define OPTION_OFF(option, flag) ((option) &= ~(flag))
#define OPTION_IS_ON(option, flag) (((option) & (flag)) != 0)