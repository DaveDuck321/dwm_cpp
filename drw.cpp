/* See LICENSE file for copyright and license details. */
#include "drw.hpp"
#include "util.hpp"

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#define UTF_INVALID 0xFFFD
#define UTF_SIZ 4

namespace {

const unsigned char utfbyte[UTF_SIZ + 1] = {0x80, 0, 0xC0, 0xE0, 0xF0};
const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
const long utfmin[UTF_SIZ + 1] = {0, 0, 0x80, 0x800, 0x10000};
const long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

long utf8decodebyte(const char c, size_t* i) {
    for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
        if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
            return (unsigned char)c & ~utfmask[*i];
    return 0;
}

size_t utf8validate(long* u, size_t i) {
    if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
        *u = UTF_INVALID;
    for (i = 1; *u > utfmax[i]; ++i)
        ;
    return i;
}

size_t utf8decode(const char* c, long* u, size_t clen) {
    size_t i, j, len, type;
    long udecoded;

    *u = UTF_INVALID;
    if (!clen)
        return 0;
    udecoded = utf8decodebyte(c[0], &len);
    if (!BETWEEN(len, 1, UTF_SIZ))
        return 1;
    for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
        udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
        if (type)
            return j;
    }
    if (j < len)
        return 0;
    *u = udecoded;
    utf8validate(u, len);

    return len;
}

const DisplayFont*
getFirstFontWithSymbol(const std::vector<DisplayFont>& fFonts,
                       const long utf8Codepoint) {
    for (const auto& font : fFonts) {
        if (font.doesCodepointExistInFont(utf8Codepoint)) {
            return &font;
        }
    }
    return nullptr;
}

std::string_view
getContiguousCharactersWithRenderer(const DisplayFont* renderingFont,
                                    const std::vector<DisplayFont>& fonts,
                                    const std::string_view text) {
    size_t utf8StringLength = 0;
    while (utf8StringLength < text.size()) {
        long utf8Codepoint;

        const auto suffix = text.substr(utf8StringLength);
        const auto utf8CharLength =
            utf8decode(suffix.data(), &utf8Codepoint, UTF_SIZ);

        if (renderingFont != getFirstFontWithSymbol(fonts, utf8Codepoint))
            break;

        utf8StringLength += utf8CharLength;
    }

    // Hack: from the original dwm -- always render a character
    return text.substr(0, std::max(utf8StringLength, size_t{1}));
}

std::string_view cropTextToExtent(const DisplayFont& renderingFont,
                                  const std::string_view text,
                                  const size_t targetExtent) {
    // TODO: I think this is a bug, I've copied the behaviour from the original
    // dwm for now what happens if the last char in 'text' is utf8 encoded?
    for (auto view = text; !view.empty(); view.remove_suffix(1)) {
        if (renderingFont.getTextExtent(view) <= targetExtent) {
            return view;
        }
    }
    return std::string_view{};
}

} // namespace

CursorFont::CursorFont(Display* display, int shape)
    : fDisplay{display}, fCursor{XCreateFontCursor(display, shape)} {}

CursorFont::CursorFont(CursorFont&& other) : fDisplay{other.fDisplay} {
    fCursor.swap(other.fCursor);
}

CursorFont::~CursorFont() {
    if (fCursor) {
        XFreeCursor(fDisplay, getXCursor());
    }
}

Cursor CursorFont::getXCursor() const { return *fCursor; }

XColorScheme::XColorScheme(Display* display, const int screen,
                           const ColorScheme& scheme) {

    const auto defaultVisual = DefaultVisual(display, screen);
    const auto defaultColormap = DefaultColormap(display, screen);

    if (!XftColorAllocName(display, defaultVisual, defaultColormap,
                           scheme.foreground.data(), &foreground) ||
        !XftColorAllocName(display, defaultVisual, defaultColormap,
                           scheme.background.data(), &background) ||
        !XftColorAllocName(display, defaultVisual, defaultColormap,
                           scheme.border.data(), &border)) {

        die("error, color allocation failure");
    }
}

