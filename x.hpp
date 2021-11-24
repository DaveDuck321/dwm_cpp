#pragma once

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <optional>
#include <string_view>

namespace {
const auto XA_TEXT = XA_LAST_PREDEFINED + 1;

class XSentinel {
  public:
    XSentinel(Display* dpy, const char* name)
        : fIdentity{XInternAtom(dpy, name, False)} {};
    explicit XSentinel(Atom identity) : fIdentity{identity} {};

    operator Atom() const { return fIdentity; }

  protected:
    Atom fIdentity;
};

template <Atom XType> class XProperty : public XSentinel {
  public:
    XProperty(Display* dpy, const char* name)
        : XSentinel{dpy, name}, fDisplay{dpy} {};

  protected:
    Display* fDisplay;
};

class MutableTextXProperty : public XProperty<XA_TEXT> {
  public:
    MutableTextXProperty(Window win, XProperty<XA_TEXT> identity)
        : XProperty<XA_TEXT>{identity}, fWindow{win} {}

    void overwrite(const std::string_view text) const {
        Atom utf8Type = XInternAtom(fDisplay, "UTF8_STRING", False);
        XChangeProperty(fDisplay, fWindow, fIdentity, utf8Type, 8,
                        PropModeReplace, (unsigned char*)text.data(),
                        text.size());
    }

  protected:
    Window fWindow;
};

template <Atom XType> class MutableXProperty : public XProperty<XType> {
  public:
    MutableXProperty(Window window, XProperty<XType> identity)
        : XProperty<XType>{identity}, fWindow{window} {}

    template <typename T, size_t L>
    void overwrite(const T (&data)[L], const Atom type = XType) const {
        updateProperty<PropModeReplace, L>(fWindow, data, type);
    }
    void overwriteWithNullValue(Atom nullVal = 0L) const {
        updateProperty<PropModeReplace, 0>(fWindow, &nullVal);
    }
    void append(const Atom data) const {
        updateProperty<PropModeAppend, 1>(fWindow, &data);
    }
    void erase() const {
        XDeleteProperty(this->fDisplay, fWindow, this->fIdentity);
    }

  protected:
    template <int Mode, size_t Length, typename T>
    void updateProperty(Window window, const T* data, Atom type = XType) const {
        static_assert(sizeof(T) == 8);
        XChangeProperty(this->fDisplay, window, this->fIdentity, type, 32, Mode,
                        (unsigned char*)data, Length);
    }

    Window fWindow;
};

template <Atom XType>
class MutableXPropertyWithCleanup : public MutableXProperty<XType> {
  public:
    MutableXPropertyWithCleanup(Window root, XProperty<XType> identity)
        : MutableXProperty<XType>{root, identity} {}
    MutableXPropertyWithCleanup(MutableXPropertyWithCleanup<XType>&& other)
        : MutableXProperty<XType>{std::move(other)} {
        other.fWindow = None;
    }
    ~MutableXPropertyWithCleanup() {
        if (this->fWindow)
            this->erase();
    }
};

class XNetPropertyFactory {
  public:
    XNetPropertyFactory(Display* dpy, Window root)
        : fXSupported{root, XProperty<XA_ATOM>{dpy, "_NET_SUPPORTED"}},
          fDisplay{dpy}, fRoot{root} {
        fXSupported.erase();
    }

    template <typename PropertyType> PropertyType make(const char* name) const {
        static_assert(std::is_convertible<PropertyType, XSentinel>::value);
        PropertyType property{fDisplay, name};
        fXSupported.append(property);
        return property;
    }

    template <Atom XType>
    MutableXPropertyWithCleanup<XType> makeManaged(const char* name) const {
        return {fRoot, make<XProperty<XType>>(name)};
    }

  private:
    MutableXProperty<XA_ATOM> fXSupported;
    Display* fDisplay;
    Window fRoot;
};
} // namespace
