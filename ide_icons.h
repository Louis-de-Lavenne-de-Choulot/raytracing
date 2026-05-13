#pragma once

// ── Font Awesome 6 Free ───────────────────────────────────────────────────────
// Codepoints are the Unicode PUA values from FA6.
// All FA solid icons live in U+F000–U+F8FF and U+E000–U+E0FF.
#define ICON_MIN_FA  0xE000
#define ICON_MAX_FA  0xF8FF

// Commonly used icons — add more as needed from:
// https://fontawesome.com/icons?s=solid&f=classic
#define ICON_FA_LOCK         "\xef\x80\xa3"   // f023
#define ICON_FA_UNLOCK       "\xef\x80\xa4"   // f09c
#define ICON_FA_EYE_SLASH    "\xef\x81\xb0"   // f070
#define ICON_FA_SUN          "\xef\x86\x85"   // f185
#define ICON_FA_LIGHTBULB    "\xef\x83\xab"   // f0eb
#define ICON_FA_FOLDER       "\xef\x81\xbb"   // f07b
#define ICON_FA_FILE         "\xef\x85\x9b"   // f15b
#define ICON_FA_TRASH        "\xef\x8b\xad"   // f2ed
#define ICON_FA_PLAY         "\xef\x81\x8b"   // f04b
#define ICON_FA_PAUSE        "\xef\x81\x8c"   // f04c
#define ICON_FA_STOP         "\xef\x81\x8d"   // f04d
#define ICON_FA_CUBE         "\xef\x86\xb2"   // f1b2
#define ICON_FA_LAYER_GROUP  "\xef\x9d\xa2"   // f5fd
#define ICON_FA_GLOBE        "\xef\x82\xac"   // f0ac
#define ICON_FA_CAMERA       "\xef\x80\xb0"   // f030
#define ICON_FA_CIRCLE_DOT   "\xef\x84\x92"   // f192 (point light)
#define ICON_FA_ARROWS_ALT   "\xef\x82\xb2"   // f0b2 (move)
#define ICON_FA_ROTATE       "\xef\x9e\xb9"   // f7b9 (rotate)
#define ICON_FA_EXPAND       "\xef\x81\x9e"   // f065 (scale)
#define ICON_FA_PLUS         "\xef\x81\xa7"   // f067
#define ICON_FA_SEARCH       "\xef\x80\x82"   // f002
#define ICON_FA_COG          "\xef\x80\x93"   // f013
#define ICON_FA_SAVE         "\xef\x83\x87"   // f0c7
#define ICON_FA_UPLOAD       "\xef\x81\x93"   // f055 (ship/export)
#define ICON_FA_TERMINAL     "\xef\x84\xa0"   // f120
#define ICON_FA_IMAGE        "\xef\x80\xbe"   // f03e (viewport)
#define ICON_FA_TREE         "\xef\x86\xbb"   // f1bb (hierarchy)
#define ICON_FA_INFO_CIRCLE  "\xef\x81\x9a"   // f05a
#define ICON_FA_COPY         "\xef\x80\x9c"   // f0c5
#define ICON_FA_CIRCLE      "\xef\x84\x91"   // f111
#define ICON_FA_SQUARE      "\xef\x83\x88"   // f0c8
#define ICON_FA_VIDEO       "\xef\x80\xbd"   // f03d
// Additional icons for tools
#define ICON_FA_HAND_POINTER "\xef\x89\xa5"   // f2a5 (hand)
#define ICON_FA_ARROWS       "\xef\x81\x87"   // f047 (arrows for translate)
#define ICON_FA_REDO_ALT     "\xef\x8b\xb9"   // f2f9 (redo)