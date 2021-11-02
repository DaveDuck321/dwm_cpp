/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */

#include "drw.hpp"
#include "util.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <sys/wait.h>

/* macros */
#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask)                                                        \
    (mask & ~(numlockmask | LockMask) &                                        \
     (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask |    \
      Mod5Mask))
#define MOUSEMASK (BUTTONMASK | PointerMotionMask)
#define TAGMASK ((1 << tags.size()) - 1)

namespace {
/* enums */
enum {
    NetSupported,
    NetWMName,
    NetWMState,
    NetWMCheck,
    NetWMFullscreen,
    NetActiveWindow,
    NetWMWindowType,
    NetWMWindowTypeDialog,
    NetClientList,
    NetLast
}; /* EWMH atoms */
enum {
    WMProtocols,
    WMDelete,
    WMState,
    WMTakeFocus,
    WMLast
}; /* default atoms */
enum {
    ClkTagBar,
    ClkLtSymbol,
    ClkStatusText,
    ClkWinTitle,
    ClkClientWin,
    ClkRootWin,
    ClkLast
}; /* clicks */

union Arg {
    int i;
    unsigned int ui;
    float f;
    const void* v;
};

struct Button {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg* arg);
    const Arg arg;
};

typedef struct Monitor Monitor;
typedef struct Client Client;

struct CursorTheme {
    CursorFont normal;
    CursorFont resizing;
    CursorFont moving;
};

class Client {
    struct Flags {
        bool isFixed, isFloating, isUrgent, neverFocus, isFullscreen,
            wasPreviouslyFloating;
    };

  public:
    Client(Window, const Rect&, int borderWidth);

    bool isVisible() const;
    int getBorderWidth() const;
    int getOuterHeight() const;
    int getOuterWidth() const;
    const Flags& getFlags() const;
    std::string_view getName() const;

    void resizeXClient(const Rect&);
    void resize(int x, int y, int width, int height, bool interact);
    void resizeWithMouse();
    void moveWithMouse();

    void setState(long state) const;
    void setUrgent(bool urgent);
    void setFocus() const;
    void setFullscreen(bool fullscreen);
    void toggleFloating();
    void showHide();

    void updatePropertyFromEvent(Atom property);

    void grabXButtons(bool focused) const;

    void handleConfigurationRequest(XConfigureRequestEvent*);
    bool sendXEvent(Atom proto) const;
    void unmanageAndDestroyX() const;

  private:
    void applyCustomRules();
    void sendXWindowConfiguration() const;

    Atom getXAtomProperty(Atom prop) const;
    void updateWindowTitleFromX();
    void updateWindowTypeFromX();
    void updateWMHintsTypeFromX();
    void updateSizeHintsFromX();

  public:
    Client* next; // TODO: this should be a vector
    Client* snext;
    Monitor* fMonitor; // TODO: this is bad encapsulation
    Window fWindow;
    uint fTags;

  private:
    char fName[256];
    Flags fFlags;
    Rect fSize, fOldSize;

    float fMinAspect, fMaxAspect;
    int fWidthIncrement, fHeightIncrement;
    int fBaseWidth, fBaseHeight;
    int fMaxWidth, fMaxHeight;
    int fMinWidth, fMinHeight;
    int fBorderWidth, fOldBorderWidth;
};

struct Key {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg*);
    const Arg arg;
};

struct Layout {
    const char* symbol;
    void (*arrange)(Monitor*);
};

struct Monitor {
    char ltsymbol[16];
    float mfact;
    int nmaster;
    int num;
    int by;            /* bar geometry */
    Rect sRect, wRect; /* Screen and window geometry */
    int gappx;         /* gaps between windows */
    unsigned int seltags;
    unsigned int sellt;
    unsigned int tagset[2];
    int showbar;
    int topbar;
    Client* clients;
    Client* sel;
    Client* stack;
    Monitor* next;
    Window barwin;
    const Layout* layout[2];
};

struct Rule {
    const char* xclass;
    const char* instance;
    const char* title;
    unsigned int tags;
    int isfloating;
    int monitor;
};

/* function declarations */
void arrange(Monitor* m);
void arrangemon(Monitor* m);
void attach(Client* c);
void attachstack(Client* c);
void buttonpress(XEvent* e);
void checkotherwm(void);
void cleanup(void);
void cleanupmon(Monitor* mon);
void clientmessage(XEvent* e);
void configurenotify(XEvent* e);
void configurerequest(XEvent* e);
Monitor* createmon(void);
void destroynotify(XEvent* e);
void detach(Client* c);
void detachstack(Client* c);
Monitor* dirtomon(int dir);
void drawbar(Monitor* m);
void drawbars(void);
void enternotify(XEvent* e);
void expose(XEvent* e);
void focus(Client* c);
void focusin(XEvent* e);
void focusmon(const Arg* arg);
void focusstack(const Arg* arg);
int getrootptr(int* x, int* y);
long getstate(Window w);
int gettextprop(Window w, Atom atom, char* text, unsigned int size);
void grabkeys(void);
void incnmaster(const Arg* arg);
void keypress(XEvent* e);
void killclient(const Arg* arg);
void manage(Window w, XWindowAttributes* wa);
void mappingnotify(XEvent* e);
void maprequest(XEvent* e);
void monocle(Monitor* m);
void motionnotify(XEvent* e);
void movemouse(const Arg* arg);
Client* nexttiled(Client* c);
void pop(Client*);
void propertynotify(XEvent* e);
void quit(const Arg* arg);
Monitor* recttomon(const Rect&);
void resizemouse(const Arg* arg);
void restack(Monitor* m);
void run(void);
void scan(void);
void sendmon(Client* c, Monitor* m);
void setgaps(const Arg* arg);
void setlayout(const Arg* arg);
void setmfact(const Arg* arg);
void setup(void);
void showhide(Client* c);
void sigchld(int unused);
void spawn(const Arg* arg);
void tag(const Arg* arg);
void tagmon(const Arg* arg);
void tile(Monitor*);
void togglebar(const Arg* arg);
void togglefloating(const Arg* arg);
void toggletag(const Arg* arg);
void toggleview(const Arg* arg);
void unfocus(Client* c, int setfocus);
void unmanage(Client* c, bool destroyed);
void unmapnotify(XEvent* e);
void updatebarpos(Monitor* m);
void updatebars(void);
void updateclientlist(void);
int updategeom(void);
void updatenumlockmask(void);
void updatestatus(void);
void view(const Arg* arg);
Client* wintoclient(Window w);
Monitor* wintomon(Window w);
int xerror(Display* dpy, XErrorEvent* ee);
int xerrordummy(Display* dpy, XErrorEvent* ee);
int xerrorstart(Display* dpy, XErrorEvent* ee);
void zoom(const Arg* arg);

/* variables */
// WORKAROUND: XClassHint expects a char*
char dwmClassHint[] = {'d', 'w', 'm', '\0'};
const char broken[] = "broken";
char stext[256];
int screen;
int screenWidth, screenHeight; /* X display screen geometry width, height */
int barHeight, blw = 0;        /* bar geometry */
int lrpad;                     /* sum of left and right padding for text */
int (*xerrorxlib)(Display*, XErrorEvent*);
unsigned int numlockmask = 0;
Atom wmatom[WMLast], netatom[NetLast];
int running = 1;
std::optional<CursorTheme> cursors;
std::optional<Theme<XColorScheme>> scheme;
Display* dpy;
Drw* drw;

Monitor *selmon, *allMonitors;
Window root, wmcheckwin;

constexpr bool contains(const std::string_view haystack,
                        const std::string_view needle) {
    return std::string_view::npos != haystack.find(needle);
}

/* configuration, allows nested code to access above variables */
#include "config.hpp"

static_assert(tags.size() < 32);

