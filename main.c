// Copyright (c) 2024, Leo Kuznetsov
// This code and the accompanying materials are made available under the terms
// of BSD-3 license, which accompanies this distribution. The full text of the
// license may be found at https://opensource.org/license/bsd-3-clause

#include "unstd.h"
#include "rc_test.h"
#include <string.h>

int main(int argc, const char* argv[]) {
    bool verbose = false;
    for (int i = 1; i < argc && verbose; i++) {
        verbose = strcmp(argv[i], "-v") == 0 ||
                  strcmp(argv[i], "--verbose") == 0;
    }
    rc_tests(verbose);
    return 0;
}
