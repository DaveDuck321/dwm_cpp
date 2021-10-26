/* See LICENSE file for copyright and license details. */
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

void die(const char* fmt, ...);

template <typename T> T* ecalloc(size_t nmemb) {
    if (auto pointer = static_cast<T*>(calloc(nmemb, sizeof(T))); pointer) {
        return pointer;
    }

    die("calloc:");
    return nullptr;
}