void handleXEvent(XEvent* event) {
    switch (event->type) {
    case ButtonPress:
        return buttonpress(event);
    case ClientMessage:
        return clientmessage(event);
    case ConfigureRequest:
        return configurerequest(event);
    case ConfigureNotify:
        return configurenotify(event);
    case DestroyNotify:
        return destroynotify(event);
    case EnterNotify:
        return enternotify(event);
    case Expose:
        return expose(event);
    case FocusIn:
        return focusin(event);
    case KeyPress:
        return keypress(event);
    case MappingNotify:
        return mappingnotify(event);
    case MapRequest:
        return maprequest(event);
    case MotionNotify:
        return motionnotify(event);
    case PropertyNotify:
        return propertynotify(event);
    case UnmapNotify:
        return unmapnotify(event);
    default:
        // TODO: throw here
        break;
    }
}

Client::Client(Window win, const Rect& clientRect, int borderWidth)
    : fWindow{win}, fSize{clientRect}, fOldSize{clientRect},
      fBorderWidth{borderpx}, fOldBorderWidth{borderWidth} {

    updateWindowTitleFromX();

    Client* t = nullptr;
    Window trans{};
    if (XGetTransientForHint(dpy, win, &trans) && (t = wintoclient(trans))) {
        fMonitor = t->fMonitor;
        fTags = t->fTags;
    } else {
        fMonitor = selmon;
        applyCustomRules();
    }

    if (fSize.x + getOuterWidth() > fMonitor->sRect.x + fMonitor->sRect.width)
        fSize.x = fMonitor->sRect.x + fMonitor->sRect.width - getOuterWidth();
    if (fSize.y + getOuterHeight() > fMonitor->sRect.y + fMonitor->sRect.height)
        fSize.y = fMonitor->sRect.y + fMonitor->sRect.height - getOuterHeight();

    fSize.x = std::max(fSize.x, fMonitor->sRect.x);
    /* only fix client y-offset, if the client center might cover the bar */
    fSize.y =
        std::max(fSize.y, ((fMonitor->by == fMonitor->sRect.y) &&
                           (fSize.x + (fSize.width / 2) >= fMonitor->wRect.x) &&
                           (fSize.x + (fSize.width / 2) <
                            fMonitor->wRect.x + fMonitor->wRect.width))
                              ? barHeight
                              : fMonitor->sRect.y);

    XWindowChanges wc{};
    wc.border_width = fBorderWidth;
    XConfigureWindow(dpy, win, CWBorderWidth, &wc);
    XSetWindowBorder(dpy, win, scheme->normal.border.pixel);
    sendXWindowConfiguration();
    updateWindowTypeFromX();
    updateSizeHintsFromX();
    updateWMHintsTypeFromX();
    XSelectInput(dpy, win,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask |
                     StructureNotifyMask);
    grabXButtons(false);
    if (!fFlags.isFloating) {
        fFlags.isFloating = fFlags.wasPreviouslyFloating =
            trans != None || fFlags.isFixed;
    }
    if (fFlags.isFloating)
        XRaiseWindow(dpy, fWindow);

    XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
                    PropModeAppend, (unsigned char*)&(fWindow), 1);
    XMoveResizeWindow(dpy, fWindow, fSize.x + 2 * screenWidth, fSize.y,
                      fSize.width, fSize.height);
    setState(NormalState);
}

bool Client::isVisible() const {
    return fTags & fMonitor->tagset[fMonitor->seltags];
}

int Client::getBorderWidth() const { return fBorderWidth; }

int Client::getOuterHeight() const { return fSize.height + 2 * fBorderWidth; }

int Client::getOuterWidth() const { return fSize.width + 2 * fBorderWidth; }

const Client::Flags& Client::getFlags() const { return fFlags; };

std::string_view Client::getName() const { return fName; };

void Client::resizeXClient(const Rect& newSize) {
    fOldSize = fSize;
    fSize = newSize;

    XWindowChanges windowChanges{};
    windowChanges.x = fSize.x;
    windowChanges.y = fSize.y;
    windowChanges.width = fSize.width;
    windowChanges.height = fSize.height;
    windowChanges.border_width = fBorderWidth;

    XConfigureWindow(dpy, fWindow,
                     CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
                     &windowChanges);
    sendXWindowConfiguration();
    XSync(dpy, False);
}

void Client::resize(int x, int y, int width, int height, const bool interact) {
    // Minimum size requirements
    width = std::max(1, width);
    height = std::max(1, height);

    if (interact) {
        if (x > screenWidth)
            x = screenWidth - getOuterWidth();
        if (y > screenHeight)
            y = screenHeight - getOuterHeight();
        if (x + width + 2 * fBorderWidth < 0)
            x = 0;
        if (y + height + 2 * fBorderWidth < 0)
            y = 0;
    } else {
        if (x >= fMonitor->wRect.x + fMonitor->wRect.width)
            x = fMonitor->wRect.x + fMonitor->wRect.width - getOuterWidth();
        if (y >= fMonitor->wRect.y + fMonitor->wRect.height)
            y = fMonitor->wRect.y + fMonitor->wRect.height - getOuterHeight();
        if (x + width + 2 * fBorderWidth <= fMonitor->wRect.x)
            x = fMonitor->wRect.x;
        if (y + height + 2 * fBorderWidth <= fMonitor->wRect.y)
            y = fMonitor->wRect.y;
    }
    height = std::max(height, barHeight);
    width = std::max(width, barHeight); // TODO: cleanup: this is not a typo

    if (resizehints || fFlags.isFloating ||
        !fMonitor->layout[fMonitor->sellt]->arrange) {
        /* see last two sentences in ICCCM 4.1.2.3 */
        bool isBaseSizeMin =
            fBaseWidth == fMinWidth && fBaseHeight == fMinHeight;

        // TODO: this logic is hard to follow
        if (!isBaseSizeMin) { /* temporarily remove base dimensions */
            width -= fBaseWidth;
            height -= fBaseHeight;
        }
        /* adjust for aspect limits */
        if (fMinAspect > 0 && fMaxAspect > 0) {
            if (fMaxAspect < static_cast<float>(width) / height) {
                width = height * fMaxAspect + 0.5f;
            } else if (fMinAspect < static_cast<float>(height) / width) {
                // TODO: this looks like a typo in original dwm
                height = width * fMinAspect + 0.5f;
            }
        }
        if (isBaseSizeMin) { /* increment calculation requires this */
            width -= fBaseWidth;
            height -= fBaseHeight;
        }

        // Ensure window is aligned with size increments
        if (fWidthIncrement)
            width -= width % fWidthIncrement;
        if (fHeightIncrement)
            height -= height % fHeightIncrement;

        // Restore base dimensions
        width = std::max(width + fBaseWidth, fMinWidth);
        height = std::max(height + fBaseHeight, fMinHeight);

        if (fMaxWidth)
            width = std::min(width, fMaxWidth);
        if (fMaxHeight)
            height = std::min(height, fMaxHeight);
    }

    // If the dimensions are unchanged, don't make the redundant X call.
    if (x != fSize.x || y != fSize.y || width != fSize.width ||
        height != fSize.height) {
        resizeXClient({x, y, width, height});
    }
}

