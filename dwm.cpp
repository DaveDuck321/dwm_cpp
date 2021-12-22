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
#include "x.hpp"

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
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
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
enum {
    WMProtocols,
    WMDelete,
    WMState,
    WMTakeFocus,
    WMLast
}; /* default atoms */

struct Net_Properties {
    MutableXPropertyWithCleanup<XA_WINDOW> activeWindow, clientList;
    XProperty<XA_TEXT> wmName;
    XProperty<XA_ATOM> wmState;
    XSentinel wmFullscreen, wmWindowType, wmWindowTypeDialog;
};

enum {
    ClkTagBar,
    ClkLtSymbol,
    ClkStatusText,
    ClkWinTitle,
    ClkClientWin,
    ClkRootWin,
    ClkLast
}; /* clicks */

typedef class Monitor Monitor;
typedef class Client Client;

struct CursorTheme {
    CursorFont normal;
    CursorFont resizing;
    CursorFont moving;
};

struct Button {
    uint click;
    uint mask;
    uint button;
    std::function<void(uint)> action;
};

struct Key {
    uint mod;
    KeySym keysym;
    std::function<void()> func;
};

struct Layout {
    const char symbol[16];
    void (*arrange)(Monitor*);
};

struct Rule {
    const char* xclass;
    const char* instance;
    const char* title;
    uint tags;
    int isfloating;
    int monitor;
};

struct CommandPtr {
    const char* const* data;
};

template <typename... Args> struct Command {
    Command(Args... args) : m_data{args..., nullptr} {}
    operator CommandPtr() const { return {m_data.data()}; }
    std::array<const char*, sizeof...(Args) + 1> m_data;
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
    void hideXClientIfInvisible();

    void setState(long state) const;
    void setUrgent(bool urgent);
    void setFocus() const;
    void setFullscreen(bool fullscreen);
    void toggleFloating();

    void updatePropertyFromEvent(Atom property);
    void grabXButtons(bool focused) const;
    void handleConfigurationRequest(XConfigureRequestEvent*);
    void requestKill() const;
    bool sendXEvent(Atom proto) const;
    void unmanageAndDestroyX() const;

  private:
    void applyCustomRules();
    void sendXWindowConfiguration() const;
    void updateWindowTitleFromX();
    void updateWindowTypeFromX();
    void updateWMHintsTypeFromX();
    void updateSizeHintsFromX();

  public:
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

    MutableTextXProperty fXName;
    MutableXProperty<XA_ATOM> fXState;
};

class Monitor {
  public:
    explicit Monitor(int num);
    ~Monitor();
    Monitor(Monitor&&) = delete;

    bool isSelectedMonitor() const;
    int getMonitorNumber() const;
    Client* getClientFromWindowID(Window) const;

    void incrementMasterCount(int amount);
    void incrementMasterFactor(float amount);
    uint getActiveTags() const;
    void setActiveTags(uint);
    const Layout* getActiveLayout() const;
    void setActiveLayout(const Layout*);
    void toggleSelectedTagSet();
    void toggleSelectedLayout();

    auto getTiledClients() const;
    auto findClientLocation(Client*);
    void transferAllClients(Monitor& target);
    Client* attach(std::unique_ptr<Client>);
    std::unique_ptr<Client> detach(Client*);
    void unmanage(Client*, bool xResourceDestroyed);
    void hideClientsIfInvisible() const;

    void focus(Client* client = nullptr);
    void shiftFocusThroughStack(int direction);
    void zoomClientToMaster(Client*);
    void restackClients() const;
    void arrangeClients(bool shouldRestack = true);
    void updateBarPosition();
    void drawbar() const;
    void toggleBarRendering();

    void updateXClientList() const;
    void updateXGeometry() const;

    void monocle();
    void tile();

  public:
    Rect sRect, wRect; /* Screen and window geometry */
    int fBarY = 0;     /* bar geometry */
    int fGapSize;      /* gaps between windows */
    Window fBarID = 0;
    Client* fSelected = nullptr;

  private:
    int fMonitorNumber;
    char fLayoutSymbol[16];
    float fMasterFactor;
    int fMasterCount;
    uint fSelectedTags = 0;
    uint fSelectedLayout = 0;
    uint fTags[2];
    bool fShouldRenderBar, fShouldRenderBarOnTop;
    std::vector<std::unique_ptr<Client>> fClients;
    std::vector<Client*> fStack;
    const Layout* fLayouts[2];
};

/* function declarations */
void autostart();
void handleXEvent(XEvent* event);
void monocle(Monitor*);
void tile(Monitor*);

void focusmon(const int dir);
void focusstack(const int dir);
void incnmaster(const int dir);
void killclient();
void movemouse();
void quit();
void resizemouse();
void setgaps(const int inc);
void setlayout(const Layout* layout);
void setmfact(const float factor);
void spawn(CommandPtr command);
void tag(const uint tag);
void tagmon(const int dir);
void togglebar();
void togglefloating();
void togglelayout();
void toggletag(const uint tag);
void toggleview(const uint tag);
void view(const uint tag);
void zoom();

/* variables */
char dwmClassHint[] = {'d', 'w', 'm', '+', '+', '\0'};
const char broken[] = "broken";
char stext[256];
int screen;
int screenWidth, screenHeight; /* X display screen geometry width, height */
int barHeight, blw = 0;        /* bar geometry */
int lrpad;                     /* sum of left and right padding for text */
int (*xerrorxlib)(Display*, XErrorEvent*);
std::unique_ptr<Net_Properties> netatom;
uint numlockmask = 0;
Atom wmatom[WMLast];
int running = 1;
std::optional<CursorTheme> cursors;
std::optional<Theme<XColorScheme>> scheme;
Display* dpy;
Drw* drw;

std::vector<std::unique_ptr<Monitor>> allMonitors;
Monitor* selmon;
Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.hpp"

static_assert(tags.size() < 32);

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
    fprintf(stderr, "dwm++: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display*, XErrorEvent*) { return 0; }

int xerrorstart(Display*, XErrorEvent*) {
    die("dwm++: another window manager is already running");
    return -1;
}

void updateNumLockMask() {
    numlockmask = 0;
    XModifierKeymap* modmap = XGetModifierMapping(dpy);
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < modmap->max_keypermod; j++) {
            if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
                XKeysymToKeycode(dpy, XK_Num_Lock)) {
                numlockmask = (1 << i);
            }
        }
    }
    XFreeModifiermap(modmap);
}

