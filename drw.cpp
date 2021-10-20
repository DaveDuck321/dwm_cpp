/* See LICENSE file for copyright and license details. */
#include "drw.hpp"
#include "util.hpp"

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

} // namespace

Fnt::~Fnt() {
    if (pattern)
        FcPatternDestroy(pattern);
    XftFontClose(dpy, xfont);
}

void Fnt::getexts(const char* text, uint len, uint* w, uint* h) const {
    if (!text)
        return;

    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, xfont, (XftChar8*)text, len, &ext);

    if (w)
        *w = ext.xOff;
    if (h)
        *h = this->h;
}

Drw::Drw(Display* dpy, int screen, Window root, uint w, uint h)
    : w{w}, h{h}, dpy{dpy}, screen{screen}, root{root},
      drawable{XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen))},
      gc{XCreateGC(dpy, root, 0, nullptr)} {

    XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
}

Drw::~Drw() {
    XFreePixmap(dpy, drawable);
    XFreeGC(dpy, gc);
}

void Drw::resize(uint w, uint h) {
    this->w = w;
    this->h = h;

    if (this->drawable) {
        XFreePixmap(dpy, drawable);
    }
    this->drawable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));
}

Fnt* Drw::fontset_create(const char* fonts[], size_t fontcount) {
    if (!fonts)
        return nullptr;

    // TODO: fix this insane pointer logic... Why not just use a vector?
    for (size_t i = 1; i <= fontcount; i++) {
        if (auto currentFont = xfont_create(fonts[fontcount - i], nullptr);
            currentFont) {
            currentFont->next.swap(this->fonts);
            this->fonts.swap(currentFont);
        }
    }
    return this->fonts.get();
}

void Drw::clr_create(XftColor* dest, const char* clrname) const {
    if (!dest || !clrname)
        return;

    if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                           DefaultColormap(dpy, screen), clrname, dest)) {
        die("error, cannot allocate color '%s'", clrname);
    }
}

/* Wrapper to create color schemes. The caller has to call free(3) on the
 * returned color scheme when done using it. */
XftColor* Drw::scm_create(const char* clrnames[], size_t clrcount) const {
    XftColor* ret;
    /* need at least two colors for a scheme */
    if (!clrnames || clrcount < 2 || !(ret = ecalloc<XftColor>(clrcount)))
        return nullptr;

    for (size_t i = 0; i < clrcount; i++) {
        clr_create(&ret[i], clrnames[i]);
    }
    return ret;
}

const Fnt& Drw::getFontset() const { return *fonts; }
void Drw::setFontset(Fnt* set) { fonts.reset(set); }

void Drw::setscheme(XftColor* scm) { scheme = scm; }

void Drw::rect(int x, int y, uint w, uint h, int filled, int invert) const {
    if (!scheme)
        return;

    XSetForeground(dpy, gc, invert ? scheme[ColBg].pixel : scheme[ColFg].pixel);

    if (filled)
        XFillRectangle(dpy, drawable, gc, x, y, w, h);
    else
        XDrawRectangle(dpy, drawable, gc, x, y, w - 1, h - 1);
}

