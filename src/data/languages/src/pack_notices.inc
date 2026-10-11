// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The start-of-game notice for a language the engine holds lines for.
// Each line's text is tools/text-fonts/endonyms.txt, written as escaped
// bytes the way an endonym is in registry.inc: the sized body (with "%s"
// where that list writes the size), the sizeless body, the download
// button, the failure line, the retry button, and the line shown when the
// pack cannot be downloaded.

struct PackNoticeRow {
    std::string_view tag; ///< the language's BCP-47 tag
    PackNotice notice;    ///< its lines
};

constexpr std::array<PackNoticeRow, 1> kPackNotices{{
    {"zh-Hans",
     {
         "\347\256\200\344\275\223\344\270\255\346\226\207\347\216\260\345\234\250\351\234\200\350"
         "\246"
         "\201\345\215\225\347\213\254\344\270\213\350\275\275\357\274\210%"
         "s\357\274\211\343\200\202"
         "\345\256\211\350\243\205\345\256\214\346\210\220\345\211\215\357\274\214\346\270\270\346"
         "\210"
         "\217\345\260\206\346\230\276\347\244\272\350\213\261\346\226\207\343\200\202",
         "\347\256\200\344\275\223\344\270\255\346\226\207\347\216\260\345\234\250\351\234\200\350"
         "\246"
         "\201\345\215\225\347\213\254\344\270\213\350\275\275\343\200\202\345\256\211\350\243\205"
         "\345\256\214\346\210\220\345\211\215\357\274\214\346\270\270\346\210\217\345\260\206\346"
         "\230"
         "\276\347\244\272\350\213\261\346\226\207\343\200\202",
         "\344\270\213\350\275\275 \302\267 Download",
         "\344\270\213\350\275\275\345\244\261\350\264\245\343\200\202",
         "\351\207\215\350\257\225 \302\267 Try again",
         "\347\216\260\345\234\250\346\227\240\346\263\225\344\270\213\350\275\275\343\200\202",
     }},
}};