void grabkeys() {
    updateNumLockMask();
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

long getXStateProperty(Window window) {
    int format;
    long result = -1;
    unsigned char* p = nullptr;
    unsigned long n, extra;
    Atom real{};
    if (XGetWindowProperty(dpy, window, wmatom[WMState], 0L, 2L, False,
                           wmatom[WMState], &real, &format, &n, &extra,
                           (unsigned char**)&p) != Success) {
        return -1;
    }
    if (n != 0)
        result = *p;

    XFree(p);
    return result;
}

Atom getXAtomProperty(Window window, Atom prop) {
    int actualFormatReturn;
    unsigned long nitemsReturn, bytesAfterReturn;
    unsigned char* outProperty = nullptr;
    Atom actualTypeReturn = None;
    if (XGetWindowProperty(dpy, window, prop, 0L, sizeof(prop), False, XA_ATOM,
                           &actualTypeReturn, &actualFormatReturn,
                           &nitemsReturn, &bytesAfterReturn,
                           &outProperty) == Success &&
        outProperty && nitemsReturn != 0) {

        const auto atom = *(Atom*)outProperty;
        XFree(outProperty);
        return atom;
    }
    return None;
}

int getXTextProperties(Window window, Atom atom, char* text, uint size) {
    if (!text || size == 0)
        return 0;
    text[0] = '\0';

    XTextProperty name{};
    if (!XGetTextProperty(dpy, window, &name, atom) || !name.nitems)
        return 0;
    if (name.encoding == XA_STRING) {
        strncpy(text, (char*)name.value, size - 1);
    } else {
        char** list = nullptr;
        if (int n;
            XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success &&
            n > 0 && *list) {
            strncpy(text, *list, size - 1);
            XFreeStringList(list);
        }
    }
    text[size - 1] = '\0';
    XFree(name.value);
    return 1;
}

int getrootptr(int* x, int* y) {
    int di;
    uint dui;
    Window dummy{};
    return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

Monitor* recttomon(const Rect& rect) {
    Monitor* r = selmon;
    int area = 0;
    for (const auto& monitor : allMonitors) {
        if (int a = rect.getIntersection(monitor->wRect); a > area) {
            area = a;
            r = monitor.get();
        }
    }
    return r;
}

Client* wintoclient(Window window) {
    for (const auto& monitor : allMonitors) {
        if (auto* client = monitor->getClientFromWindowID(window); client)
            return client;
    }
    return nullptr;
}

Monitor* wintomon(Window w) {
    int x, y;
    if (w == root && getrootptr(&x, &y))
        return recttomon({x, y, 1, 1});

    for (const auto& monitor : allMonitors) {
        if (w == monitor->fBarID)
            return monitor.get();
    }
    if (Client* client = wintoclient(w); client)
        return client->fMonitor;

    return selmon;
}

int getMonitorIndex(Monitor* monitor) {
    // TODO: This is a complexity regression for forward monitor navigation
    for (size_t i = 0; i < allMonitors.size(); i++) {
        if (allMonitors[i].get() == monitor)
            return i;
    }
    return 0; // TODO: throw here
}

Monitor* dirtomon(int dir) {
    int newIndex = (getMonitorIndex(selmon) + dir + allMonitors.size()) %
                   allMonitors.size();

    return allMonitors[newIndex].get();
}

#ifdef XINERAMA
bool isUniqueGeometry(XineramaScreenInfo* unique, size_t n,
                      XineramaScreenInfo* info) {
    while (n--) {
        if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org &&
            unique[n].width == info->width &&
            unique[n].height == info->height) {
            return false;
        }
    }
    return true;
}
#endif /* XINERAMA */

int updateDisplayGeometry() {
    bool dirty = false;

#ifdef XINERAMA
    if (XineramaIsActive(dpy)) {
        int i, j, xMonitorCount;
        int n = allMonitors.size();
        XineramaScreenInfo* info = XineramaQueryScreens(dpy, &xMonitorCount);

        /* only consider unique geometries as separate screens */
        XineramaScreenInfo* unique = new XineramaScreenInfo[xMonitorCount];
        for (i = 0, j = 0; i < xMonitorCount; i++) {
            if (isUniqueGeometry(unique, j, &info[i]))
                memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
        }
        XFree(info);

        xMonitorCount = j;
        if (n <= xMonitorCount) { /* new monitors available */
            for (int i = 0; i < (xMonitorCount - n); i++)
                allMonitors.emplace_back(std::make_unique<Monitor>(n + i));

            for (int i = 0; i < xMonitorCount; i++) {
                auto& m = allMonitors[i];
                if (i >= n || unique[i].x_org != m->sRect.x ||
                    unique[i].y_org != m->sRect.y ||
                    unique[i].width != m->sRect.width ||
                    unique[i].height != m->sRect.height) {
                    dirty = true;
                    m->sRect.x = m->wRect.x = unique[i].x_org;
                    m->sRect.y = m->wRect.y = unique[i].y_org;
                    m->sRect.width = m->wRect.width = unique[i].width;
                    m->sRect.height = m->wRect.height = unique[i].height;
                    m->updateBarPosition();
                }
            }
        } else { /* less monitors available xMonitorCount < n */
            dirty = true;
            auto& firstMonitor = *allMonitors.front();
            selmon = &firstMonitor;
            for (int i = xMonitorCount; i < n; i++) {
                auto& excessMonitor = allMonitors.back();
                excessMonitor->transferAllClients(firstMonitor);
                allMonitors.pop_back();
            }
        }
        delete[] unique;
    } else
#endif /* XINERAMA */
    {  /* default monitor setup */
        if (allMonitors.empty())
            allMonitors.emplace_back(std::make_unique<Monitor>(0));

        auto& monitor = *allMonitors.front();
        if (monitor.sRect.width != screenWidth ||
            monitor.sRect.height != screenHeight) {
            dirty = true;
            monitor.sRect.width = monitor.wRect.width = screenWidth;
            monitor.sRect.height = monitor.wRect.height = screenHeight;
            monitor.updateBarPosition();
        }
    }
    if (dirty) {
        selmon = allMonitors.front().get();
        selmon = wintomon(root);
    }
    return dirty;
}

void updateBarsXWindows() {
    // TODO: move this into Monitor constructor
    XSetWindowAttributes wa{};
    wa.background_pixmap = ParentRelative;
    wa.event_mask = ButtonPressMask | ExposureMask;
    wa.override_redirect = True;

    XClassHint* hint = XAllocClassHint();
    hint->res_class = dwmClassHint;
    hint->res_name = dwmClassHint;

    for (auto& monitor : allMonitors) {
        if (monitor->fBarID)
            continue;
        monitor->fBarID = XCreateWindow(
            dpy, root, monitor->wRect.x, monitor->fBarY, monitor->wRect.width,
            barHeight, 0, DefaultDepth(dpy, screen), CopyFromParent,
            DefaultVisual(dpy, screen),
            CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
        XDefineCursor(dpy, monitor->fBarID, cursors->normal.getXCursor());
        XMapRaised(dpy, monitor->fBarID);
        XSetClassHint(dpy, monitor->fBarID, hint);
    }
    XFree(hint);
}

void unfocus(Client* c, bool setfocus) {
    if (!c)
        return;
    c->grabXButtons(false);
    XSetWindowBorder(dpy, c->fWindow, scheme->normal.border.pixel);
    if (setfocus) {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        netatom->activeWindow.erase();
    }
}

void manageClient(Window window, XWindowAttributes* wa) {
    auto client = std::make_unique<Client>(
        window, Rect{wa->x, wa->y, wa->width, wa->height}, wa->border_width);

    auto clientPtr = client->fMonitor->attach(std::move(client));
    if (clientPtr->fMonitor == selmon)
        unfocus(selmon->fSelected, false);

    clientPtr->fMonitor->fSelected = clientPtr;
    clientPtr->fMonitor->arrangeClients();
    XMapWindow(dpy, clientPtr->fWindow);
    selmon->focus();
}

void arrangeAllMonitors() {
    for (const auto& monitor : allMonitors) {
        monitor->hideClientsIfInvisible();
        monitor->arrangeClients(false);
    }
}

void sendClientToMonitor(Client* client, Monitor* monitor) {
    if (client->fMonitor == monitor)
        return;
    unfocus(client, true);
    Client* clientPtr = monitor->attach(client->fMonitor->detach(client));
    clientPtr->fTags = monitor->getActiveTags();
    selmon->focus();
    arrangeAllMonitors();
}

void drawbars() {
    for (const auto& monitor : allMonitors)
        monitor->drawbar();
}

void updateStatusBarMessage() {
    if (!getXTextProperties(root, XA_WM_NAME, stext, sizeof(stext)))
        strcpy(stext, "dwm++-" VERSION);
    selmon->drawbar();
}

void updateAllXClientLists() {
    netatom->clientList.erase();
    for (const auto& monitor : allMonitors)
        monitor->updateXClientList();
}

Client::Client(Window win, const Rect& clientRect, int borderWidth)
    : fWindow{win}, fSize{clientRect}, fOldSize{clientRect},
      fBorderWidth{borderpx}, fOldBorderWidth{borderWidth},
      fXName{win, netatom->wmName}, fXState{win, netatom->wmState} {

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
        std::max(fSize.y, ((fMonitor->fBarY == fMonitor->sRect.y) &&
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

    netatom->clientList.append(fWindow);
    XMoveResizeWindow(dpy, fWindow, fSize.x + 2 * screenWidth, fSize.y,
                      fSize.width, fSize.height);
    setState(NormalState);
}

bool Client::isVisible() const { return fTags & fMonitor->getActiveTags(); }

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
        !fMonitor->getActiveLayout()->arrange) {
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
                if (!fFlags.isFloating && selmon->getActiveLayout()->arrange &&
                    (std::abs(newWidth - fSize.width) > snap ||
                     std::abs(newHeight - fSize.height) > snap)) {
                    togglefloating();
                }
            }
            if (!selmon->getActiveLayout()->arrange || fFlags.isFloating)
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
        sendClientToMonitor(this, monitor);
        selmon = monitor;
        selmon->focus();
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

            if (std::abs(selmon->wRect.x - newX - selmon->fGapSize) < snap) {
                newX = selmon->wRect.x + selmon->fGapSize;
            } else if (std::abs((selmon->wRect.x + selmon->wRect.width) -
                                (newX + getOuterWidth() + selmon->fGapSize)) <
                       snap) {
                newX = selmon->wRect.x + selmon->wRect.width - getOuterWidth() -
                       selmon->fGapSize;
            }
            if (std::abs(selmon->wRect.y - newY - selmon->fGapSize) < snap) {
                newY = selmon->wRect.y + selmon->fGapSize;
            } else if (std::abs((selmon->wRect.y + selmon->wRect.height) -
                                (newY + getOuterHeight() + selmon->fGapSize)) <
                       snap) {
                newY = selmon->wRect.y + selmon->wRect.height -
                       getOuterHeight() - selmon->fGapSize;
            }
            if (!fFlags.isFloating && selmon->getActiveLayout()->arrange &&
                (std::abs(newX - fSize.x) > snap ||
                 std::abs(newY - fSize.y) > snap)) {
                togglefloating();
            }
            if (!selmon->getActiveLayout()->arrange || fFlags.isFloating)
                resize(newX, newY, fSize.width, fSize.height, true);
            break;
        }
    } while (event.type != ButtonRelease);

    XUngrabPointer(dpy, CurrentTime);

    if (Monitor* monitor = recttomon(fSize); monitor != selmon) {
        sendClientToMonitor(this, monitor);
        selmon = monitor;
        selmon->focus();
    }
}

