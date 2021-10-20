/* See LICENSE file for copyright and license details. */

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include <memory>

struct Cur {
    Cursor cursor;
};

struct Fnt {
    ~Fnt();
    void getexts(const char* text, uint len, uint* w, uint* h) const;

    Display* dpy;
    unsigned int h;
    XftFont* xfont;
    FcPattern* pattern;
    std::unique_ptr<Fnt> next;
};

enum { ColFg, ColBg, ColBorder }; /* Clr scheme index */
typedef XftColor Clr;

class Drw {
  private:
    uint w, h;
    Display* dpy;
    int screen;
    Window root;
    Drawable drawable;
    GC gc;
    Clr* scheme;
    std::unique_ptr<Fnt> fonts;

  public:
    Drw(Display* dpy, int screen, Window win, uint w, uint h);
    ~Drw();

    void resize(uint w, uint h);

    Fnt* fontset_create(const char* fonts[], size_t fontcount);
    uint fontset_getwidth(const char* text) const;

    const Fnt& getFontset() const;
    void setFontset(Fnt* set);

    void clr_create(Clr* dest, const char* clrname) const;
    Clr* scm_create(const char* clrnames[], size_t clrcount) const;

    
    void setscheme(Clr* scm);

    void rect(int x, int y, uint w, uint h, int filled, int invert) const;
    int text(int x, int y, uint w, uint h, uint lpad, const char* text,
             int invert) const;

    void map(Window win, int x, int y, uint w, uint h) const;

    // These should move into their own class
    Cur* cur_create(int shape) const;
    void cur_free(Cur* cursor) const;

  private:
    std::unique_ptr<Fnt> xfont_create(const char* fontname,
                                      FcPattern* fontpattern) const;
};