void DisplayFont::dieIfFontInvalid() const {
    // This isn't the original behaviour, should just throw to prevent Font
    // construction In DWM they just returned a null pointer instead

    /* Using the pattern found at font->xfont->pattern does not yield the
     * same substitution results as using the pattern returned by
     * FcNameParse; using the latter results in the desired fallback
     * behaviour whereas the former just results in missing-character
     * rectangles being drawn, at least with some fonts. */
    if (!fXfont) {
        die("cannot load font from name:");
    }
    if (!fPattern) {
        die("cannot parse font pattern:");
    }

    /* Do not allow using color fonts. This is a workaround for a BadLength
     * error from Xft with color glyphs. Modelled on the Xterm workaround. See
     * https://bugzilla.redhat.com/show_bug.cgi?id=1498269
     * https://lists.suckless.org/dev/1701/30932.html
     * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916349
     * and lots more all over the internet.
     */
    if (FcBool isColored; FcPatternGetBool(fXfont->pattern, FC_COLOR, 0,
                                           &isColored) == FcResultMatch &&
                          isColored) {
        die("Color fonts are not permitted");
    }
}

DisplayFont::DisplayFont(Display* display, FcPattern* pattern)
    : fDisplay{display}, fXfont{XftFontOpenPattern(display, pattern)},
      fPattern{pattern} {

    dieIfFontInvalid();
}

DisplayFont::DisplayFont(Display* display, const int screen,
                         const char* fontname)
    : fDisplay{display}, fXfont{XftFontOpenName(display, screen, fontname)},
      fPattern{FcNameParse((FcChar8*)fontname)} {

    dieIfFontInvalid();
}

DisplayFont::DisplayFont(DisplayFont&& other)
    : fDisplay{other.fDisplay}, fXfont{other.fXfont}, fPattern{other.fPattern} {

    other.fPattern = nullptr;
    other.fXfont = nullptr;
}

DisplayFont::~DisplayFont() {
    if (fPattern) {
        FcPatternDestroy(fPattern);
    }
    if (fXfont) {
        XftFontClose(fDisplay, fXfont);
    }
}

bool DisplayFont::doesCodepointExistInFont(long utf8Codepoint) const {
    return XftCharExists(fDisplay, fXfont, utf8Codepoint);
}

std::optional<DisplayFont>
DisplayFont::generateDerivedFontWithCodepoint(const int screen,
                                              const long utf8Codepoint) const {
    auto* fcCharSet = FcCharSetCreate();
    FcCharSetAddChar(fcCharSet, utf8Codepoint);

    if (!fPattern)
        die("First font in the cache must be loaded from a font string.");

    auto* fcPattern = FcPatternDuplicate(fPattern);
    FcPatternAddCharSet(fcPattern, FC_CHARSET, fcCharSet);
    FcPatternAddBool(fcPattern, FC_SCALABLE, FcTrue);
    FcPatternAddBool(fcPattern, FC_COLOR, FcFalse);

    FcConfigSubstitute(nullptr, fcPattern, FcMatchPattern);
    FcDefaultSubstitute(fcPattern);

    XftResult result;
    auto* match = XftFontMatch(fDisplay, screen, fcPattern, &result);

    FcCharSetDestroy(fcCharSet);
    FcPatternDestroy(fcPattern);

    if (!match)
        die("Match fail: TODO: figure out what should happen here");

    if (DisplayFont newFont{fDisplay, match};
        newFont.doesCodepointExistInFont(utf8Codepoint)) {
        return newFont;
    }

    fprintf(stderr, "Codepoint doesn't exist: reverting to default font\n");
    return std::nullopt;
}

uint DisplayFont::getHeight() const { return fXfont->ascent + fXfont->descent; }

XftFont* DisplayFont::getXFont() const { return fXfont; };

uint DisplayFont::getTextExtent(const std::string_view text) const {
    if (text.empty())
        return 0;

    XGlyphInfo extent;
    XftTextExtentsUtf8(fDisplay, fXfont, (XftChar8*)text.data(), text.size(),
                       &extent);

    return extent.xOff;
}

Drw::Drw(Display* display, int screen, Window root, uint w, uint h)
    : fWidth{w}, fHeight{h}, fDisplay{display}, fScreen{screen}, fRoot{root},
      fDrawable{
          XCreatePixmap(display, root, w, h, DefaultDepth(display, screen))},
      fGC{XCreateGC(display, root, 0, nullptr)} {

    XSetLineAttributes(display, fGC, 1, LineSolid, CapButt, JoinMiter);
}

Drw::~Drw() {
    XFreePixmap(fDisplay, fDrawable);
    XFreeGC(fDisplay, fGC);
}

