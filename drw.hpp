/* See LICENSE file for copyright and license details. */
#pragma once

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include <memory>
#include <string_view>
#include <vector>

struct Cur {
    Cursor cursor;
};

class DisplayFont {
  public:
    DisplayFont(Display* display, int screen, const char* fontName);
    DisplayFont(Display* display, FcPattern* pattern);
    DisplayFont(DisplayFont&&);
    ~DisplayFont();

    bool doesCodepointExistInFont(long utf8Codepoint) const;

    DisplayFont generateDerivedFontWithCodepoint(int screen,
                                                 long utf8Codepoint) const;

    uint getHeight() const;
    uint getTextExtent(std::string_view text) const;
    XftFont* getXFont() const;

  private:
    void dieIfFontInvalid() const;

    Display* fDisplay;
    XftFont* fXfont;
    FcPattern* fPattern;
};

enum { ColFg, ColBg, ColBorder }; /* Clr scheme index */
typedef XftColor Clr;

class Drw {
  public:
    Drw(Display* dpy, int screen, Window win, uint w, uint h);
    ~Drw();

    void resize(uint w, uint h);

    const std::vector<DisplayFont>&
    createFontSet(const std::vector<std::string>& fontNames);

    uint fontset_getwidth(const char* text);

    uint getPrimaryFontHeight() const;
    const std::vector<DisplayFont>& getFontset() const;

    void clr_create(Clr* dest, const char* clrname) const;
    Clr* scm_create(const char* clrnames[], size_t clrcount) const;

    void setscheme(Clr* scm);

    void rect(int x, int y, uint w, uint h, int filled, int invert) const;
    int text(int x, int y, uint w, uint h, uint lpad, std::string_view text,
             int invert);

    void map(Window win, int x, int y, uint w, uint h) const;

    // These should move into their own class
    Cur* cur_create(int shape) const;
    void cur_free(Cur* cursor) const;

  private:
    uint fWidth, fHeight;
    Display* fDisplay;
    int fScreen;
    Window fRoot;
    Drawable fDrawable;
    GC fGC;
    Clr* fScheme;

    std::vector<DisplayFont> fFonts;
};
