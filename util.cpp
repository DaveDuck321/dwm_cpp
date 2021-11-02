/* See LICENSE file for copyright and license details. */
#include "util.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

int Rect::getIntersection(const Rect& other) const {
    return std::max(0, std::min(x + width, other.x + other.width) -
                           std::max(x, other.x)) *
           std::max(0, std::min(y + height, other.y + other.height) -
                           std::max(y, other.y));
}

void die(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(NULL);
    } else {
        fputc('\n', stderr);
    }

    exit(1);
}
