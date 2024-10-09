// Copyright (c) 2024, Leo Kuznetsov
// This code and the accompanying materials are made available under the terms
// of BSD-3 license, which accompanies this distribution. The full text of the
// license may be found at https://opensource.org/license/bsd-3-clause

#include "unstd.h"
#include "rc_test.h"
#include <string.h>

// --verbose --randomize --iterations 2
// -i 99 -v -r

int main(int argc, const char* argv[]) {
    int iterations = 1;
    bool verbose   = false;
    bool randomize = false;
    for (int i = 1; i < argc; i++) {
        verbose   |= strcmp(argv[i], "-v") == 0 ||
                     strcmp(argv[i], "--verbose") == 0;
        randomize |= strcmp(argv[i], "-r") == 0 ||
                     strcmp(argv[i], "--randomize") == 0;
        if (i < argc - 1 && strcmp(argv[i], "-i") == 0 ||
            strcmp(argv[i], "--iterations") == 0) {
            char* e = null;
            long it = strtol(argv[i + 1], &e, 10);
            if (it > 0 && e > argv[i + 1]) {
                iterations = (int)it;
            }
        }
    }
    return rc_tests(iterations, verbose, randomize);
}
