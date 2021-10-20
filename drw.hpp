/* See LICENSE file for copyright and license details. */

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include <memory>

struct Cur {
    Cursor cursor;
};

struct Fnt {
    ~Fnt();
    void getexts(const char* text, uint len, uint* w, uint* h);

    Display* dpy;
    unsigned int h;
    XftFont* xfont;
    FcPattern* pattern;
    std::unique_ptr<Fnt> next;
};

enum { ColFg, ColBg, ColBorder }; /* Clr scheme index */
typedef XftColor Clr;

struct Drw {
    uint w, h;
    Display* dpy;
    int screen;
    Window root;
    Drawable drawable;
    GC gc;
    Clr* scheme;
    std::unique_ptr<Fnt> fonts;

    Drw(Display* dpy, int screen, Window win, uint w, uint h);
    ~Drw();

    void resize(uint w, uint h);

    Fnt* fontset_create(const char* fonts[], size_t fontcount);
    uint fontset_getwidth(const char* text);

    void clr_create(Clr* dest, const char* clrname);
    Clr* scm_create(const char* clrnames[], size_t clrcount);

    void setfontset(Fnt* set);
    void setscheme(Clr* scm);

    void rect(int x, int y, uint w, uint h, int filled, int invert);
    int text(int x, int y, uint w, uint h, uint lpad, const char* text,
             int invert);

    void map(Window win, int x, int y, uint w, uint h);

    Cur* cur_create(int shape);
    void cur_free(Cur* cursor);

  private:
    std::unique_ptr<Fnt> xfont_create(const char* fontname, FcPattern* fontpattern);
};
