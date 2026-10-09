// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// DIAGNOSTIC (win95-exit-diag, not for landing): exit handlers registered at
// marked points of a run; exit() runs them in reverse, so the last mark
// printed puts the handler that ends the process between it and the next.
#pragma once

#include <cstdio>
#include <cstdlib>

inline const char*& oa_diag_mark_name(int n) {
    static const char* names[16] = {};
    return names[n];
}

template <int N> void oa_diag_mark_runs() {
    std::fprintf(stderr, "diag: exit handler of mark %d (%s) runs\n", N, oa_diag_mark_name(N));
    std::fflush(stderr);
}

template <int N> void oa_diag_mark(const char* name) {
    oa_diag_mark_name(N) = name;
    std::atexit(oa_diag_mark_runs<N>);
}