void Client::resizeWithMouse() {
    int originalX = fSize.x;
    int originalY = fSize.y;
    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                     None, cursors->resizing.getXCursor(),
                     CurrentTime) != GrabSuccess) {
        return;
    }

    XWarpPointer(dpy, None, fWindow, 0, 0, 0, 0, fSize.width + fBorderWidth - 1,
                 fSize.height + fBorderWidth - 1);

    XEvent event{};
    Time lasttime = 0;
    do {
        XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask,
                   &event);

        switch (event.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handleXEvent(&event);
            break;
        case MotionNotify:
            if ((event.xmotion.time - lasttime) <= (1000 / 60))
                continue;

            lasttime = event.xmotion.time;

            auto newWidth =
                std::max(event.xmotion.x - originalX - 2 * fBorderWidth + 1, 1);
            auto newHeight =
                std::max(event.xmotion.y - originalY - 2 * fBorderWidth + 1, 1);

            if (fMonitor->wRect.x + newWidth >= selmon->wRect.x &&
                fMonitor->wRect.x + newWidth <=
                    selmon->wRect.x + selmon->wRect.width &&
                fMonitor->wRect.y + newHeight >= selmon->wRect.y &&
                fMonitor->wRect.y + newHeight <=
                    selmon->wRect.y + selmon->wRect.height) {
                if (!fFlags.isFloating &&
                    selmon->layout[selmon->sellt]->arrange &&
                    (std::abs(newWidth - fSize.width) > snap ||
                     std::abs(newHeight - fSize.height) > snap)) {
                    togglefloating(nullptr);
                }
            }
            if (!selmon->layout[selmon->sellt]->arrange || fFlags.isFloating)
                resize(fSize.x, fSize.y, newWidth, newHeight, true);
            break;
        }
    } while (event.type != ButtonRelease);

    XWarpPointer(dpy, None, fWindow, 0, 0, 0, 0, fSize.width + fBorderWidth - 1,
                 fSize.height + fBorderWidth - 1);
    XUngrabPointer(dpy, CurrentTime);

    while (XCheckMaskEvent(dpy, EnterWindowMask, &event)) {
    }

    if (Monitor* monitor = recttomon(fSize); monitor != selmon) {
        sendmon(this, monitor);
        selmon = monitor;
        focus(NULL);
    }
}

void Client::moveWithMouse() {
    int originalX = fSize.x;
    int originalY = fSize.y;
    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                     None, cursors->moving.getXCursor(),
                     CurrentTime) != GrabSuccess) {
        return;
    }

    int x, y;
    if (!getrootptr(&x, &y))
        return;

    Time lasttime = 0;
    XEvent event{};
    do {
        XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask,
                   &event);
        switch (event.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handleXEvent(&event);
            break;
        case MotionNotify:
            if ((event.xmotion.time - lasttime) <= (1000 / 60))
                continue;

            lasttime = event.xmotion.time;

            int newX = originalX + (event.xmotion.x - x);
            int newY = originalY + (event.xmotion.y - y);

            if (std::abs(selmon->wRect.x - newX - selmon->gappx) < snap) {
                newX = selmon->wRect.x + selmon->gappx;
            } else if (std::abs((selmon->wRect.x + selmon->wRect.width) -
                                (newX + getOuterWidth() + selmon->gappx)) <
                       snap) {
                newX = selmon->wRect.x + selmon->wRect.width - getOuterWidth() -
                       selmon->gappx;
            }
            if (std::abs(selmon->wRect.y - newY - selmon->gappx) < snap) {
                newY = selmon->wRect.y + selmon->gappx;
            } else if (std::abs((selmon->wRect.y + selmon->wRect.height) -
                                (newY + getOuterHeight() + selmon->gappx)) <
                       snap) {
                newY = selmon->wRect.y + selmon->wRect.height -
                       getOuterHeight() - selmon->gappx;
            }
            if (!fFlags.isFloating && selmon->layout[selmon->sellt]->arrange &&
                (std::abs(newX - fSize.x) > snap ||
                 std::abs(newY - fSize.y) > snap)) {
                togglefloating(nullptr);
            }
            if (!selmon->layout[selmon->sellt]->arrange || fFlags.isFloating)
                resize(newX, newY, fSize.width, fSize.height, true);
            break;
        }
    } while (event.type != ButtonRelease);

    XUngrabPointer(dpy, CurrentTime);

    if (Monitor* monitor = recttomon(fSize); monitor != selmon) {
        sendmon(this, monitor);
        selmon = monitor;
        focus(NULL);
    }
}

void Client::setState(long state) const {
    long data[] = {state, None};

    XChangeProperty(dpy, fWindow, wmatom[WMState], wmatom[WMState], 32,
                    PropModeReplace, (unsigned char*)data, 2);
}

void Client::setUrgent(bool urgent) {
    fFlags.isUrgent = urgent;

    if (auto wmHint = XGetWMHints(dpy, fWindow); wmHint) {
        wmHint->flags = urgent ? (wmHint->flags | XUrgencyHint)
                               : (wmHint->flags & ~XUrgencyHint);
        XSetWMHints(dpy, fWindow, wmHint);
        XFree(wmHint);
    }
}

void Client::setFocus() const {
    if (!fFlags.neverFocus) {
        XSetInputFocus(dpy, fWindow, RevertToPointerRoot, CurrentTime);
        XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
                        PropModeReplace, (unsigned char*)&(fWindow), 1);
    }
    sendXEvent(wmatom[WMTakeFocus]);
}

void Client::setFullscreen(const bool fullscreen) {
    if (fullscreen && !fFlags.isFullscreen) {
        XChangeProperty(dpy, fWindow, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace,
                        (unsigned char*)&netatom[NetWMFullscreen], 1);
        fFlags.isFullscreen = true;
        fFlags.isFloating = true;
        fFlags.wasPreviouslyFloating = fFlags.isFloating;
        fOldBorderWidth = fBorderWidth;
        fBorderWidth = 0;

        resizeXClient(fMonitor->sRect);
        XRaiseWindow(dpy, fWindow);
    } else if (!fullscreen && fFlags.isFullscreen) {
        XChangeProperty(dpy, fWindow, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)0, 0);
        fFlags.isFullscreen = false;
        fFlags.isFloating = fFlags.wasPreviouslyFloating;
        fSize = fOldSize;
        fBorderWidth = fOldBorderWidth;

        resizeXClient(fSize);
        arrange(fMonitor);
    }
}

void Client::toggleFloating() {
    if (fFlags.isFullscreen)
        return; /* no support for fullscreen windows */

    fFlags.isFloating = !fFlags.isFloating || fFlags.isFixed;
    if (fFlags.isFloating) {
        resize(fSize.x, fSize.y, fSize.width, fSize.height, false);
    }
}

void Client::showHide() {
    if (isVisible()) {
        /* show clients top down */
        XMoveWindow(dpy, fWindow, fSize.x, fSize.y);
        if ((!fMonitor->layout[fMonitor->sellt]->arrange ||
             fFlags.isFloating) &&
            !fFlags.isFullscreen) {
            resize(fSize.x, fSize.y, fSize.width, fSize.height, false);
        }
        showhide(snext);
    } else {
        /* hide clients bottom up */
        showhide(snext);
        XMoveWindow(dpy, fWindow, getOuterWidth() * -2, fSize.y);
    }
}

void Client::updatePropertyFromEvent(Atom property) {
    switch (property) {
    case XA_WM_TRANSIENT_FOR:
        if (Window trans;
            !fFlags.isFloating &&
            (XGetTransientForHint(dpy, fWindow, &trans)) &&
            (fFlags.isFloating = (wintoclient(trans)) != nullptr)) {

            arrange(fMonitor);
        }
        break;
    case XA_WM_NORMAL_HINTS:
        updateSizeHintsFromX();
        break;
    case XA_WM_HINTS:
        updateWMHintsTypeFromX();
        drawbars();
        break;
    default:
        break;
    }
    if (property == XA_WM_NAME || property == netatom[NetWMName]) {
        updateWindowTitleFromX();
        if (this == fMonitor->sel)
            drawbar(fMonitor);
    }
    if (property == netatom[NetWMWindowType])
        updateWindowTypeFromX();
}

void Client::grabXButtons(bool focused) const {
    updatenumlockmask();
    const std::array<uint, 4> modifiers{0, LockMask, numlockmask,
                                        numlockmask | LockMask};

    XUngrabButton(dpy, AnyButton, AnyModifier, fWindow);
    if (!focused) {
        XGrabButton(dpy, AnyButton, AnyModifier, fWindow, False, BUTTONMASK,
                    GrabModeSync, GrabModeSync, None, None);
    }
    for (const auto& button : buttons) {
        if (button.click != ClkClientWin) {
            continue;
        }
        for (const auto& modifier : modifiers) {
            XGrabButton(dpy, button.button, button.mask | modifier, fWindow,
                        False, BUTTONMASK, GrabModeAsync, GrabModeSync, None,
                        None);
        }
    }
}

