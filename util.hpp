/* See LICENSE file for copyright and license details. */
#pragma once

#include <string_view>
#include <utility>

#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;

    int getIntersection(const Rect& other) const;
};

template <typename Container, typename LocationIt>
inline void shuffleToFront(Container& container, LocationIt location) {
    auto element = std::move(*location);
    container.erase(location);
    container.insert(container.begin(), std::move(element));
}

inline bool contains(const std::string_view haystack,
                     const std::string_view needle) {
    return std::string_view::npos != haystack.find(needle);
}

void die(const char* fmt, ...);
