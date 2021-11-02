/* See LICENSE file for copyright and license details. */
#pragma once

#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;

    int getIntersection(const Rect& other) const;
};

void die(const char* fmt, ...);