void Client::handleConfigurationRequest(XConfigureRequestEvent* event) {
    if (event->value_mask & CWBorderWidth) {
        fBorderWidth = event->border_width;
    } else if (fFlags.isFloating || !selmon->layout[selmon->sellt]->arrange) {
        if (event->value_mask & CWX) {
            fOldSize.x = fSize.x;
            fSize.x = fMonitor->sRect.x + event->x;
        }
        if (event->value_mask & CWY) {
            fOldSize.y = fSize.y;
            fSize.y = fMonitor->sRect.y + event->y;
        }
        if (event->value_mask & CWWidth) {
            fOldSize.width = fSize.width;
            fSize.width = event->width;
        }
        if (event->value_mask & CWHeight) {
            fOldSize.height = fSize.height;
            fSize.height = event->height;
        }

        if ((fSize.x + fSize.width) >
                fMonitor->sRect.x + fMonitor->sRect.width &&
            fFlags.isFloating) { // Center x
            fSize.x = fMonitor->sRect.x +
                      (fMonitor->sRect.width / 2 - getOuterWidth() / 2);
        }
        if ((fSize.y + fSize.height) >
                fMonitor->sRect.y + fMonitor->sRect.height &&
            fFlags.isFloating) { // Center y
            fSize.y = fMonitor->sRect.y +
                      (fMonitor->sRect.height / 2 - getOuterHeight() / 2);
        }

        if ((event->value_mask & (CWX | CWY)) &&
            !(event->value_mask & (CWWidth | CWHeight))) {
            sendXWindowConfiguration();
        }
        if (isVisible()) {
            XMoveResizeWindow(dpy, fWindow, fSize.x, fSize.y, fSize.width,
                              fSize.height);
        }
    } else {
        sendXWindowConfiguration();
    }
}

bool Client::sendXEvent(Atom proto) const {
    bool exists = false;

    int n;
    Atom* protocols;
    if (XGetWMProtocols(dpy, fWindow, &protocols, &n)) {
        while (!exists && n--) {
            exists = protocols[n] == proto;
        }
        XFree(protocols);
    }
    if (exists) {
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = fWindow;
        event.xclient.message_type = wmatom[WMProtocols];
        event.xclient.format = 32;
        event.xclient.data.l[0] = proto;
        event.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, fWindow, False, NoEventMask, &event);
    }
    return exists;
}

void Client::unmanageAndDestroyX() const {
    XWindowChanges wc{};
    wc.border_width = fOldBorderWidth;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XConfigureWindow(dpy, fWindow, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, fWindow);
    setState(WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
}

void Client::applyCustomRules() {
    fFlags.isFloating = false;
    fTags = 0;

    XClassHint classHint = {nullptr, nullptr};
    XGetClassHint(dpy, fWindow, &classHint);

    std::string_view xclass =
        classHint.res_class ? classHint.res_class : broken;
    std::string_view instance =
        classHint.res_name ? classHint.res_name : broken;

    for (const auto& rule : rules) {
        if ((!rule.title || contains(fName, rule.title)) &&
            (!rule.xclass || contains(xclass, rule.xclass)) &&
            (!rule.instance || contains(instance, rule.instance))) {
            fFlags.isFloating = rule.isfloating;
            fTags |= rule.tags;

            Monitor* monitor;
            for (monitor = allMonitors; monitor && monitor->num != rule.monitor;
                 monitor = monitor->next) { // Empty body
            }
            if (monitor) {
                fMonitor = monitor;
            }
        }
    }
    if (classHint.res_class)
        XFree(classHint.res_class);
    if (classHint.res_name)
        XFree(classHint.res_name);

    fTags =
        fTags & TAGMASK ? fTags & TAGMASK : fMonitor->tagset[fMonitor->seltags];
}

void Client::sendXWindowConfiguration() const {
    XConfigureEvent config{};
    config.type = ConfigureNotify;
    config.display = dpy;
    config.event = fWindow;
    config.window = fWindow;
    config.x = fSize.x;
    config.y = fSize.y;
    config.width = fSize.width;
    config.height = fSize.height;
    config.border_width = fBorderWidth;
    config.above = None;
    config.override_redirect = False;
    XSendEvent(dpy, fWindow, False, StructureNotifyMask, (XEvent*)&config);
}

Atom Client::getXAtomProperty(Atom prop) const {
    int di;
    unsigned long dl;
    unsigned char* outProperty = nullptr;
    Atom da = None;

    if (XGetWindowProperty(dpy, fWindow, prop, 0L, sizeof(prop), False, XA_ATOM,
                           &da, &di, &dl, &dl, &outProperty) == Success &&
        outProperty) {
        const auto atom = *(Atom*)outProperty;
        XFree(outProperty);

        return atom;
    }
    return None;
}

void Client::updateWindowTitleFromX() {
    if (!gettextprop(fWindow, netatom[NetWMName], fName, sizeof(fName))) {
        gettextprop(fWindow, XA_WM_NAME, fName, sizeof(fName));
    }
    if (fName[0] == '\0') { /* hack to mark broken clients */
        strcpy(fName, broken);
    }
}

void Client::updateWindowTypeFromX() {
    Atom state = getXAtomProperty(netatom[NetWMState]);
    Atom wtype = getXAtomProperty(netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen]) {
        setFullscreen(true);
    }
    if (wtype == netatom[NetWMWindowTypeDialog]) {
        fFlags.isFloating = true;
    }
}

void Client::updateWMHintsTypeFromX() {
    if (XWMHints* wmHints = XGetWMHints(dpy, fWindow); wmHints) {
        if (this == selmon->sel && wmHints->flags & XUrgencyHint) {
            wmHints->flags &= ~XUrgencyHint;
            XSetWMHints(dpy, fWindow, wmHints);
        } else {
            fFlags.isUrgent = wmHints->flags & XUrgencyHint;
        }

        if (wmHints->flags & InputHint) {
            fFlags.neverFocus = !wmHints->input;
        } else {
            fFlags.neverFocus = false;
        }
        XFree(wmHints);
    }
}

void Client::updateSizeHintsFromX() {
    long msize;
    XSizeHints size{};
    if (!XGetWMNormalHints(dpy, fWindow, &size, &msize)) {
        size.flags = PSize;
    }

    if (size.flags & PBaseSize) {
        fBaseWidth = size.base_width;
        fBaseHeight = size.base_height;
    } else if (size.flags & PMinSize) {
        fBaseWidth = size.min_width;
        fBaseHeight = size.min_height;
    } else {
        fBaseWidth = fBaseHeight = 0;
    }

    if (size.flags & PResizeInc) {
        fWidthIncrement = size.width_inc;
        fHeightIncrement = size.height_inc;
    } else {
        fWidthIncrement = fHeightIncrement = 0;
    }

    if (size.flags & PMaxSize) {
        fMaxWidth = size.max_width;
        fMaxHeight = size.max_height;
    } else {
        fMaxWidth = fMaxHeight = 0;
    }

    if (size.flags & PMinSize) {
        fMinWidth = size.min_width;
        fMinHeight = size.min_height;
    } else if (size.flags & PBaseSize) {
        fMinWidth = size.base_width;
        fMinHeight = size.base_height;
    } else {
        fMinWidth = fMinHeight = 0;
    }

    if (size.flags & PAspect) {
        fMinAspect = static_cast<float>(size.min_aspect.y) / size.min_aspect.x;
        fMaxAspect = static_cast<float>(size.max_aspect.x) / size.max_aspect.y;
    } else {
        fMaxAspect = fMinAspect = 0.0;
    }
    fFlags.isFixed = (fMaxWidth && fMaxHeight && fMaxWidth == fMinWidth &&
                      fMaxHeight == fMinHeight);
}

void arrange(Monitor* m) {
    if (m)
        showhide(m->stack);
    else
        for (m = allMonitors; m; m = m->next)
            showhide(m->stack);
    if (m) {
        arrangemon(m);
        restack(m);
    } else
        for (m = allMonitors; m; m = m->next)
            arrangemon(m);
}

