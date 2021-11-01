/* See LICENSE file for copyright and license details. */
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

struct Rect {
    int x, y, width, height;
};

void die(const char* fmt, ...);