void Drw::resize(const uint w, const uint h) {
    fWidth = w;
    fHeight = h;

    if (fDrawable) {
        XFreePixmap(fDisplay, fDrawable);
    }
    fDrawable = XCreatePixmap(fDisplay, fRoot, fWidth, fHeight,
                              DefaultDepth(fDisplay, fScreen));
}

const std::vector<DisplayFont>&
Drw::createFontSet(const std::vector<std::string>& fontNames) {
    for (const auto& fontName : fontNames) {
        fFonts.emplace_back(fDisplay, fScreen, fontName.data());
    }
    return fFonts;
}

Theme<XColorScheme> Drw::parseTheme(const Theme<ColorScheme>& scheme) const {
    return {
        .normal = {fDisplay, fScreen, scheme.normal},
        .selected = {fDisplay, fScreen, scheme.selected},
    };
}

uint Drw::getPrimaryFontHeight() const { return fFonts.at(0).getHeight(); }

const std::vector<DisplayFont>& Drw::getFontset() const { return fFonts; }

void Drw::setScheme(const XColorScheme& scheme) { fScheme = scheme; }

void Drw::renderRect(const int x, const int y, const uint w, const uint h,
                     const bool filled, const bool invert) const {
    if (!fScheme)
        return;

    XSetForeground(fDisplay, fGC,
                   invert ? fScheme->background.pixel
                          : fScheme->foreground.pixel);

    if (filled) {
        XFillRectangle(fDisplay, fDrawable, fGC, x, y, w, h);
    } else {
        XDrawRectangle(fDisplay, fDrawable, fGC, x, y, w - 1, h - 1);
    }
}

int Drw::renderText(int x, const int y, uint w, uint h, const uint lpad,
                    std::string_view text, const bool invert) {

    bool shouldRender = x || y || w || h;
    if ((shouldRender && !fScheme) || text.empty() || fFonts.empty()) {
        return 0;
    }

    XftDraw* xftDrawer = nullptr;
    if (shouldRender) {
        XSetForeground(fDisplay, fGC,
                       invert ? fScheme->foreground.pixel
                              : fScheme->background.pixel);
        XFillRectangle(fDisplay, fDrawable, fGC, x, y, w, h);
        xftDrawer =
            XftDrawCreate(fDisplay, fDrawable, DefaultVisual(fDisplay, fScreen),
                          DefaultColormap(fDisplay, fScreen));
        x += lpad;
        w -= lpad;
    } else {
        w = ~w;
    }

    while (!text.empty()) {
        long utf8Codepoint;
        utf8decode(text.data(), &utf8Codepoint, UTF_SIZ);

        const auto* renderingFont =
            getFirstFontWithSymbol(fFonts, utf8Codepoint);

        if (!renderingFont) {
            // Make a new font to render this character
            // NOTE: pointer into vector: don't mutate fFonts past this point
            auto newFont = fFonts[0].generateDerivedFontWithCodepoint(
                fScreen, utf8Codepoint);
            if (newFont) {
                renderingFont = &fFonts.emplace_back(std::move(*newFont));
            } else {
                renderingFont = &(*fFonts.begin());
            }
        }

        const auto textToRender =
            getContiguousCharactersWithRenderer(renderingFont, fFonts, text);
        text.remove_prefix(textToRender.size());

        const auto croppedTextToRender =
            cropTextToExtent(*renderingFont, textToRender, w);

        if (!croppedTextToRender.empty()) {
            // TODO: render elipsis here if textToRender != croppedTextToRender

            if (shouldRender) {
                const auto ty = y + (h - renderingFont->getHeight()) / 2 +
                                renderingFont->getXFont()->ascent;
                XftDrawStringUtf8(xftDrawer,
                                  invert ? &fScheme->background
                                         : &fScheme->foreground,
                                  renderingFont->getXFont(), x, ty,
                                  (XftChar8*)croppedTextToRender.data(),
                                  croppedTextToRender.size());
            }

            const auto finalExtent =
                renderingFont->getTextExtent(croppedTextToRender);
            x += finalExtent;
            w -= finalExtent;
        }
    }

    if (xftDrawer) {
        XftDrawDestroy(xftDrawer);
    }

    return x + (shouldRender ? w : 0);
}

int Drw::getTextWidth(const std::string_view text) {
    return renderText(0, 0, 0, 0, 0, text, 0);
}

void Drw::map(Window win, int x, int y, uint w, uint h) const {
    XCopyArea(fDisplay, fDrawable, win, fGC, x, y, w, h, x, y);
    XSync(fDisplay, False);
}