void arrangemon(Monitor* m) {
    strncpy(m->ltsymbol, m->layout[m->sellt]->symbol, sizeof m->ltsymbol);
    if (m->layout[m->sellt]->arrange)
        m->layout[m->sellt]->arrange(m);
}

void attach(Client* c) {
    c->next = c->fMonitor->clients;
    c->fMonitor->clients = c;
}

void attachstack(Client* c) {
    c->snext = c->fMonitor->stack;
    c->fMonitor->stack = c;
}

void buttonpress(XEvent* e) {
    unsigned int i, click;
    Arg arg = {0};
    Client* c;
    Monitor* m;
    XButtonPressedEvent* ev = &e->xbutton;

    click = ClkRootWin;
    /* focus monitor if necessary */
    if ((m = wintomon(ev->window)) && m != selmon) {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(NULL);
    }
    if (ev->window == selmon->barwin) {
        int x = 0;
        i = 0;
        do {
            x += drw->getTextWidth(tags[i]) + lrpad;
        } while (ev->x >= x && ++i < tags.size());
        if (i < tags.size()) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x < x + blw) {
            click = ClkLtSymbol;
        } else if (ev->x >
                   selmon->wRect.width - (drw->getTextWidth(stext) + lrpad)) {
            click = ClkStatusText;
        } else {
            click = ClkWinTitle;
        }
    } else if ((c = wintoclient(ev->window))) {
        focus(c);
        restack(selmon);
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }
    for (const auto& button : buttons) {
        if (click == button.click && button.func &&
            button.button == ev->button &&
            CLEANMASK(button.mask) == CLEANMASK(ev->state)) {
            button.func(click == ClkTagBar && button.arg.i == 0 ? &arg
                                                                : &button.arg);
        }
    }
}

void checkotherwm(void) {
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}

void cleanup(void) {
    Arg a = {.ui = ~0u};
    Layout foo = {"", NULL};

    view(&a);
    selmon->layout[selmon->sellt] = &foo;

    for (Monitor* m = allMonitors; m; m = m->next) {
        while (m->stack) {
            unmanage(m->stack, false);
        }
    }
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    while (allMonitors)
        cleanupmon(allMonitors);

    XDestroyWindow(dpy, wmcheckwin);
    delete drw; // TODO: this should be a unique pointer
    XSync(dpy, False);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void cleanupmon(Monitor* mon) {
    if (mon == allMonitors)
        allMonitors = allMonitors->next;
    else {
        Monitor* m;
        for (m = allMonitors; m && m->next != mon; m = m->next)
            ;
        m->next = mon->next;
    }
    XUnmapWindow(dpy, mon->barwin);
    XDestroyWindow(dpy, mon->barwin);
    delete mon;
}

void clientmessage(XEvent* e) {
    XClientMessageEvent* cme = &e->xclient;
    Client* c = wintoclient(cme->window);

    if (!c)
        return;
    if (cme->message_type == netatom[NetWMState]) {
        if (static_cast<unsigned long>(cme->data.l[1]) ==
                netatom[NetWMFullscreen] ||
            static_cast<unsigned long>(cme->data.l[2]) ==
                netatom[NetWMFullscreen]) {
            c->setFullscreen((cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                              ||
                              (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
                               !c->getFlags().isFullscreen)));
        }
    } else if (cme->message_type == netatom[NetActiveWindow]) {
        if (c != selmon->sel && !c->getFlags().isUrgent)
            c->setUrgent(true);
    }
}

void configurenotify(XEvent* e) {
    Monitor* m;
    Client* c;
    XConfigureEvent* ev = &e->xconfigure;
    int dirty;

    /* TODO: updategeom handling sucks, needs to be simplified */
    if (ev->window == root) {
        dirty = (screenWidth != ev->width || screenHeight != ev->height);
        screenWidth = ev->width;
        screenHeight = ev->height;
        if (updategeom() || dirty) {
            drw->resize(screenWidth, barHeight);
            updatebars();
            for (m = allMonitors; m; m = m->next) {
                for (c = m->clients; c; c = c->next)
                    if (c->getFlags().isFullscreen)
                        c->resizeXClient(m->sRect);
                XMoveResizeWindow(dpy, m->barwin, m->wRect.x, m->by,
                                  m->wRect.width, barHeight);
            }
            focus(NULL);
            arrange(NULL);
        }
    }
}

void configurerequest(XEvent* e) {
    XConfigureRequestEvent* ev = &e->xconfigurerequest;

    if (Client* c = wintoclient(ev->window); c) {
        c->handleConfigurationRequest(ev);
    } else {
        XWindowChanges wc{};
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
    XSync(dpy, False);
}

Monitor* createmon(void) {
    Monitor* m = new Monitor{};
    m->tagset[0] = m->tagset[1] = 1;
    m->mfact = mfact;
    m->nmaster = nmaster;
    m->showbar = showbar;
    m->topbar = topbar;
    m->gappx = gappx;
    m->layout[0] = &layouts[0];
    m->layout[1] = &layouts[1 % layouts.size()];
    strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
    return m;
}

void setgaps(const Arg* arg) {
    if ((arg->i == 0) || (selmon->gappx + arg->i < 0)) {
        selmon->gappx = 0;
    } else {
        selmon->gappx += arg->i;
    }
    arrange(selmon);
}

void destroynotify(XEvent* e) {
    Client* c;
    XDestroyWindowEvent* ev = &e->xdestroywindow;

    if ((c = wintoclient(ev->window)))
        unmanage(c, true);
}

void detach(Client* c) {
    Client** tc;

    for (tc = &c->fMonitor->clients; *tc && *tc != c; tc = &(*tc)->next)
        ;
    *tc = c->next;
}

void detachstack(Client* c) {
    Client** tc;

    for (tc = &c->fMonitor->stack; *tc && *tc != c; tc = &(*tc)->snext)
        ;
    *tc = c->snext;

    if (c == c->fMonitor->sel) {
        Client* t;
        for (t = c->fMonitor->stack; t && !t->isVisible(); t = t->snext)
            ;
        c->fMonitor->sel = t;
    }
}

Monitor* dirtomon(int dir) {
    Monitor* m = NULL;

    if (dir > 0) {
        if (!(m = selmon->next))
            m = allMonitors;
    } else if (selmon == allMonitors)
        for (m = allMonitors; m->next; m = m->next)
            ;
    else
        for (m = allMonitors; m->next != selmon; m = m->next)
            ;
    return m;
}

void drawbar(Monitor* m) {
    int x, w, tw = 0;
    int boxs = drw->getPrimaryFontHeight() / 9;
    int boxw = drw->getPrimaryFontHeight() / 6 + 2;
    unsigned int occ = 0, urg = 0;
    Client* c;

    /* draw status first so it can be overdrawn by tags later */
    if (m == selmon) { /* status is only drawn on selected monitor */
        drw->setScheme(scheme->normal);
        tw = drw->getTextWidth(stext) + 2; /* 2px right padding */
        drw->renderText(m->wRect.width - tw, 0, tw, barHeight, 0, stext, 0);
    }

    for (c = m->clients; c; c = c->next) {
        occ |= c->fTags;
        if (c->getFlags().isUrgent)
            urg |= c->fTags;
    }
    x = 0;
    for (size_t i = 0; i < tags.size(); i++) {
        w = drw->getTextWidth(tags[i]) + lrpad;
        drw->setScheme(m->tagset[m->seltags] & 1 << i ? scheme->selected
                                                      : scheme->normal);
        drw->renderText(x, 0, w, barHeight, lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i)
            drw->renderRect(x + boxs, boxs, boxw, boxw,
                            m == selmon && selmon->sel &&
                                selmon->sel->fTags & 1 << i,
                            urg & 1 << i);
        x += w;
    }
    w = blw = drw->getTextWidth(m->ltsymbol) + lrpad;
    drw->setScheme(scheme->normal);
    x = drw->renderText(x, 0, w, barHeight, lrpad / 2, m->ltsymbol, 0);

    if ((w = m->wRect.width - tw - x) > barHeight) {
        if (m->sel) {
            drw->setScheme(m == selmon ? scheme->selected : scheme->normal);
            drw->renderText(x, 0, w, barHeight, lrpad / 2, m->sel->getName(),
                            0);
            if (m->sel->getFlags().isFloating) {
                drw->renderRect(x + boxs, boxs, boxw, boxw,
                                m->sel->getFlags().isFixed, 0);
            }
        } else {
            drw->setScheme(scheme->normal);
            drw->renderRect(x, 0, w, barHeight, 1, 1);
        }
    }
    drw->map(m->barwin, 0, 0, m->wRect.width, barHeight);
}

void drawbars(void) {
    Monitor* m;

    for (m = allMonitors; m; m = m->next)
        drawbar(m);
}

void enternotify(XEvent* e) {
    Client* c;
    Monitor* m;
    XCrossingEvent* ev = &e->xcrossing;

    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) &&
        ev->window != root)
        return;
    c = wintoclient(ev->window);
    m = c ? c->fMonitor : wintomon(ev->window);
    if (m != selmon) {
        unfocus(selmon->sel, 1);
        selmon = m;
    } else if (!c || c == selmon->sel)
        return;
    focus(c);
}

