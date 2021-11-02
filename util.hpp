/* See LICENSE file for copyright and license details. */
#pragma once

#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

struct Rect {
    int x, y, width, height;

    int getIntersection(const Rect& other) const;
};

void die(const char* fmt, ...);