void Client::hideXClientIfInvisible() {
    if (isVisible()) {
        XMoveWindow(dpy, fWindow, fSize.x, fSize.y);
        if ((!fMonitor->getActiveLayout()->arrange || fFlags.isFloating) &&
            !fFlags.isFullscreen) {
            resize(fSize.x, fSize.y, fSize.width, fSize.height, false);
        }
    } else {
        XMoveWindow(dpy, fWindow, getOuterWidth() * -2, fSize.y);
    }
}

void Client::setState(long state) const {
    fXState.overwrite({state, None}, fXState);
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
        netatom->activeWindow.overwrite({fWindow});
    }
    sendXEvent(wmatom[WMTakeFocus]);
}

void Client::setFullscreen(const bool fullscreen) {
    if (fullscreen && !fFlags.isFullscreen) {
        fXState.overwrite({static_cast<Atom>(netatom->wmFullscreen)});
        fFlags.wasPreviouslyFloating = fFlags.isFloating;
        fFlags.isFullscreen = true;
        fFlags.isFloating = true;
        fOldBorderWidth = fBorderWidth;
        fBorderWidth = 0;

        resizeXClient(fMonitor->sRect);
        XRaiseWindow(dpy, fWindow);
    } else if (!fullscreen && fFlags.isFullscreen) {
        fXState.overwriteWithNullValue();
        fFlags.isFullscreen = false;
        fFlags.isFloating = fFlags.wasPreviouslyFloating;
        fSize = fOldSize;
        fBorderWidth = fOldBorderWidth;

        resizeXClient(fSize);
        fMonitor->arrangeClients();
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

void Client::updatePropertyFromEvent(Atom property) {
    switch (property) {
    case XA_WM_TRANSIENT_FOR:
        if (Window trans;
            !fFlags.isFloating &&
            (XGetTransientForHint(dpy, fWindow, &trans)) &&
            (fFlags.isFloating = (wintoclient(trans)) != nullptr)) {

            fMonitor->arrangeClients();
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
    if (property == XA_WM_NAME || property == netatom->wmName) {
        updateWindowTitleFromX();
        if (this == fMonitor->fSelected)
            fMonitor->drawbar();
    }
    if (property == netatom->wmWindowType)
        updateWindowTypeFromX();
}

void Client::grabXButtons(bool focused) const {
    updateNumLockMask();
    const std::array<uint, 4> modifiers{0, LockMask, numlockmask,
                                        numlockmask | LockMask};

    XUngrabButton(dpy, AnyButton, AnyModifier, fWindow);
    if (!focused) {
        XGrabButton(dpy, AnyButton, AnyModifier, fWindow, False, BUTTONMASK,
                    GrabModeSync, GrabModeSync, None, None);
    }
    for (const auto& button : buttons) {
        if (button.click != ClkClientWin)
            continue;
        for (const auto& modifier : modifiers) {
            XGrabButton(dpy, button.button, button.mask | modifier, fWindow,
                        False, BUTTONMASK, GrabModeAsync, GrabModeSync, None,
                        None);
        }
    }
}

void Client::requestKill() const {
    if (!sendXEvent(wmatom[WMDelete])) {
        XGrabServer(dpy);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(dpy, DestroyAll);
        XKillClient(dpy, fWindow);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
}

void Client::handleConfigurationRequest(XConfigureRequestEvent* event) {
    if (event->value_mask & CWBorderWidth) {
        fBorderWidth = event->border_width;
    } else if (fFlags.isFloating || !selmon->getActiveLayout()->arrange) {
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

            for (const auto& monitor : allMonitors) {
                if (monitor->getMonitorNumber() == rule.monitor) {
                    fMonitor = monitor.get();
                    break;
                }
            }
        }
    }
    if (classHint.res_class)
        XFree(classHint.res_class);
    if (classHint.res_name)
        XFree(classHint.res_name);

    fTags = fTags & TAGMASK ? fTags & TAGMASK : fMonitor->getActiveTags();
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

void Client::updateWindowTitleFromX() {
    if (!getXTextProperties(fWindow, netatom->wmName, fName, sizeof(fName))) {
        getXTextProperties(fWindow, XA_WM_NAME, fName, sizeof(fName));
    }
    if (fName[0] == '\0') /* hack to mark broken clients */
        strcpy(fName, broken);
}

void Client::updateWindowTypeFromX() {
    Atom state = getXAtomProperty(fWindow, netatom->wmState);
    Atom wtype = getXAtomProperty(fWindow, netatom->wmWindowType);

    if (state == netatom->wmFullscreen)
        setFullscreen(true);
    if (wtype == netatom->wmWindowTypeDialog)
        fFlags.isFloating = true;
}

void Client::updateWMHintsTypeFromX() {
    if (XWMHints* wmHints = XGetWMHints(dpy, fWindow); wmHints) {
        if (this == selmon->fSelected && wmHints->flags & XUrgencyHint) {
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

Monitor::Monitor(int num)
    : fGapSize{::gappx}, fMonitorNumber{num}, fMasterFactor{::mfact},
      fMasterCount{::nmaster}, fShouldRenderBar{::showbar},
      fShouldRenderBarOnTop{::topbar} {
    fTags[0] = fTags[1] = 1;
    fLayouts[0] = &layouts[0];
    fLayouts[1] = &layouts[1 % layouts.size()];
    strncpy(fLayoutSymbol, layouts[0].symbol, sizeof(fLayoutSymbol));
}

Monitor::~Monitor() {
    // TODO: improve this performance hack from original dwm
    Layout emptyLayout = {"", nullptr};
    fLayouts[fSelectedTags] = &emptyLayout;
    while (!fStack.empty()) {
        auto client = detach(fStack.front());
        client->unmanageAndDestroyX();
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        netatom->activeWindow.erase();
    }
    XUnmapWindow(dpy, fBarID);
    XDestroyWindow(dpy, fBarID);
}

bool Monitor::isSelectedMonitor() const { return this == selmon; };

int Monitor::getMonitorNumber() const { return fMonitorNumber; };

Client* Monitor::getClientFromWindowID(Window win) const {
    auto client = std::ranges::find_if(
        fClients, [=](const auto& client) { return client->fWindow == win; });
    if (client == fClients.end())
        return nullptr;
    return client->get();
}

void Monitor::incrementMasterCount(int amount) {
    fMasterCount = std::max(fMasterCount + amount, 0);
    arrangeClients();
}

void Monitor::incrementMasterFactor(float amount) {
    fMasterFactor = std::clamp(fMasterFactor + amount, 0.05f, 0.95f);
    arrangeClients();
}

uint Monitor::getActiveTags() const { return fTags[fSelectedTags]; }

void Monitor::setActiveTags(uint tags) { fTags[fSelectedTags] = tags; };

const Layout* Monitor::getActiveLayout() const {
    return fLayouts[fSelectedLayout];
}

void Monitor::setActiveLayout(const Layout* layout) {
    if (layout)
        fLayouts[fSelectedLayout] = layout;
    strncpy(fLayoutSymbol, getActiveLayout()->symbol, sizeof(fLayoutSymbol));
    if (fSelected) // TODO: why does this exists?
        arrangeClients();
    else
        drawbar();
}

void Monitor::toggleSelectedTagSet() { fSelectedTags ^= 1; }

void Monitor::toggleSelectedLayout() { fSelectedLayout ^= 1; }

auto Monitor::getTiledClients() const {
    return std::views::filter(fClients, [](const auto& client) {
        return !client->getFlags().isFloating && client->isVisible();
    });
}

auto Monitor::findClientLocation(Client* client) {
    return std::ranges::find_if(
        fClients, [=](const auto& ptr) { return client == ptr.get(); });
}

void Monitor::transferAllClients(Monitor& target) {
    target.fStack.insert(target.fStack.end(), fStack.begin(), fStack.end());
    target.fClients.insert(target.fClients.end(),
                           std::make_move_iterator(fClients.begin()),
                           std::make_move_iterator(fClients.end()));
    fStack.clear();
    fClients.clear();
    fSelected = nullptr;
}

Client* Monitor::attach(std::unique_ptr<Client> client) {
    // TODO: use push_back instead of emulating the linked list
    auto ptr = fClients.insert(fClients.begin(), std::move(client))->get();
    ptr->fMonitor = this;
    fStack.insert(fStack.begin(), ptr);
    return ptr;
}

std::unique_ptr<Client> Monitor::detach(Client* client) {
    auto clientLocation = findClientLocation(client);
    auto clientContainer = std::move(*clientLocation);
    fClients.erase(clientLocation);

    fStack.erase(std::ranges::find(fStack, client));
    if (client == fSelected) {
        auto newSelection = std::ranges::find_if(
            fStack, [](const auto& client) { return client->isVisible(); });

        fSelected = newSelection == fStack.end() ? nullptr : *newSelection;
    }
    return clientContainer;
}

void Monitor::unmanage(Client* ptr, bool xResourceDestroyed) {
    {
        auto client = detach(ptr);
        if (!xResourceDestroyed)
            client->unmanageAndDestroyX();
    }
    selmon->focus();
    updateAllXClientLists();
    arrangeClients();
}

void Monitor::hideClientsIfInvisible() const {
    for (auto& client : fStack) {
        if (client->isVisible())
            client->hideXClientIfInvisible();
    }
    for (auto& client : std::views::reverse(fStack)) {
        if (!client->isVisible())
            client->hideXClientIfInvisible();
    }
}

void Monitor::focus(Client* client) {
    selmon = this;

    if (!client || !client->isVisible()) {
        auto loc = std::ranges::find_if(
            fStack, [](const auto& client) { return client->isVisible(); });
        if (loc != fStack.end())
            client = *loc;
    }
    if (fSelected && fSelected != client)
        unfocus(fSelected, false);

    if (client) {
        if (client->getFlags().isUrgent)
            client->setUrgent(false);

        shuffleToFront(fStack, std::ranges::find(fStack, client));
        client->grabXButtons(true);
        XSetWindowBorder(dpy, client->fWindow, scheme->selected.border.pixel);
        client->setFocus();
    } else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        netatom->activeWindow.erase();
    }
    fSelected = client;
    drawbars();
}

void Monitor::shiftFocusThroughStack(int direction) {
    if (!fSelected || (fSelected->getFlags().isFullscreen & lockfullscreen))
        return;

    Client* c = nullptr;
    if (direction > 0) {
        // TODO: regression: previously O(1) if fSelected was visible
        for (auto client = ++findClientLocation(fSelected);
             client != fClients.end(); ++client) {
            if ((*client)->isVisible()) {
                c = client->get();
                break;
            }
        }
        if (!c) {
            for (auto& client : fClients) {
                if (client->isVisible()) {
                    c = client.get();
                    break;
                }
            }
        }
    } else {
        for (auto& client : fClients) {
            if (fSelected == client.get() && c)
                break;
            if (client->isVisible())
                c = client.get();
        }
    }
    if (c) {
        focus(c);
        restackClients();
    }
}

void Monitor::zoomClientToMaster(Client* client) {
    if (!getActiveLayout()->arrange || client->getFlags().isFloating)
        return;

    if (auto tiledClients = getTiledClients();
        tiledClients && client == tiledClients.front().get()) {
        // TODO: this is a complexity regression
        auto nextTiledClient =
            std::views::drop_while(tiledClients, [=](const auto& tiledClient) {
                return tiledClient.get() != client;
            });
        if (!nextTiledClient)
            return;

        client = nextTiledClient.front().get();
    }
    shuffleToFront(fClients, findClientLocation(client));
    focus(client);
    arrangeClients();
}

void Monitor::restackClients() const {
    drawbar();
    if (!fSelected)
        return;
    if (fSelected->getFlags().isFloating || !getActiveLayout()->arrange)
        XRaiseWindow(dpy, fSelected->fWindow);
    if (getActiveLayout()->arrange) {
        XWindowChanges windowChanges{};
        windowChanges.stack_mode = Below;
        windowChanges.sibling = fBarID;
        for (const auto* client : fStack) {
            if (client->getFlags().isFloating || !client->isVisible())
                continue;

            XConfigureWindow(dpy, client->fWindow, CWSibling | CWStackMode,
                             &windowChanges);
            windowChanges.sibling = client->fWindow;
        }
    }
    XEvent event{};
    XSync(dpy, False);
    while (XCheckMaskEvent(dpy, EnterWindowMask, &event)) {
    }
}

void Monitor::arrangeClients(bool shouldRestack) {
    hideClientsIfInvisible();

    strncpy(fLayoutSymbol, getActiveLayout()->symbol, sizeof(fLayoutSymbol));
    if (getActiveLayout()->arrange)
        getActiveLayout()->arrange(this);

    if (shouldRestack)
        restackClients();
}

void Monitor::updateBarPosition() {
    wRect.y = sRect.y;
    wRect.height = sRect.height;
    if (fShouldRenderBar) {
        wRect.height -= barHeight;
        fBarY = fShouldRenderBarOnTop ? wRect.y : wRect.y + wRect.height;
        wRect.y = fShouldRenderBarOnTop ? wRect.y + barHeight : wRect.y;
    } else {
        fBarY = -barHeight;
    }
}

void Monitor::drawbar() const {
    int tw = 0;
    int boxs = drw->getPrimaryFontHeight() / 9;
    int boxw = drw->getPrimaryFontHeight() / 6 + 2;
    uint occ = 0, urg = 0;

    /* draw status first so it can be overdrawn by tags later */
    if (isSelectedMonitor()) { /* status is only drawn on selected monitor */
        drw->setScheme(scheme->normal);
        tw = drw->getTextWidth(stext) + 2; /* 2px right padding */
        drw->renderText(wRect.width - tw, 0, tw, barHeight, 0, stext, 0);
    }

    for (const auto& client : fClients) {
        occ |= client->fTags;
        if (client->getFlags().isUrgent)
            urg |= client->fTags;
    }
    int x = 0;
    for (size_t i = 0; i < tags.size(); i++) {
        auto w = drw->getTextWidth(tags[i]) + lrpad;
        drw->setScheme(fTags[fSelectedTags] & 1 << i ? scheme->selected
                                                     : scheme->normal);
        drw->renderText(x, 0, w, barHeight, lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i) {
            drw->renderRect(x + boxs, boxs, boxw, boxw,
                            isSelectedMonitor() && fSelected &&
                                fSelected->fTags & 1 << i,
                            urg & 1 << i);
        }
        x += w;
    }
    int w = blw = drw->getTextWidth(fLayoutSymbol) + lrpad;
    drw->setScheme(scheme->normal);
    x = drw->renderText(x, 0, w, barHeight, lrpad / 2, fLayoutSymbol, 0);

    if ((w = wRect.width - tw - x) > barHeight) {
        if (fSelected) {
            drw->setScheme(isSelectedMonitor() ? scheme->selected
                                               : scheme->normal);
            drw->renderText(x, 0, w, barHeight, lrpad / 2, fSelected->getName(),
                            0);
            if (fSelected->getFlags().isFloating) {
                drw->renderRect(x + boxs, boxs, boxw, boxw,
                                fSelected->getFlags().isFixed, 0);
            }
        } else {
            drw->setScheme(scheme->normal);
            drw->renderRect(x, 0, w, barHeight, 1, 1);
        }
    }
    drw->map(fBarID, 0, 0, wRect.width, barHeight);
}

void Monitor::toggleBarRendering() {
    fShouldRenderBar = !fShouldRenderBar;
    updateBarPosition();
    XMoveResizeWindow(dpy, fBarID, wRect.x, fBarY, wRect.width, barHeight);
    arrangeClients();
}

void Monitor::updateXClientList() const {
    for (const auto& client : fClients)
        netatom->clientList.append(client->fWindow);
}

void Monitor::updateXGeometry() const {
    for (auto& client : fClients) {
        if (client->getFlags().isFullscreen)
            client->resizeXClient(sRect);
    }
    XMoveResizeWindow(dpy, fBarID, wRect.x, fBarY, wRect.width, barHeight);
}

void Monitor::monocle() {
    int n = std::ranges::count_if(
        fClients, [](const auto& client) { return client->isVisible(); });
    if (n > 0) /* override layout symbol */
        snprintf(fLayoutSymbol, sizeof(fLayoutSymbol), "[%d]", n);

    for (auto& client : getTiledClients()) {
        client->resize(wRect.x, wRect.y,
                       wRect.width - 2 * client->getBorderWidth(),
                       wRect.height - 2 * client->getBorderWidth(), false);
    }
}

void Monitor::tile() {
    int n = std::ranges::count_if(getTiledClients(),
                                  [](const auto&) { return true; });
    if (n == 0)
        return;

    int mw;
    if (n > fMasterCount)
        mw = fMasterCount ? wRect.width * fMasterFactor : 0;
    else
        mw = wRect.width - fGapSize;

    int i = 0, my = fGapSize, ty = fGapSize;
    for (const auto& c : getTiledClients()) {
        if (i < fMasterCount) { // Master window
            auto h = (wRect.height - my) / (std::min(n, fMasterCount) - i) -
                     fGapSize;
            c->resize(wRect.x + fGapSize, wRect.y + my,
                      mw - (2 * c->getBorderWidth()) - fGapSize,
                      h - (2 * c->getBorderWidth()), false);
            if (my + c->getOuterHeight() + fGapSize < wRect.height)
                my += c->getOuterHeight() + fGapSize;
        } else { // Stack window
            auto h = (wRect.height - ty) / (n - i) - fGapSize;
            c->resize(wRect.x + mw + fGapSize, wRect.y + ty,
                      wRect.width - mw - (2 * c->getBorderWidth()) -
                          2 * fGapSize,
                      h - (2 * c->getBorderWidth()), false);
            if (ty + c->getOuterHeight() + fGapSize < wRect.height)
                ty += c->getOuterHeight() + fGapSize;
        }
        i++;
    }
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent* e) {
    XFocusChangeEvent* ev = &e->xfocus;
    if (selmon->fSelected && ev->window != selmon->fSelected->fWindow)
        selmon->fSelected->setFocus();
}

void buttonpress(XEvent* e) {
    XButtonPressedEvent* ev = &e->xbutton;
    /* focus monitor if necessary */
    if (Monitor* m = wintomon(ev->window); m && m != selmon) {
        unfocus(selmon->fSelected, true);
        selmon = m;
        selmon->focus();
    }

    uint clickedTag = 0u;
    auto click = ClkRootWin;
    if (ev->window == selmon->fBarID) {
        int x = 0;
        uint i = 0;
        do {
            x += drw->getTextWidth(tags[i]) + lrpad;
        } while (ev->x >= x && ++i < tags.size());
        if (i < tags.size()) {
            click = ClkTagBar;
            clickedTag = 1 << i;
        } else if (ev->x < x + blw) {
            click = ClkLtSymbol;
        } else if (ev->x >
                   selmon->wRect.width - (drw->getTextWidth(stext) + lrpad)) {
            click = ClkStatusText;
        } else {
            click = ClkWinTitle;
        }
    } else if (Client* c = wintoclient(ev->window)) {
        c->fMonitor->focus(c);
        c->fMonitor->restackClients();
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }
    for (const auto& button : buttons) {
        if (click == button.click && button.button == ev->button &&
            CLEANMASK(button.mask) == CLEANMASK(ev->state)) {
            button.action(click == ClkTagBar ? clickedTag : 0u);
        }
    }
}

void clientmessage(XEvent* e) {
    XClientMessageEvent* cme = &e->xclient;
    Client* c = wintoclient(cme->window);
    if (!c)
        return;

    if (cme->message_type == netatom->wmState) {
        if (static_cast<unsigned long>(cme->data.l[1]) ==
                netatom->wmFullscreen ||
            static_cast<unsigned long>(cme->data.l[2]) ==
                netatom->wmFullscreen) {
            c->setFullscreen((cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                              ||
                              (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
                               !c->getFlags().isFullscreen)));
        }
    } else if (cme->message_type == netatom->activeWindow) {
        if (c != selmon->fSelected && !c->getFlags().isUrgent)
            c->setUrgent(true);
    }
}

void configurenotify(XEvent* e) {
    XConfigureEvent* ev = &e->xconfigure;

    /* TODO: updategeom handling sucks, needs to be simplified */
    if (ev->window == root) {
        bool dirty = (screenWidth != ev->width || screenHeight != ev->height);
        screenWidth = ev->width;
        screenHeight = ev->height;
        if (updateDisplayGeometry() || dirty) {
            drw->resize(screenWidth, barHeight);
            updateBarsXWindows();
            for (const auto& monitor : allMonitors)
                monitor->updateXGeometry();
            selmon->focus();
            arrangeAllMonitors();
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

void destroynotify(XEvent* e) {
    XDestroyWindowEvent* ev = &e->xdestroywindow;
    if (Client* client = wintoclient(ev->window); client)
        client->fMonitor->unmanage(client, true);
}

void enternotify(XEvent* e) {
    XCrossingEvent* ev = &e->xcrossing;
    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) &&
        ev->window != root) {
        return;
    }

    Client* c = wintoclient(ev->window);
    Monitor* m = c ? c->fMonitor : wintomon(ev->window);
    if (m != selmon) {
        unfocus(selmon->fSelected, true);
        selmon = m;
    } else if (!c || c == selmon->fSelected) {
        return;
    }
    m->focus(c);
}

void expose(XEvent* e) {
    XExposeEvent* ev = &e->xexpose;
    if (Monitor * m; ev->count == 0 && (m = wintomon(ev->window)))
        m->drawbar();
}

void keypress(XEvent* e) {
    XKeyEvent* ev;
    ev = &e->xkey;
    const auto keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
    for (const auto& key : keys) {
        if (keysym == key.keysym &&
            CLEANMASK(key.mod) == CLEANMASK(ev->state)) {
            key.func();
        }
    }
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
    if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
        return;
    if (!wintoclient(ev->window))
        manageClient(ev->window, &wa);
}

void motionnotify(XEvent* e) {
    static Monitor* mon = nullptr;
    XMotionEvent* ev = &e->xmotion;
    if (ev->window != root)
        return;

    Monitor* monitor = recttomon({ev->x_root, ev->y_root, 1, 1});
    if (monitor != mon && mon) {
        unfocus(selmon->fSelected, true);
        selmon = monitor;
        selmon->focus();
    }
    mon = monitor;
}

void propertynotify(XEvent* e) {
    XPropertyEvent* ev = &e->xproperty;
    if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
        updateStatusBarMessage();
    } else if (ev->state == PropertyDelete) {
        return; /* ignore */
    } else if (Client* c = wintoclient(ev->window); c) {
        c->updatePropertyFromEvent(ev->atom);
    }
}

void unmapnotify(XEvent* e) {
    XUnmapEvent* ev = &e->xunmap;
    if (Client* c = wintoclient(ev->window); c) {
        if (ev->send_event)
            c->setState(WithdrawnState);
        else
            c->fMonitor->unmanage(c, false);
    }
}

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

/* User config */
void focusmon(const int dir) {
    if (allMonitors.size() <= 1)
        return;

    if (Monitor* m = dirtomon(dir); m != selmon) {
        unfocus(selmon->fSelected, false);
        selmon = m;
        selmon->focus();
    }
}

void focusstack(const int dir) { selmon->shiftFocusThroughStack(dir); }

void incnmaster(const int dir) { selmon->incrementMasterCount(dir); }

void killclient() {
    if (selmon->fSelected)
        selmon->fSelected->requestKill();
}

void movemouse() {
    if (Client* client = selmon->fSelected;
        client && !client->getFlags().isFullscreen) {
        selmon->restackClients();
        client->moveWithMouse();
    }
}

void quit() { running = 0; }

void resizemouse() {
    if (Client* client = selmon->fSelected;
        client && !client->getFlags().isFullscreen) {
        selmon->restackClients();
        client->resizeWithMouse();
    }
}

void setgaps(const int inc) {
    if ((inc == 0) || (selmon->fGapSize + inc < 0))
        selmon->fGapSize = 0;
    else
        selmon->fGapSize += inc;
    selmon->arrangeClients();
}

void setlayout(const Layout* layout) {
    if (!layout || layout != selmon->getActiveLayout())
        selmon->toggleSelectedLayout();
    selmon->setActiveLayout(layout);
}

void setmfact(const float factor) {
    if (selmon->getActiveLayout()->arrange)
        selmon->incrementMasterFactor(factor);
}

void spawn(CommandPtr command) {
    spawnCommandMonitorID[0] = '0' + selmon->getMonitorNumber();
    if (fork() == 0) {
        if (dpy)
            close(ConnectionNumber(dpy));
        setsid();
        execvp(command.data[0], const_cast<char* const*>(command.data));
        fprintf(stderr, "dwm++: execvp %s", command.data[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}

void tag(const uint tag) {
    if (selmon->fSelected && tag & TAGMASK) {
        selmon->fSelected->fTags = tag & TAGMASK;
        selmon->focus();
        selmon->arrangeClients();
    }
}

void tagmon(const int dir) {
    if (selmon->fSelected && allMonitors.size() > 1)
        sendClientToMonitor(selmon->fSelected, dirtomon(dir));
}

void togglebar() { selmon->toggleBarRendering(); }

void togglefloating() {
    if (selmon->fSelected) {
        selmon->fSelected->toggleFloating();
        selmon->arrangeClients();
    }
}

void togglelayout() {
    selmon->toggleSelectedLayout();
    selmon->setActiveLayout(nullptr);
}

void toggletag(const uint tag) {
    if (!selmon->fSelected)
        return;

    auto newtags = selmon->fSelected->fTags ^ (tag & TAGMASK);
    if (newtags) {
        selmon->fSelected->fTags = newtags;
        selmon->focus();
        selmon->arrangeClients();
    }
}

void toggleview(const uint tag) {
    uint newtagset = selmon->getActiveTags() ^ (tag & TAGMASK);
    if (newtagset) {
        selmon->setActiveTags(newtagset);
        selmon->focus();
        selmon->arrangeClients();
    }
}

void view(const uint tag) {
    if ((tag & TAGMASK) == selmon->getActiveTags())
        return;
    selmon->toggleSelectedTagSet();
    if (tag & TAGMASK)
        selmon->setActiveTags(tag & TAGMASK);
    selmon->focus();
    selmon->arrangeClients();
}

void zoom() {
    if (selmon->fSelected)
        selmon->zoomClientToMaster(selmon->fSelected);
}

void tile(Monitor* m) { m->tile(); }

void monocle(Monitor* m) { m->monocle(); }

/* Setup */
void checkotherwm() {
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}

void sigchld(int) {
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        die("can't install SIGCHLD handler:");
    while (0 < waitpid(-1, NULL, WNOHANG)) {
    }
}

void setup() {
    sigchld(0); /* clean up any zombies immediately */
    /* init screen */
    screen = DefaultScreen(dpy);
    screenWidth = DisplayWidth(dpy, screen);
    screenHeight = DisplayHeight(dpy, screen);
    root = RootWindow(dpy, screen);
    drw = new Drw{dpy, screen, root, static_cast<uint>(screenWidth),
                  static_cast<uint>(screenHeight)};
    if (drw->createFontSet(fonts).empty())
        die("no fonts could be loaded.");
    lrpad = drw->getPrimaryFontHeight();
    barHeight = drw->getPrimaryFontHeight() + 2;
    updateDisplayGeometry();
    /* init atoms */
    XNetPropertyFactory net{dpy, root};
    auto wmCheck = net.make<XProperty<XA_WINDOW>>("_NET_SUPPORTING_WM_CHECK");
    netatom = std::make_unique<Net_Properties>(Net_Properties{
        .activeWindow = net.makeManaged<XA_WINDOW>("_NET_ACTIVE_WINDOW"),
        .clientList = net.makeManaged<XA_WINDOW>("_NET_CLIENT_LIST"),
        .wmName = net.make<XProperty<XA_TEXT>>("_NET_WM_NAME"),
        .wmState = net.make<XProperty<XA_ATOM>>("_NET_WM_STATE"),
        .wmFullscreen = net.make<XSentinel>("_NET_WM_STATE_FULLSCREEN"),
        .wmWindowType = net.make<XSentinel>("_NET_WM_WINDOW_TYPE"),
        .wmWindowTypeDialog = net.make<XSentinel>("_NET_WM_WINDOW_TYPE_DIALOG"),
    });

    wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    /* init cursors */
    cursors.emplace(CursorTheme{
        .normal = {dpy, XC_left_ptr},
        .resizing = {dpy, XC_sizing},
        .moving = {dpy, XC_fleur},
    });
    /* init appearance */
    scheme = drw->parseTheme(colors);
    /* init bars */
    updateBarsXWindows();
    updateStatusBarMessage();
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    MutableXProperty<XA_WINDOW>{wmcheckwin, wmCheck}.overwrite({wmcheckwin});
    MutableTextXProperty{wmcheckwin, netatom->wmName}.overwrite(dwmClassHint);
    MutableXProperty<XA_WINDOW>{root, wmCheck}.overwrite({wmcheckwin});

    netatom->clientList.erase();
    /* select events */
    XSetWindowAttributes wa;
    wa.cursor = cursors->normal.getXCursor();
    wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                    ButtonPressMask | PointerMotionMask | EnterWindowMask |
                    LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
    XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
    XSelectInput(dpy, root, wa.event_mask);
    grabkeys();
    selmon->focus();
}

void run() {
    XEvent ev;
    XSync(dpy, False);
    autostart();
    while (running && !XNextEvent(dpy, &ev))
        handleXEvent(&ev); /* TODO: Ignore unhandled events */
}

void scanAndManageOpenClients() {
    Window d1, d2, *wins = nullptr;
    XWindowAttributes wa;
    if (uint num; XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (uint i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) ||
                wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable ||
                getXStateProperty(wins[i]) == IconicState)
                manageClient(wins[i], &wa);
        }
        for (uint i = 0; i < num; i++) { /* now the transients */
            if (!XGetWindowAttributes(dpy, wins[i], &wa))
                continue;
            if (XGetTransientForHint(dpy, wins[i], &d1) &&
                (wa.map_state == IsViewable ||
                 getXStateProperty(wins[i]) == IconicState)) {
                manageClient(wins[i], &wa);
            }
        }
        if (wins)
            XFree(wins);
    }
}

void cleanup() {
    view(~0u);
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    allMonitors.clear();
    XDestroyWindow(dpy, wmcheckwin);
    cursors.reset();
    delete drw; // TODO: this should be a unique pointer
    XSync(dpy, False);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    netatom.reset();
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1]))
        die("dwm++-" VERSION);
    else if (argc != 1)
        die("usage: dwm [-v]");
    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);
    if (!(dpy = XOpenDisplay(NULL)))
        die("dwm++: cannot open display");
    checkotherwm();
    setup();
    scanAndManageOpenClients();
    run();
    cleanup();
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}