void expose(XEvent* e) {
    Monitor* m;
    XExposeEvent* ev = &e->xexpose;

    if (ev->count == 0 && (m = wintomon(ev->window)))
        drawbar(m);
}

void focus(Client* c) {
    if (!c || !c->isVisible())
        for (c = selmon->stack; c && !c->isVisible(); c = c->snext)
            ;
    if (selmon->sel && selmon->sel != c)
        unfocus(selmon->sel, 0);
    if (c) {
        if (c->fMonitor != selmon)
            selmon = c->fMonitor;
        if (c->getFlags().isUrgent)
            c->setUrgent(false);
        detachstack(c);
        attachstack(c);
        c->grabXButtons(true);
        XSetWindowBorder(dpy, c->fWindow, scheme->selected.border.pixel);
        c->setFocus();
    } else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
    selmon->sel = c;
    drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent* e) {
    XFocusChangeEvent* ev = &e->xfocus;

    if (selmon->sel && ev->window != selmon->sel->fWindow)
        selmon->sel->setFocus();
}

void focusmon(const Arg* arg) {
    Monitor* m;

    if (!allMonitors->next)
        return;
    if ((m = dirtomon(arg->i)) == selmon)
        return;
    unfocus(selmon->sel, 0);
    selmon = m;
    focus(NULL);
}

void focusstack(const Arg* arg) {
    Client* c = nullptr;

    if (!selmon->sel || (selmon->sel->getFlags().isFullscreen & lockfullscreen))
        return;
    if (arg->i > 0) {
        for (c = selmon->sel->next; c && !c->isVisible(); c = c->next)
            ;
        if (!c)
            for (c = selmon->clients; c && !c->isVisible(); c = c->next)
                ;
    } else {
        Client* i;
        for (i = selmon->clients; i != selmon->sel; i = i->next)
            if (i->isVisible())
                c = i;
        if (!c)
            for (; i; i = i->next)
                if (i->isVisible())
                    c = i;
    }
    if (c) {
        focus(c);
        restack(selmon);
    }
}

int getrootptr(int* x, int* y) {
    int di;
    unsigned int dui;
    Window dummy{};

    return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long getstate(Window w) {
    int format;
    long result = -1;
    unsigned char* p = NULL;
    unsigned long n, extra;
    Atom real{};

    if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False,
                           wmatom[WMState], &real, &format, &n, &extra,
                           (unsigned char**)&p) != Success)
        return -1;
    if (n != 0)
        result = *p;
    XFree(p);
    return result;
}

int gettextprop(Window w, Atom atom, char* text, unsigned int size) {
    char** list = NULL;
    int n;
    XTextProperty name{};

    if (!text || size == 0)
        return 0;
    text[0] = '\0';
    if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
        return 0;
    if (name.encoding == XA_STRING)
        strncpy(text, (char*)name.value, size - 1);
    else {
        if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success &&
            n > 0 && *list) {
            strncpy(text, *list, size - 1);
            XFreeStringList(list);
        }
    }
    text[size - 1] = '\0';
    XFree(name.value);
    return 1;
}

void grabkeys(void) {
    updatenumlockmask();
    {
        const std::array<uint, 4> modifiers{0, LockMask, numlockmask,
                                            numlockmask | LockMask};

        XUngrabKey(dpy, AnyKey, AnyModifier, root);

        for (const auto& key : keys) {
            if (const auto code = XKeysymToKeycode(dpy, key.keysym); code) {
                for (const auto& modifier : modifiers) {
                    XGrabKey(dpy, code, key.mod | modifier, root, True,
                             GrabModeAsync, GrabModeAsync);
                }
            }
        }
    }
}

void incnmaster(const Arg* arg) {
    selmon->nmaster = std::max(selmon->nmaster + arg->i, 0);
    arrange(selmon);
}

#ifdef XINERAMA
int isuniquegeom(XineramaScreenInfo* unique, size_t n,
                 XineramaScreenInfo* info) {
    while (n--)
        if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org &&
            unique[n].width == info->width && unique[n].height == info->height)
            return 0;
    return 1;
}
#endif /* XINERAMA */

void keypress(XEvent* e) {
    XKeyEvent* ev;
    ev = &e->xkey;

    const auto keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
    for (const auto& key : keys) {
        if (keysym == key.keysym &&
            CLEANMASK(key.mod) == CLEANMASK(ev->state) && key.func) {
            key.func(&(key.arg));
        }
    }
}

void killclient(const Arg* arg) {
    if (!selmon->sel)
        return;
    if (!selmon->sel->sendXEvent(wmatom[WMDelete])) {
        XGrabServer(dpy);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(dpy, DestroyAll);
        XKillClient(dpy, selmon->sel->fWindow);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
}

void manage(Window w, XWindowAttributes* wa) {
    auto* client =
        new Client{w, {wa->x, wa->y, wa->width, wa->height}, wa->border_width};

    attach(client);
    attachstack(client);

    if (client->fMonitor == selmon)
        unfocus(selmon->sel, 0);
    client->fMonitor->sel = client;
    arrange(client->fMonitor);
    XMapWindow(dpy, client->fWindow);
    focus(nullptr);
}

void mappingnotify(XEvent* e) {
    XMappingEvent* ev = &e->xmapping;

    XRefreshKeyboardMapping(ev);
    if (ev->request == MappingKeyboard)
        grabkeys();
}

void maprequest(XEvent* e) {
    XWindowAttributes wa;
    XMapRequestEvent* ev = &e->xmaprequest;

    if (!XGetWindowAttributes(dpy, ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;
    if (!wintoclient(ev->window))
        manage(ev->window, &wa);
}

void monocle(Monitor* m) {
    unsigned int n = 0;
    Client* c;

    for (c = m->clients; c; c = c->next)
        if (c->isVisible())
            n++;
    if (n > 0) /* override layout symbol */
        snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
    for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
        c->resize(m->wRect.x, m->wRect.y,
                  m->wRect.width - 2 * c->getBorderWidth(),
                  m->wRect.height - 2 * c->getBorderWidth(), false);
}

void motionnotify(XEvent* e) {
    Monitor* mon = NULL;
    Monitor* m;
    XMotionEvent* ev = &e->xmotion;

    if (ev->window != root)
        return;
    if ((m = recttomon({ev->x_root, ev->y_root, 1, 1})) != mon && mon) {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(NULL);
    }
    mon = m;
}

void movemouse(const Arg* arg) {
    if (Client* client = selmon->sel; client) {
        if (client->getFlags().isFullscreen)
            return; /* no support moving fullscreen windows by mouse */

        restack(selmon);
        client->moveWithMouse();
    }
}

Client* nexttiled(Client* c) {
    for (; c && (c->getFlags().isFloating || !c->isVisible()); c = c->next)
        ;
    return c;
}

void pop(Client* c) {
    detach(c);
    attach(c);
    focus(c);
    arrange(c->fMonitor);
}

void propertynotify(XEvent* e) {
    XPropertyEvent* ev = &e->xproperty;
    if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
        updatestatus();
    } else if (ev->state == PropertyDelete) {
        return; /* ignore */
    } else if (Client* c = wintoclient(ev->window); c) {
        c->updatePropertyFromEvent(ev->atom);
    }
}

void quit(const Arg* arg) { running = 0; }

Monitor* recttomon(const Rect& rect) {
    Monitor* r = selmon;
    int area = 0;
    for (Monitor* m = allMonitors; m; m = m->next)
        if (int a = rect.getIntersection(m->wRect); a > area) {
            area = a;
            r = m;
        }
    return r;
}

void resizemouse(const Arg* arg) {
    if (Client* client = selmon->sel; client) {
        if (client->getFlags().isFullscreen)
            return; /* no support resizing fullscreen windows by mouse */

        restack(selmon);
        client->resizeWithMouse();
    }
}

void restack(Monitor* m) {
    Client* c;
    XEvent ev;
    XWindowChanges wc;

    drawbar(m);
    if (!m->sel)
        return;
    if (m->sel->getFlags().isFloating || !m->layout[m->sellt]->arrange)
        XRaiseWindow(dpy, m->sel->fWindow);
    if (m->layout[m->sellt]->arrange) {
        wc.stack_mode = Below;
        wc.sibling = m->barwin;
        for (c = m->stack; c; c = c->snext)
            if (!c->getFlags().isFloating && c->isVisible()) {
                XConfigureWindow(dpy, c->fWindow, CWSibling | CWStackMode, &wc);
                wc.sibling = c->fWindow;
            }
    }
    XSync(dpy, False);
    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
        ;
}

void run(void) {
    XEvent ev;
    /* main event loop */
    XSync(dpy, False);
    while (running && !XNextEvent(dpy, &ev))
        handleXEvent(&ev); /* TODO: Ignore unhandled events */
}

void scan(void) {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) ||
                wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
                manage(wins[i], &wa);
        }
        for (i = 0; i < num; i++) { /* now the transients */
            if (!XGetWindowAttributes(dpy, wins[i], &wa))
                continue;
            if (XGetTransientForHint(dpy, wins[i], &d1) &&
                (wa.map_state == IsViewable ||
                 getstate(wins[i]) == IconicState))
                manage(wins[i], &wa);
        }
        if (wins)
            XFree(wins);
    }
}