int Drw::text(int x, int y, uint w, uint h, uint lpad, const char* text,
              int invert) const {

    char buf[1024];
    int ty;
    uint ew;
    XftDraw* d = nullptr;
    Fnt *usedfont, *curfont, *nextfont;
    size_t i, len;
    int utf8charlen, render = x || y || w || h;
    long utf8codepoint = 0;
    const char* utf8str;
    FcCharSet* fccharset;
    FcPattern* fcpattern;
    FcPattern* match;
    XftResult result;
    int charexists = 0;

    if ((render && !scheme) || !text || !fonts)
        return 0;

    if (!render) {
        w = ~w;
    } else {
        XSetForeground(dpy, gc, scheme[invert ? ColFg : ColBg].pixel);
        XFillRectangle(dpy, drawable, gc, x, y, w, h);
        d = XftDrawCreate(dpy, drawable, DefaultVisual(dpy, screen),
                          DefaultColormap(dpy, screen));
        x += lpad;
        w -= lpad;
    }

    usedfont = fonts.get();
    while (1) {
        size_t utf8strlen = 0;
        utf8str = text;
        nextfont = nullptr;
        while (*text) {
            utf8charlen = utf8decode(text, &utf8codepoint, UTF_SIZ);
            for (curfont = fonts.get(); curfont;
                 curfont = curfont->next.get()) {
                charexists = charexists ||
                             XftCharExists(dpy, curfont->xfont, utf8codepoint);
                if (charexists) {
                    if (curfont == usedfont) {
                        utf8strlen += utf8charlen;
                        text += utf8charlen;
                    } else {
                        nextfont = curfont;
                    }
                    break;
                }
            }

            if (!charexists || nextfont)
                break;
            else
                charexists = 0;
        }

        if (utf8strlen) {
            usedfont->getexts(utf8str, utf8strlen, &ew, nullptr);
            /* shorten text if necessary */
            for (len = MIN(utf8strlen, sizeof(buf) - 1); len && ew > w; len--)
                usedfont->getexts(utf8str, len, &ew, nullptr);

            if (len) {
                memcpy(buf, utf8str, len);
                buf[len] = '\0';
                if (len < utf8strlen)
                    for (i = len; i && i > len - 3; buf[--i] = '.')
                        ; /* NOP */

                if (render) {
                    ty = y + (h - usedfont->h) / 2 + usedfont->xfont->ascent;
                    XftDrawStringUtf8(d, &scheme[invert ? ColBg : ColFg],
                                      usedfont->xfont, x, ty, (XftChar8*)buf,
                                      len);
                }
                x += ew;
                w -= ew;
            }
        }

        if (!*text) {
            break;
        } else if (nextfont) {
            charexists = 0;
            usedfont = nextfont;
        } else {
            /* Regardless of whether or not a fallback font is found, the
             * character must be drawn. */
            charexists = 1;

            fccharset = FcCharSetCreate();
            FcCharSetAddChar(fccharset, utf8codepoint);

            if (!fonts->pattern) {
                /* Refer to the comment in xfont_create for more information. */
                die("the first font in the cache must be loaded from a font "
                    "string.");
            }

            fcpattern = FcPatternDuplicate(fonts->pattern);
            FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
            FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);
            FcPatternAddBool(fcpattern, FC_COLOR, FcFalse);

            FcConfigSubstitute(nullptr, fcpattern, FcMatchPattern);
            FcDefaultSubstitute(fcpattern);
            match = XftFontMatch(dpy, screen, fcpattern, &result);

            FcCharSetDestroy(fccharset);
            FcPatternDestroy(fcpattern);

            if (match) {
                // AHHH! Why do c programmers do this?
                auto newly_usedfont = xfont_create(nullptr, match);
                usedfont = newly_usedfont.get();

                if (usedfont &&
                    XftCharExists(dpy, usedfont->xfont, utf8codepoint)) {
                    for (curfont = fonts.get(); curfont->next;
                         curfont = curfont->next.get())
                        ; /* NOP */
                    curfont->next.swap(newly_usedfont);
                } else {
                    usedfont = fonts.get();
                }
            }
        }
    }
    if (d)
        XftDrawDestroy(d);

    return x + (render ? w : 0);
}

void Drw::map(Window win, int x, int y, uint w, uint h) const {
    XCopyArea(dpy, drawable, win, gc, x, y, w, h, x, y);
    XSync(dpy, False);
}

uint Drw::fontset_getwidth(const char* text_to_draw) const {
    if (!fonts || !text_to_draw)
        return 0;
    return text(0, 0, 0, 0, 0, text_to_draw, 0);
}

Cur* Drw::cur_create(int shape) const {
    Cur* cur;
    if (!(cur = ecalloc<Cur>(1)))
        return nullptr;

    cur->cursor = XCreateFontCursor(dpy, shape);

    return cur;
}

void Drw::cur_free(Cur* cursor) const {
    if (!cursor)
        return;

    XFreeCursor(dpy, cursor->cursor);
    free(cursor);
}

/* Library users should use drw_fontset_create instead. */
std::unique_ptr<Fnt> Drw::xfont_create(const char* fontname,
                                       FcPattern* fontpattern) const {
    XftFont* xfont = nullptr;
    FcPattern* pattern = nullptr;

    if (fontname) {
        /* Using the pattern found at font->xfont->pattern does not yield the
         * same substitution results as using the pattern returned by
         * FcNameParse; using the latter results in the desired fallback
         * behaviour whereas the former just results in missing-character
         * rectangles being drawn, at least with some fonts. */
        if (!(xfont = XftFontOpenName(dpy, screen, fontname))) {
            fprintf(stderr, "error, cannot load font from name: '%s'\n",
                    fontname);
            return nullptr;
        }
        if (!(pattern = FcNameParse((FcChar8*)fontname))) {
            fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n",
                    fontname);
            XftFontClose(dpy, xfont);
            return nullptr;
        }
    } else if (fontpattern) {
        if (!(xfont = XftFontOpenPattern(dpy, fontpattern))) {
            fprintf(stderr, "error, cannot load font from pattern.\n");
            return nullptr;
        }
    } else {
        die("no font specified.");
    }

    /* Do not allow using color fonts. This is a workaround for a BadLength
     * error from Xft with color glyphs. Modelled on the Xterm workaround. See
     * https://bugzilla.redhat.com/show_bug.cgi?id=1498269
     * https://lists.suckless.org/dev/1701/30932.html
     * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=916349
     * and lots more all over the internet.
     */
    FcBool iscol;
    if (FcPatternGetBool(xfont->pattern, FC_COLOR, 0, &iscol) ==
            FcResultMatch &&
        iscol) {
        XftFontClose(dpy, xfont);
        return nullptr;
    }

    return std::unique_ptr<Fnt>(new Fnt{
        .dpy = dpy,
        .h = xfont->ascent + xfont->descent,
        .xfont = xfont,
        .pattern = pattern,
    });
}
