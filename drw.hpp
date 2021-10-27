/* See LICENSE file for copyright and license details. */
#pragma once

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

struct Cur {
    Cursor cursor;
};

struct ColorScheme {
    std::string foreground;
    std::string background;
    std::string border;
};

struct XColorScheme {
    XColorScheme(Display*, int screen, const ColorScheme&);

    XftColor foreground;
    XftColor background;
    XftColor border;
};

template <typename Scheme> struct Theme {
    Scheme normal;
    Scheme selected;
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

class Drw {
  public:
    Drw(Display* dpy, int screen, Window win, uint w, uint h);
    ~Drw();

    void resize(uint w, uint h);

    const std::vector<DisplayFont>&
    createFontSet(const std::vector<std::string>& fontNames);

    uint getPrimaryFontHeight() const;
    const std::vector<DisplayFont>& getFontset() const;

    Theme<XColorScheme> parseTheme(const Theme<ColorScheme>&) const;
    void setScheme(const XColorScheme&);

    int getTextWidth(std::string_view);
    void renderRect(int x, int y, uint w, uint h, bool filled,
                    bool invert) const;
    int renderText(int x, int y, uint w, uint h, uint lpad, std::string_view,
                   bool invert);

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
    std::optional<XColorScheme> fScheme;

    std::vector<DisplayFont> fFonts;
};