void sendmon(Client* c, Monitor* m) {
    if (c->fMonitor == m)
        return;
    unfocus(c, 1);
    detach(c);
    detachstack(c);
    c->fMonitor = m;
    c->fTags = m->tagset[m->seltags]; /* assign tags of target monitor */
    attach(c);
    attachstack(c);
    focus(NULL);
    arrange(NULL);
}

void setlayout(const Arg* arg) {
    if (!arg || !arg->v || arg->v != selmon->layout[selmon->sellt])
        selmon->sellt ^= 1;
    if (arg && arg->v)
        selmon->layout[selmon->sellt] = (Layout*)arg->v;
    strncpy(selmon->ltsymbol, selmon->layout[selmon->sellt]->symbol,
            sizeof selmon->ltsymbol);
    if (selmon->sel)
        arrange(selmon);
    else
        drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg* arg) {
    float f;

    if (!arg || !selmon->layout[selmon->sellt]->arrange)
        return;
    f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
    if (f < 0.05 || f > 0.95)
        return;
    selmon->mfact = f;
    arrange(selmon);
}

void setup(void) {
    static_assert(tags.size() < 32);

    XSetWindowAttributes wa;
    Atom utf8string;

    /* clean up any zombies immediately */
    sigchld(0);

    /* init screen */
    screen = DefaultScreen(dpy);
    screenWidth = DisplayWidth(dpy, screen);
    screenHeight = DisplayHeight(dpy, screen);
    root = RootWindow(dpy, screen);
    drw = new Drw{dpy, screen, root, screenWidth, screenHeight};
    if (drw->createFontSet(fonts).empty()) {
        die("no fonts could be loaded.");
    }
    lrpad = drw->getPrimaryFontHeight();
    barHeight = drw->getPrimaryFontHeight() + 2;
    updategeom();
    /* init atoms */
    utf8string = XInternAtom(dpy, "UTF8_STRING", False);
    wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
    netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMFullscreen] =
        XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    /* init cursors */
    cursors.emplace(CursorTheme{
        .normal = {dpy, XC_left_ptr},
        .resizing = {dpy, XC_sizing},
        .moving = {dpy, XC_fleur},
    });
    /* init appearance */
    scheme = drw->parseTheme(colors);
    /* init bars */
    updatebars();
    updatestatus();
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&wmcheckwin, 1);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
                    PropModeReplace, (unsigned char*)"dwm", 3);
    XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&wmcheckwin, 1);
    /* EWMH support per view */
    XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)netatom, NetLast);
    XDeleteProperty(dpy, root, netatom[NetClientList]);
    /* select events */
    wa.cursor = cursors->normal.getXCursor();
    wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                    ButtonPressMask | PointerMotionMask | EnterWindowMask |
                    LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
    XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
    XSelectInput(dpy, root, wa.event_mask);
    grabkeys();
    focus(NULL);
}

void showhide(Client* c) {
    if (c) {
        c->showHide();
    }
}

void sigchld(int unused) {
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        die("can't install SIGCHLD handler:");
    while (0 < waitpid(-1, NULL, WNOHANG))
        ;
}

void spawn(const Arg* arg) {
    if (arg->v == dmenucmd)
        dmenumon[0] = '0' + selmon->num;
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));
        setsid();
        execvp(((char**)arg->v)[0], (char**)arg->v);
        fprintf(stderr, "dwm: execvp %s", ((char**)arg->v)[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}

void tag(const Arg* arg) {
    if (selmon->sel && arg->ui & TAGMASK) {
        selmon->sel->fTags = arg->ui & TAGMASK;
        focus(NULL);
        arrange(selmon);
    }
}

void tagmon(const Arg* arg) {
    if (!selmon->sel || !allMonitors->next)
        return;
    sendmon(selmon->sel, dirtomon(arg->i));
}

void tile(Monitor* m) {
    Client* c;

    int n;
    for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
        ;
    if (n == 0)
        return;

    int mw;
    if (n > m->nmaster)
        mw = m->nmaster ? m->wRect.width * m->mfact : 0;
    else
        mw = m->wRect.width - m->gappx;

    int i, my, ty;
    for (i = 0, my = ty = m->gappx, c = nexttiled(m->clients); c;
         c = nexttiled(c->next), i++)
        if (i < m->nmaster) {
            auto h = (m->wRect.height - my) / (std::min(n, m->nmaster) - i) -
                     m->gappx;
            c->resize(m->wRect.x + m->gappx, m->wRect.y + my,
                      mw - (2 * c->getBorderWidth()) - m->gappx,
                      h - (2 * c->getBorderWidth()), false);
            if (my + c->getOuterHeight() + m->gappx < m->wRect.height)
                my += c->getOuterHeight() + m->gappx;
        } else {
            auto h = (m->wRect.height - ty) / (n - i) - m->gappx;
            c->resize(m->wRect.x + mw + m->gappx, m->wRect.y + ty,
                      m->wRect.width - mw - (2 * c->getBorderWidth()) -
                          2 * m->gappx,
                      h - (2 * c->getBorderWidth()), false);
            if (ty + c->getOuterHeight() + m->gappx < m->wRect.height)
                ty += c->getOuterHeight() + m->gappx;
        }
}

void togglebar(const Arg* arg) {
    selmon->showbar = !selmon->showbar;
    updatebarpos(selmon);
    XMoveResizeWindow(dpy, selmon->barwin, selmon->wRect.x, selmon->by,
                      selmon->wRect.width, barHeight);
    arrange(selmon);
}

void togglefloating(const Arg* arg) {
    if (!selmon->sel)
        return;

    selmon->sel->toggleFloating();
    arrange(selmon);
}

void toggletag(const Arg* arg) {
    unsigned int newtags;

    if (!selmon->sel)
        return;
    newtags = selmon->sel->fTags ^ (arg->ui & TAGMASK);
    if (newtags) {
        selmon->sel->fTags = newtags;
        focus(NULL);
        arrange(selmon);
    }
}

void toggleview(const Arg* arg) {
    unsigned int newtagset =
        selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

    if (newtagset) {
        selmon->tagset[selmon->seltags] = newtagset;
        focus(NULL);
        arrange(selmon);
    }
}

void unfocus(Client* c, int setfocus) {
    if (!c)
        return;
    c->grabXButtons(false);
    XSetWindowBorder(dpy, c->fWindow, scheme->normal.border.pixel);
    if (setfocus) {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
}

void unmanage(Client* c, bool destroyed) {
    Monitor* m = c->fMonitor;

    detach(c);
    detachstack(c);
    if (!destroyed)
        c->unmanageAndDestroyX();

    delete c;
    focus(NULL);
    updateclientlist();
    arrange(m);
}

void unmapnotify(XEvent* e) {
    Client* c;
    XUnmapEvent* ev = &e->xunmap;

    if ((c = wintoclient(ev->window))) {
        if (ev->send_event)
            c->setState(WithdrawnState);
        else
            unmanage(c, false);
    }
}

void updatebars(void) {
    XSetWindowAttributes wa{};
    wa.background_pixmap = ParentRelative;
    wa.event_mask = ButtonPressMask | ExposureMask;
    wa.override_redirect = True;

    XClassHint* hint = XAllocClassHint();
    hint->res_class = dwmClassHint;
    hint->res_name = dwmClassHint;

    for (Monitor* m = allMonitors; m; m = m->next) {
        if (m->barwin)
            continue;
        m->barwin =
            XCreateWindow(dpy, root, m->wRect.x, m->by, m->wRect.width,
                          barHeight, 0, DefaultDepth(dpy, screen),
                          CopyFromParent, DefaultVisual(dpy, screen),
                          CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
        XDefineCursor(dpy, m->barwin, cursors->normal.getXCursor());
        XMapRaised(dpy, m->barwin);
        XSetClassHint(dpy, m->barwin, hint);
    }
    XFree(hint);
}

void updatebarpos(Monitor* m) {
    m->wRect.y = m->sRect.y;
    m->wRect.height = m->sRect.height;
    if (m->showbar) {
        m->wRect.height -= barHeight;
        m->by = m->topbar ? m->wRect.y : m->wRect.y + m->wRect.height;
        m->wRect.y = m->topbar ? m->wRect.y + barHeight : m->wRect.y;
    } else {
        m->by = -barHeight;
    }
}

void updateclientlist() {
    Client* c;
    Monitor* m;

    XDeleteProperty(dpy, root, netatom[NetClientList]);
    for (m = allMonitors; m; m = m->next)
        for (c = m->clients; c; c = c->next)
            XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
                            PropModeAppend, (unsigned char*)&(c->fWindow), 1);
}

int updategeom(void) {
    int dirty = 0;

#ifdef XINERAMA
    if (XineramaIsActive(dpy)) {
        int i, j, n, nn;
        Monitor* m;
        XineramaScreenInfo* info = XineramaQueryScreens(dpy, &nn);

        for (n = 0, m = allMonitors; m; m = m->next, n++)
            ;
        /* only consider unique geometries as separate screens */
        XineramaScreenInfo* unique = new XineramaScreenInfo[nn];
        for (i = 0, j = 0; i < nn; i++)
            if (isuniquegeom(unique, j, &info[i]))
                memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
        XFree(info);
        nn = j;
        if (n <= nn) { /* new monitors available */
            for (i = 0; i < (nn - n); i++) {
                for (m = allMonitors; m && m->next; m = m->next)
                    ;
                if (m)
                    m->next = createmon();
                else
                    allMonitors = createmon();
            }
            for (i = 0, m = allMonitors; i < nn && m; m = m->next, i++)
                if (i >= n || unique[i].x_org != m->sRect.x ||
                    unique[i].y_org != m->sRect.y ||
                    unique[i].width != m->sRect.width ||
                    unique[i].height != m->sRect.height) {
                    dirty = 1;
                    m->num = i;
                    m->sRect.x = m->wRect.x = unique[i].x_org;
                    m->sRect.y = m->wRect.y = unique[i].y_org;
                    m->sRect.width = m->wRect.width = unique[i].width;
                    m->sRect.height = m->wRect.height = unique[i].height;
                    updatebarpos(m);
                }
        } else { /* less monitors available nn < n */
            for (i = nn; i < n; i++) {
                for (m = allMonitors; m && m->next; m = m->next)
                    ;
                for (Client* c = m->clients; c; c = m->clients) {
                    dirty = 1;
                    m->clients = c->next;
                    detachstack(c);
                    c->fMonitor = allMonitors;
                    attach(c);
                    attachstack(c);
                }
                if (m == selmon)
                    selmon = allMonitors;
                cleanupmon(m);
            }
        }
        delete[] unique;
    } else
#endif /* XINERAMA */
    {  /* default monitor setup */
        if (!allMonitors)
            allMonitors = createmon();
        if (allMonitors->sRect.width != screenWidth ||
            allMonitors->sRect.height != screenHeight) {
            dirty = 1;
            allMonitors->sRect.width = allMonitors->wRect.width = screenWidth;
            allMonitors->sRect.height = allMonitors->wRect.height =
                screenHeight;
            updatebarpos(allMonitors);
        }
    }
    if (dirty) {
        selmon = allMonitors;
        selmon = wintomon(root);
    }
    return dirty;
}

void updatenumlockmask(void) {
    XModifierKeymap* modmap;

    numlockmask = 0;
    modmap = XGetModifierMapping(dpy);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
                XKeysymToKeycode(dpy, XK_Num_Lock))
                numlockmask = (1 << i);
    XFreeModifiermap(modmap);
}

void updatestatus(void) {
    if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
        strcpy(stext, "dwm-" VERSION);
    drawbar(selmon);
}

void view(const Arg* arg) {
    if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
        return;
    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selmon);
}

Client* wintoclient(Window w) {
    Client* c;
    Monitor* m;

    for (m = allMonitors; m; m = m->next)
        for (c = m->clients; c; c = c->next)
            if (c->fWindow == w)
                return c;
    return NULL;
}

Monitor* wintomon(Window w) {
    int x, y;
    Client* c;
    Monitor* m;

    if (w == root && getrootptr(&x, &y))
        return recttomon({x, y, 1, 1});
    for (m = allMonitors; m; m = m->next)
        if (w == m->barwin)
            return m;
    if ((c = wintoclient(w)))
        return c->fMonitor;
    return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display* dpy, XErrorEvent* ee) {
    if (ee->error_code == BadWindow ||
        (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
        (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable) ||
        (ee->request_code == X_PolyFillRectangle &&
         ee->error_code == BadDrawable) ||
        (ee->request_code == X_PolySegment && ee->error_code == BadDrawable) ||
        (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
        (ee->request_code == X_GrabButton && ee->error_code == BadAccess) ||
        (ee->request_code == X_GrabKey && ee->error_code == BadAccess) ||
        (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display* dpy, XErrorEvent* ee) { return 0; }

int xerrorstart(Display* dpy, XErrorEvent* ee) {
    die("dwm: another window manager is already running");
    return -1;
}

void zoom(const Arg* arg) {
    Client* c = selmon->sel;

    if (!selmon->layout[selmon->sellt]->arrange ||
        (selmon->sel && selmon->sel->getFlags().isFloating))
        return;
    if (c == nexttiled(selmon->clients))
        if (!c || !(c = nexttiled(c->next)))
            return;
    pop(c);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1]))
        die("dwm-" VERSION);
    else if (argc != 1)
        die("usage: dwm [-v]");
    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);
    if (!(dpy = XOpenDisplay(NULL)))
        die("dwm: cannot open display");
    checkotherwm();
    setup();
    scan();
    run();
    cleanup();
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}
