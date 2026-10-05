// The immediate-mode toolkit declared in ui.hpp. How it is put together:
//  - Ids: FNV-1a over the pushed id stack plus the widget's own id; the same id in two frames is the same widget.
//  - Layout: a stack of panels; each has a cursor that moves down, optional rows (columns) and sameLine placement.
//  - Drawing: the base layer draws straight to the renderer; popups, modals and the palette record draw commands
//    into layers that end() replays in order, so they land above everything drawn later in the frame.
//  - Input ownership: overlay rects from the previous frame decide who owns the pointer this frame (the topmost
//    overlay containing it, or nobody while a modal is open); base widgets are blocked whenever an overlay is open.
//  - State: one small map keyed by id (scroll offsets, open flags, highlights) plus a single "edit" record for the
//    focused text or numeric field; nothing is allocated per frame once the vectors have grown to their size.
#include "ui.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace ui {

Theme& theme() { static Theme t; return t; }
static int g_lastFrameCalls = 0;   // renderer calls made by the last finished frame

namespace {

constexpr uint64_t kOutside = ~uint64_t(0);   // pointer owner while an overlay is open and the pointer is outside every overlay

uint64_t fnv(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}
uint64_t hashStr(uint64_t seed, const char* s) { return fnv(seed, s, std::strlen(s)); }
uint64_t hashInt(uint64_t seed, int v) { return fnv(seed, &v, sizeof v); }

bool contains(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
SDL_Rect intersect(const SDL_Rect& a, const SDL_Rect& b) {
    int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y), x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}
SDL_Rect inset(SDL_Rect r, int d) { return {r.x + d, r.y + d, std::max(0, r.w - 2 * d), std::max(0, r.h - 2 * d)}; }
SDL_Color col(uint32_t rgb, uint8_t a = 255) { return SDL_Color{uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), a}; }
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
double clampd(double v, double lo, double hi) { return !(v >= lo) ? lo : (v > hi ? hi : v); }   // NaN lands on lo

// How far each of the top `r` rows of a rounded corner is inset: pixel-art circles for small radii, geometry above.
void cornerInsets(int r, int* ins) {
    static const int t1[] = {1}, t2[] = {1, 0}, t3[] = {2, 1, 0}, t4[] = {2, 1, 0, 0};
    static const int* tables[] = {nullptr, t1, t2, t3, t4};
    if (r <= 4) { std::copy(tables[r], tables[r] + r, ins); return; }
    for (int i = 0; i < r; ++i) {
        double dy = r - i - 0.5;
        ins[i] = std::max(0, r - (int)std::floor(std::sqrt(double(r) * r - dy * dy) + 0.5));
    }
}
constexpr int kMaxRadius = 16;

// The rows that fill a rounded rect, as rects for one SDL_RenderFillRects call.
int roundedFill(SDL_Rect r, int radius, SDL_Rect* out) {
    radius = std::min({radius, r.w / 2, r.h / 2, kMaxRadius});
    if (radius <= 0) { out[0] = r; return 1; }
    int ins[kMaxRadius];
    cornerInsets(radius, ins);
    int n = 0;
    for (int i = 0; i < radius; ++i) {
        out[n++] = {r.x + ins[i], r.y + i, r.w - 2 * ins[i], 1};
        out[n++] = {r.x + ins[i], r.y + r.h - 1 - i, r.w - 2 * ins[i], 1};
    }
    out[n++] = {r.x, r.y + radius, r.w, r.h - 2 * radius};
    return n;
}

// The one-pixel outline of a rounded rect as rects: two horizontal edges, two vertical ones and the corner steps.
int roundedOutline(SDL_Rect r, int radius, SDL_Rect* out) {
    radius = std::min({radius, r.w / 2, r.h / 2, kMaxRadius});
    if (radius <= 0) {
        out[0] = {r.x, r.y, r.w, 1}; out[1] = {r.x, r.y + r.h - 1, r.w, 1};
        out[2] = {r.x, r.y + 1, 1, r.h - 2}; out[3] = {r.x + r.w - 1, r.y + 1, 1, r.h - 2};
        return 4;
    }
    int ins[kMaxRadius];
    cornerInsets(radius, ins);
    int n = 0, v0 = radius;
    out[n++] = {r.x + ins[0], r.y, r.w - 2 * ins[0], 1};
    out[n++] = {r.x + ins[0], r.y + r.h - 1, r.w - 2 * ins[0], 1};
    for (int i = 1; i < radius; ++i) {
        int a = ins[i], b = std::max(ins[i - 1], a + 1);   // the step from the row above, at least one pixel
        out[n++] = {r.x + a, r.y + i, b - a, 1}; out[n++] = {r.x + r.w - b, r.y + i, b - a, 1};
        out[n++] = {r.x + a, r.y + r.h - 1 - i, b - a, 1}; out[n++] = {r.x + r.w - b, r.y + r.h - 1 - i, b - a, 1};
        if (a == 0 && v0 == radius) v0 = i;
    }
    out[n++] = {r.x, r.y + v0, 1, r.h - 2 * v0};
    out[n++] = {r.x + r.w - 1, r.y + v0, 1, r.h - 2 * v0};
    return n;
}

// Evaluates "a op b op c" with + - * / (* and / bind tighter); numbers may carry a sign and a decimal point.
// False for anything else: empty input, a trailing operator, division by zero, a result that is not finite.
bool evalExpr(const std::string& s, double& out) {
    size_t i = 0;
    auto skip = [&] { while (i < s.size() && s[i] == ' ') ++i; };
    auto number = [&](double& v) {
        skip();
        size_t start = i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        bool digits = false;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) { ++i; digits = true; }
        if (i < s.size() && s[i] == '.') { ++i; while (i < s.size() && std::isdigit((unsigned char)s[i])) { ++i; digits = true; } }
        if (!digits) return false;
        v = std::strtod(s.substr(start, i - start).c_str(), nullptr);   // only the scanned text: strtod must not read "1e5" as 100000
        return true;
    };
    auto term = [&](double& acc) {   // number (* or / number)*
        if (!number(acc)) return false;
        for (;;) {
            skip();
            if (i >= s.size() || (s[i] != '*' && s[i] != '/')) return true;
            char op = s[i++];
            double rhs = 0;
            if (!number(rhs)) return false;
            if (op == '*') acc *= rhs;
            else { if (rhs == 0) return false; acc /= rhs; }
        }
    };
    double acc = 0;
    if (!term(acc)) return false;
    for (;;) {
        skip();
        if (i >= s.size()) break;
        char op = s[i++];
        double rhs = 0;
        if ((op != '+' && op != '-') || !term(rhs)) return false;
        acc += op == '+' ? rhs : -rhs;
    }
    if (!std::isfinite(acc)) return false;
    out = acc;
    return true;
}

std::string fmtNumber(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", std::max(0, decimals), v);
    std::string s = buf;
    if (s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);   // "-0.0" reads as 0.0
    return s;
}

// one UTF-8 sequence back / forward from a byte position
int prevChar(const std::string& s, int i) { if (i <= 0) return 0; --i; while (i > 0 && (s[i] & 0xC0) == 0x80) --i; return i; }
int nextChar(const std::string& s, int i) { int n = (int)s.size(); if (i >= n) return n; ++i; while (i < n && (s[i] & 0xC0) == 0x80) ++i; return i; }

bool hasKey(const Input& in, SDL_Keycode k) { return std::find(in.keys.begin(), in.keys.end(), k) != in.keys.end(); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------- state

enum class Op : uint8_t { Clip, Fill, Stroke, Text, Icon };
struct Cmd {
    Op op;
    bool bold = false;
    uint8_t a = 255;
    int16_t radius = 0;      // Fill/Stroke radius, Text/Icon scale
    SDL_Rect r{};            // rect, or x/y for text and icons (w = max text width, -1 for none)
    uint32_t rgb = 0;
    int str = -1;            // Text: index into Layer::strs
    font::Face face = font::Face::Ui;
    icons::Id icon = icons::None;
};
struct Layer {   // recorded drawing for one overlay, replayed by end()
    std::vector<Cmd> cmds;
    std::vector<std::string> strs;
    size_t nStr = 0;
    void reset() { cmds.clear(); nStr = 0; }
    int addStr(const std::string& s) {
        if (nStr < strs.size()) strs[nStr].assign(s); else strs.push_back(s);
        return (int)nStr++;
    }
};

struct Panel {
    uint64_t id = 0;
    SDL_Rect outer{}, inner{};
    int y = 0, startY = 0, pad = 0, scrollY = 0, maxBottom = 0;
    bool scroll = false, canScroll = false, sameLine = false;
    int rowCols = 0, rowIdx = 0, rowH = 0, rowY = 0;
    float weights[8] = {}, weightSum = 0;
    SDL_Rect last{};
};

struct WState {   // per-id memory: whichever fields the widget kind uses
    bool open = false, openInit = false, drag = false;
    int hl = -1, scrollY = 0, contentH = 0, dragOff = 0;
};

struct Overlay { uint64_t id; SDL_Rect r; bool modal; };
struct OpenOverlay { uint64_t id; bool modal, above; int layer; SDL_Rect r; int maxH, fillCmd, strokeCmd; size_t panelDepth, ovIdx; };

struct Hit { bool over = false, hover = false, pressed = false, down = false, released = false, clicked = false, dbl = false; };

struct Context::Impl {
    Theme& th = theme();
    SDL_Renderer* ren = nullptr;
    Input in;
    int winW = 0, winH = 0;
    int frameCalls = 0;

    std::vector<uint64_t> ids;
    std::vector<Panel> panels;
    std::vector<SDL_Rect> clips;
    std::unordered_map<uint64_t, WState> st;

    uint64_t active = 0, focus = 0, listFocus = 0;
    bool focusSeen = false, activeSeen = false, listFocusSeen = false, tabConsumed = false;
    std::vector<uint64_t> fieldsCur, fieldsPrev;
    struct { uint64_t id = 0; int startX = 0; double startV = 0; bool moved = false, dbl = false; uint32_t repeatAt = 0; } num;
    struct { uint64_t id = 0; std::string buf; int caret = 0; bool selAll = false; int scrollX = 0; } edit;   // the focused field's typing state
    bool editedFlag = false, listActivatedFlag = false, wheelConsumed = false, escConsumed = false, mouseOverUi = false;

    uint64_t lastId = 0; SDL_Rect lastRect{}; bool lastOver = false;
    uint64_t tipId = 0; SDL_Rect tipRect{}; std::string tipText, tipShortcut, hintText;
    uint64_t hoverPrev = 0; uint32_t hoverSince = 0; int hoverX = 0, hoverY = 0; bool tipShown = false, tipSuppressed = false;

    std::vector<Overlay> ovPrev, ovCur;
    std::vector<OpenOverlay> ovStack;
    uint64_t pointerOwner = 0;
    std::vector<Layer> layers;
    int layerCount = 0, curLayer = 0;

    std::vector<std::pair<int, int>> palRes;   // (sort key, command index)
    std::vector<std::string> mru;
    struct Toast { std::string text; uint32_t until; TextStyle style; };
    std::vector<Toast> toasts;

    // ---- helpers
    uint64_t seed() const { return ids.empty() ? 0xcbf29ce484222325ull : ids.back(); }
    uint64_t id(const char* s) const { return hashStr(seed(), s); }
    Panel& cur() { return panels.back(); }
    const SDL_Rect& clip() const { return clips.back(); }
    uint64_t layerOwner() const { return ovStack.empty() ? 0 : ovStack.back().id; }
    bool pointerAvail() const { return pointerOwner == layerOwner(); }
    // the keyboard goes to the topmost overlay of the previous frame, or to the base layer when none was open
    bool keysAvail() const { return layerOwner() == (ovPrev.empty() ? 0 : ovPrev.back().id); }
    int fontH() const { return font::height(th.face, th.fontScale); }
    int textW(const std::string& s, int scale = -1) const { return font::width(s, th.face, scale < 0 ? th.fontScale : scale); }
    int lineH() const { return fontH() + th.gap; }
    int ovIndex(uint64_t id) const { for (size_t i = 0; i < ovPrev.size(); ++i) if (ovPrev[i].id == id) return (int)i; return -1; }

    // ---- drawing: immediate for the base layer, recorded for overlays
    void exec(const Cmd& c, const std::string* s);
    void record(Cmd c) { layers[curLayer].cmds.push_back(c); }
    void fill(SDL_Rect r, uint32_t rgb, uint8_t a = 255, int radius = 0) {
        if (r.w <= 0 || r.h <= 0) return;
        Cmd c{Op::Fill}; c.r = r; c.rgb = rgb; c.a = a; c.radius = (int16_t)radius;
        if (curLayer) record(c); else exec(c, nullptr);
    }
    void stroke(SDL_Rect r, uint32_t rgb, uint8_t a = 255, int radius = 0) {
        if (r.w <= 0 || r.h <= 0) return;
        Cmd c{Op::Stroke}; c.r = r; c.rgb = rgb; c.a = a; c.radius = (int16_t)radius;
        if (curLayer) record(c); else exec(c, nullptr);
    }
    void ring(SDL_Rect r, uint32_t rgb) { stroke(r, rgb, 255, th.radius); stroke(inset(r, 1), rgb, 255, std::max(0, th.radius - 1)); }
    void txt(const std::string& s, int x, int y, uint32_t rgb, int maxW = -1, uint8_t a = 255, bool bold = false, int scale = -1,
             font::Face face = font::Face::Ui) {
        if (s.empty()) return;
        Cmd c{Op::Text}; c.r = {x, y, maxW, 0}; c.rgb = rgb; c.a = a; c.bold = bold; c.face = face;
        c.radius = (int16_t)(scale < 0 ? th.fontScale : scale);
        if (curLayer) { c.str = layers[curLayer].addStr(s); record(c); } else exec(c, &s);
    }
    void ico(icons::Id id, int x, int y, uint32_t rgb, uint8_t a = 255, int scale = 1) {
        if (id == icons::None) return;
        Cmd c{Op::Icon}; c.r = {x, y, 0, 0}; c.rgb = rgb; c.a = a; c.icon = id; c.radius = (int16_t)scale;
        if (curLayer) record(c); else exec(c, nullptr);
    }
    void applyClip() { Cmd c{Op::Clip}; c.r = clip(); if (curLayer) record(c); else exec(c, nullptr); }
    void pushClip(SDL_Rect r) { clips.push_back(intersect(clip(), r)); applyClip(); }
    void pushClipAbsolute(SDL_Rect r) { clips.push_back(r); applyClip(); }
    void popClip() { if (clips.size() > 1) clips.pop_back(); applyClip(); }
    bool visible(const SDL_Rect& r) const { SDL_Rect i = intersect(clip(), r); return i.w > 0 && i.h > 0; }
    uint32_t styleColor(TextStyle s) const;
    bool styleBold(TextStyle s) const { return s == TextStyle::Heading || s == TextStyle::Section; }
    void styledText(const std::string& s, int x, int y, TextStyle style, int maxW = -1) {
        txt(s, x, y, styleColor(style), maxW, 255, styleBold(style));
    }
    int textY(const SDL_Rect& r) const { return r.y + (r.h - fontH()) / 2; }
    uint32_t stateText(bool enabled, bool active) const { return !enabled ? th.textDisabled : active ? th.textOnAccent : th.text; }
    void buttonFrame(const SDL_Rect& r, const Hit& h, bool enabled, bool active);

    // ---- layout
    SDL_Rect alloc(int h, int naturalW = -1, bool fullWidth = true);
    void splitLabel(const SDL_Rect& r, const std::string& label, SDL_Rect& lab, SDL_Rect& ctl) const;
    void beginPanelImpl(uint64_t pid, SDL_Rect r, bool scroll, bool background, int pad);
    void endPanelImpl();
    int contentH() const { const Panel& p = panels.back(); return std::max(0, p.maxBottom - p.startY); }
    bool scrollbar(uint64_t id, SDL_Rect lane, int viewH, int contentH, int& scrollY);

    // ---- interaction
    Hit hit(uint64_t id, const SDL_Rect& r, bool enabled);
    void tipFor(const Hit& h, uint64_t id, const SDL_Rect& r, const char* tip, const char* shortcut);
    void registerField(uint64_t id) { fieldsCur.push_back(id); if (focus == id) focusSeen = true; }
    void moveFocus(uint64_t from, int dir);
    void blur() { focus = 0; edit.id = 0; edit.buf.clear(); edit.selAll = false; }
    void takeFocus(uint64_t id) {
        focus = id; focusSeen = true;
        edit.id = 0; edit.buf.clear(); edit.caret = 0; edit.selAll = false; edit.scrollX = 0;
    }
    void startTyping(uint64_t id, double v, int decimals) {
        edit.id = id; edit.buf = fmtNumber(v, decimals); edit.caret = (int)edit.buf.size(); edit.selAll = true;
    }
    struct KeyResult { bool changed = false, enter = false, esc = false; int tab = 0; };
    KeyResult editKeys(std::string& s, int maxLen, bool numeric);
    void drawEditText(const SDL_Rect& box, const std::string& s, const char* placeholder, bool focused, uint32_t color);
    bool valueField(const char* id, const std::string& label, double& v, double step, double lo, double hi, const char* unit, int decimals,
                    const char* tip, bool enabled, bool isInt);
    bool textEdit(uint64_t wid, SDL_Rect r, std::string& text, const char* placeholder, bool* submitted, bool* cancelled, int maxLen);

    // ---- overlays
    bool beginOverlay(uint64_t pid, SDL_Rect r, bool modal, bool closeOutside, bool& open, int pad, bool scroll, int maxH, bool above);
    void endOverlay(bool fitHeight);
    void closeOverlayPanels() { while (panels.size() > ovStack.back().panelDepth + 1) endPanelImpl(); }
    SDL_Rect placePopup(uint64_t pid, SDL_Rect anchor, int w, int& maxH, bool& above);
    SDL_Rect menuRow() { SDL_Rect r = alloc(th.rowH - th.gap); r.h += th.gap; return r; }   // rows that touch: the layout gap is folded in
    void drawTooltip();
    void drawToasts();
    void mruPush(const std::string& name);
    // an overlay that closed itself this frame must not block the base layer next frame
    void dropOverlay(uint64_t pid) {
        ovCur.erase(std::remove_if(ovCur.begin(), ovCur.end(), [pid](const Overlay& o) { return o.id == pid; }), ovCur.end());
    }
};

uint32_t Context::Impl::styleColor(TextStyle s) const {
    switch (s) {
        case TextStyle::Dim: case TextStyle::Section: return th.textDim;
        case TextStyle::Disabled: return th.textDisabled;
        case TextStyle::Accent: return th.accent;
        case TextStyle::Warning: return th.warning;
        case TextStyle::Danger: return th.danger;
        default: return th.text;
    }
}

void Context::Impl::exec(const Cmd& c, const std::string* s) {
    ++frameCalls;
    switch (c.op) {
        case Op::Clip: SDL_RenderSetClipRect(ren, &c.r); break;
        case Op::Fill: {
            if (c.r.w <= 0 || c.r.h <= 0) break;   // a popup frame fitted to no content; SDL draws a pixel for a zero-size rect
            SDL_Rect rs[2 * kMaxRadius + 1];
            int n = roundedFill(c.r, c.radius, rs);
            SDL_SetRenderDrawColor(ren, uint8_t(c.rgb >> 16), uint8_t(c.rgb >> 8), uint8_t(c.rgb), c.a);
            SDL_RenderFillRects(ren, rs, n);
            break;
        }
        case Op::Stroke: {
            if (c.r.w <= 0 || c.r.h <= 0) break;
            SDL_Rect rs[4 * kMaxRadius + 4];
            int n = roundedOutline(c.r, c.radius, rs);
            SDL_SetRenderDrawColor(ren, uint8_t(c.rgb >> 16), uint8_t(c.rgb >> 8), uint8_t(c.rgb), c.a);
            SDL_RenderFillRects(ren, rs, n);
            break;
        }
        case Op::Text: {
            const std::string& str = s ? *s : layers[curLayer].strs[(size_t)c.str];
            SDL_Color color = col(c.rgb, c.a);
            for (int pass = 0; pass <= (c.bold ? 1 : 0); ++pass) {
                if (c.r.w >= 0) font::textFit(ren, str, c.r.x + pass, c.r.y, c.face, c.radius, color, c.r.w);
                else font::text(ren, str, c.r.x + pass, c.r.y, c.face, c.radius, color);
            }
            break;
        }
        case Op::Icon: icons::draw(ren, c.icon, c.r.x, c.r.y, c.radius, col(c.rgb, c.a)); break;
    }
}

void Context::Impl::buttonFrame(const SDL_Rect& r, const Hit& h, bool enabled, bool active) {
    uint32_t bg = th.surface2, bd = th.border;
    if (!enabled) { bg = th.surface; }
    else if (active) { bg = th.accent; bd = th.accent; }
    else if (h.down && h.over) { bg = th.accentDim; bd = th.accent; }
    else if (h.hover) { bg = th.hover; bd = th.borderLight; }
    fill(r, bg, 255, th.radius);
    stroke(r, bd, 255, th.radius);
}

// ---------------------------------------------------------------------------------------------------------------- layout

SDL_Rect Context::Impl::alloc(int h, int naturalW, bool fullWidth) {
    Panel& p = cur();
    SDL_Rect r;
    if (p.sameLine) {
        p.sameLine = false;
        int x = p.last.x + p.last.w + th.gap, right = p.inner.x + p.inner.w;
        int room = std::max(0, right - x);
        r = {x, p.last.y, naturalW > 0 ? std::min(naturalW, room) : room, p.last.h};
    } else if (p.rowCols > 0) {
        if (p.rowIdx == 0) p.rowY = p.y;
        float before = 0;
        for (int i = 0; i < p.rowIdx; ++i) before += p.weights[i];
        float sum = p.weightSum > 0 ? p.weightSum : 1;
        int usable = p.inner.w - th.gap * (p.rowCols - 1);
        int x0 = p.inner.x + (int)std::lround(usable * before / sum) + th.gap * p.rowIdx;
        int x1 = p.inner.x + (int)std::lround(usable * (before + p.weights[p.rowIdx]) / sum) + th.gap * p.rowIdx;
        r = {x0, p.rowY, std::max(0, x1 - x0), p.rowH};
        if (++p.rowIdx >= p.rowCols) { p.rowCols = 0; p.y = p.rowY + p.rowH + th.gap; }
    } else {
        r = {p.inner.x, p.y, fullWidth || naturalW <= 0 ? p.inner.w : std::min(naturalW, p.inner.w), h};
        p.y += h + th.gap;
    }
    p.last = r;
    p.maxBottom = std::max(p.maxBottom, r.y + r.h);
    lastId = 0; lastRect = r; lastOver = false;
    return r;
}

// A labelled control: the label takes its natural width (at most half the row), the control the rest.
void Context::Impl::splitLabel(const SDL_Rect& r, const std::string& label, SDL_Rect& lab, SDL_Rect& ctl) const {
    if (label.empty()) { lab = {r.x, r.y, 0, r.h}; ctl = r; return; }
    int lw = std::min(textW(label) + th.gap * 2, r.w / 2);
    lab = {r.x, r.y, lw, r.h};
    ctl = {r.x + lw, r.y, r.w - lw, r.h};
}

void Context::Impl::beginPanelImpl(uint64_t pid, SDL_Rect r, bool scroll, bool background, int pad) {
    ids.push_back(pid);
    WState& s = st[pid];
    Panel p;
    p.id = pid; p.outer = r; p.pad = pad < 0 ? th.pad : pad; p.scroll = scroll;
    p.canScroll = scroll && s.contentH > r.h;
    int lane = p.canScroll ? th.scrollbarW : 0;
    s.scrollY = clampi(s.scrollY, 0, std::max(0, s.contentH - r.h));
    p.scrollY = p.canScroll ? s.scrollY : 0;
    p.inner = {r.x + p.pad, r.y + p.pad, std::max(0, r.w - 2 * p.pad - lane), std::max(0, r.h - 2 * p.pad)};
    p.startY = p.inner.y - p.scrollY;
    p.y = p.startY; p.maxBottom = p.startY;
    p.last = {p.inner.x, p.startY, 0, 0};
    if (background) { fill(r, th.surface); stroke(r, th.border); }
    if (contains(r, in.mx, in.my) && pointerAvail()) mouseOverUi = true;
    panels.push_back(p);
    pushClip(inset(r, 1));
}

void Context::Impl::endPanelImpl() {
    Panel p = panels.back();
    WState& s = st[p.id];
    int ch = contentH() + 2 * p.pad;
    s.contentH = ch;
    if (p.scroll && ch > p.outer.h) {
        // the wheel scrolls the panel under the pointer unless a child already took it; the scrollbar sits in its lane
        bool over = contains(p.outer, in.mx, in.my) && pointerAvail() && contains(clip(), in.mx, in.my);
        if (over && in.wheel != 0 && !wheelConsumed) {
            s.scrollY = clampi(s.scrollY - in.wheel * 48, 0, ch - p.outer.h);
            wheelConsumed = true;
        }
        SDL_Rect lane{p.outer.x + p.outer.w - 1 - th.scrollbarW, p.outer.y + 1, th.scrollbarW, std::max(0, p.outer.h - 2)};
        scrollbar(p.id ^ 0x5, lane, p.outer.h, ch, s.scrollY);
    }
    popClip();
    panels.pop_back();
    ids.pop_back();
    if (!panels.empty()) panels.back().maxBottom = std::max(panels.back().maxBottom, p.outer.y + p.outer.h);
}

bool Context::Impl::scrollbar(uint64_t id, SDL_Rect lane, int viewH, int contentH, int& scrollY) {
    int maxScroll = contentH - viewH;
    if (maxScroll <= 0 || lane.h <= 0 || lane.w <= 0) return false;
    // 64-bit products: a long list times a tall lane overflows int
    int thumbH = clampi((int)((int64_t)lane.h * viewH / contentH), std::min(24, lane.h), lane.h), track = lane.h - thumbH;
    auto thumbAt = [&](int sy) { return lane.y + (track > 0 ? (int)((int64_t)track * sy / maxScroll) : 0); };
    int thumbY = thumbAt(scrollY);
    Hit h = hit(id, lane, true);
    WState& s = st[id];
    bool changed = false;
    if (h.pressed) {   // on the thumb: drag it; on the track: jump there and keep dragging
        s.drag = true;
        s.dragOff = contains({lane.x, thumbY, lane.w, thumbH}, in.mx, in.my) ? in.my - thumbY : thumbH / 2;
    }
    if (s.drag && active == id && in.lDown && track > 0) {
        int ns = (int)clampd((double)(in.my - s.dragOff - lane.y) * maxScroll / track, 0, maxScroll);
        if (ns != scrollY) { scrollY = ns; changed = true; thumbY = thumbAt(scrollY); }
    }
    if (active != id) s.drag = false;
    int tw = (h.hover || s.drag) ? lane.w - 2 : std::max(4, lane.w / 2);
    fill({lane.x + (lane.w - tw) / 2, thumbY, tw, thumbH}, s.drag ? th.accent : h.hover ? th.borderLight : th.border, 255, tw / 2);
    return changed;
}

// ---------------------------------------------------------------------------------------------------------------- interaction

Hit Context::Impl::hit(uint64_t id, const SDL_Rect& r, bool enabled) {
    Hit h;
    bool mine = active == 0 || active == id;
    h.over = pointerAvail() && contains(clip(), in.mx, in.my) && contains(r, in.mx, in.my) && mine;
    h.hover = h.over && enabled;
    if (h.hover && in.lPressed) { h.pressed = true; active = id; }
    if (h.pressed) h.dbl = in.lDouble;
    if (active == id) { activeSeen = true; h.down = in.lDown || in.lReleased; h.released = in.lReleased; }
    h.clicked = h.released && h.hover;
    lastId = id; lastRect = r; lastOver = h.over;
    return h;
}

void Context::Impl::tipFor(const Hit& h, uint64_t id, const SDL_Rect& r, const char* tip, const char* shortcut) {
    if (!h.over || !tip) return;
    tipId = id; tipRect = r; tipText = tip; tipShortcut = shortcut ? shortcut : "";
}

void Context::Impl::moveFocus(uint64_t from, int dir) {
    if (fieldsPrev.empty()) { blur(); return; }
    int n = (int)fieldsPrev.size(), i = 0;
    for (; i < n; ++i) if (fieldsPrev[i] == from) break;
    int next = i >= n ? 0 : ((i + dir) % n + n) % n;
    takeFocus(fieldsPrev[next]);
    edit.selAll = true;
}

// Keyboard editing shared by text fields and numeric typing: edits `s` in place using the context's caret/selection.
Context::Impl::KeyResult Context::Impl::editKeys(std::string& s, int maxLen, bool numeric) {
    KeyResult kr;
    edit.caret = clampi(edit.caret, 0, (int)s.size());
    if (!keysAvail()) return kr;   // an overlay above this field has the keyboard
    auto eraseAll = [&] { s.clear(); edit.caret = 0; edit.selAll = false; kr.changed = true; };
    for (SDL_Keycode k : in.keys) {
        switch (k) {
            case SDLK_BACKSPACE:
                if (edit.selAll) eraseAll();
                else if (edit.caret > 0) {
                    int p = prevChar(s, edit.caret);
                    s.erase((size_t)p, (size_t)(edit.caret - p)); edit.caret = p; kr.changed = true;
                }
                break;
            case SDLK_DELETE:
                if (edit.selAll) eraseAll();
                else if (edit.caret < (int)s.size()) {
                    s.erase((size_t)edit.caret, (size_t)(nextChar(s, edit.caret) - edit.caret)); kr.changed = true;
                }
                break;
            case SDLK_LEFT: edit.caret = edit.selAll ? 0 : prevChar(s, edit.caret); edit.selAll = false; break;
            case SDLK_RIGHT: edit.caret = edit.selAll ? (int)s.size() : nextChar(s, edit.caret); edit.selAll = false; break;
            case SDLK_HOME: edit.caret = 0; edit.selAll = false; break;
            case SDLK_END: edit.caret = (int)s.size(); edit.selAll = false; break;
            case SDLK_RETURN: case SDLK_KP_ENTER: kr.enter = true; break;
            case SDLK_ESCAPE: if (!escConsumed) { kr.esc = true; escConsumed = true; } break;
            case SDLK_TAB: if (!tabConsumed) { kr.tab = in.shift ? -1 : 1; tabConsumed = true; } break;   // the field Tab lands on must not Tab again
            case SDLK_a: if (in.ctrl) edit.selAll = true; break;
            default: break;
        }
    }
    if (!in.typed.empty() && !in.ctrl && !in.alt) {
        for (size_t i = 0; i < in.typed.size();) {
            unsigned char c = (unsigned char)in.typed[i];
            size_t n = 1;   // one UTF-8 sequence, inserted whole so the length limit never splits it
            while (i + n < in.typed.size() && n < 4 && c >= 0xC0 && ((unsigned char)in.typed[i + n] & 0xC0) == 0x80) ++n;
            if (c < 32 || c == 127 || (numeric && !std::strchr("0123456789.+-*/ ", (char)c))) { i += n; continue; }
            if (edit.selAll) eraseAll();
            if (s.size() + n > (size_t)std::max(0, maxLen)) break;
            s.insert((size_t)edit.caret, in.typed, i, n);
            edit.caret += (int)n;
            kr.changed = true;
            i += n;
        }
    }
    return kr;
}

// Text inside a field box: placeholder when empty, selection highlight, blinking caret, scrolled so the caret shows.
void Context::Impl::drawEditText(const SDL_Rect& box, const std::string& s, const char* placeholder, bool focused, uint32_t color) {
    SDL_Rect inner = inset(box, 2);
    inner.x += th.gap; inner.w = std::max(0, inner.w - 2 * th.gap);
    if (inner.w <= 0) return;
    pushClip(inner);
    int ty = textY(box);
    if (s.empty()) {
        if (placeholder && *placeholder && !focused) txt(placeholder, inner.x, ty, th.textDisabled, inner.w);
    } else if (!focused) {
        txt(s, inner.x, ty, color, inner.w);
    } else {
        int caretX = textW(s.substr(0, (size_t)clampi(edit.caret, 0, (int)s.size())));
        if (caretX - edit.scrollX > inner.w - 2) edit.scrollX = caretX - inner.w + 2;
        if (caretX - edit.scrollX < 0) edit.scrollX = caretX;
        edit.scrollX = clampi(edit.scrollX, 0, std::max(0, textW(s) - inner.w + 2));
        int x = inner.x - edit.scrollX;
        if (edit.selAll) fill({x - 1, ty - 1, textW(s) + 2, fontH() + 2}, th.selection);
        txt(s, x, ty, color);
    }
    if (focused && (in.ticks / 530) % 2 == 0) {
        int caretX = s.empty() ? inner.x : inner.x - edit.scrollX + textW(s.substr(0, (size_t)clampi(edit.caret, 0, (int)s.size())));
        fill({caretX, ty - 1, 2, fontH() + 2}, th.focus);
    }
    popClip();
}

bool Context::Impl::textEdit(uint64_t wid, SDL_Rect r, std::string& text, const char* placeholder, bool* submitted, bool* cancelled, int maxLen) {
    registerField(wid);
    Hit h = hit(wid, r, true);
    bool focused = focus == wid, changed = false;
    if (h.pressed) {
        if (!focused) { takeFocus(wid); focused = true; edit.caret = (int)text.size(); }
        else if (h.dbl) edit.selAll = true;
        else {   // put the caret where the click landed
            int x = in.mx - (r.x + 2 + th.gap) + edit.scrollX, best = (int)text.size();
            for (int i = 0; i <= (int)text.size(); i = nextChar(text, i)) {
                if (textW(text.substr(0, (size_t)i)) >= x) { best = i; break; }
                if (i == (int)text.size()) break;
            }
            edit.caret = best; edit.selAll = false;
        }
    } else if (focused && in.lPressed && !h.over && pointerAvail()) {
        blur(); focused = false;   // a click elsewhere in this layer ends the edit
    }
    if (focused) {
        KeyResult kr = editKeys(text, maxLen, false);
        changed = kr.changed;
        if (kr.enter) { if (submitted) *submitted = true; editedFlag = true; }
        if (kr.esc) { if (cancelled) *cancelled = true; blur(); focused = false; }
        if (kr.tab) { editedFlag = true; moveFocus(wid, kr.tab); focused = false; }
    }
    if (visible(r)) {
        fill(r, th.surface2, 255, th.radius);
        if (focused) ring(r, th.focus);
        else stroke(r, h.hover ? th.borderLight : th.border, 255, th.radius);
        drawEditText(r, text, placeholder, focused, th.text);
    }
    return changed;
}

// dragFloat and dragInt share this: a label, then a box with - and + ends and the number (or the typed text) between.
bool Context::Impl::valueField(const char* idStr, const std::string& label, double& v, double step, double lo, double hi, const char* unit,
                               int decimals, const char* tip, bool enabled, bool isInt) {
    uint64_t wid = id(idStr);
    int natural = (label.empty() ? 0 : textW(label) + 2 * th.gap) + 2 * th.fieldH + textW("0000.0") + 2 * th.gap
                  + (unit && *unit ? textW(unit) + th.gap : 0);
    SDL_Rect r = alloc(th.fieldH, natural, true), lab, ctl;
    splitLabel(r, label, lab, ctl);
    if (enabled) registerField(wid);   // Tab skips disabled fields, and one that is disabled while focused lets go of the focus
    double before = v;
    bool changed = false;
    auto snap = [&](double x) { x = clampd(x, lo, hi); return isInt ? std::round(x) : x; };
    v = snap(v);
    double mod = in.shift ? 0.1 : in.ctrl ? 10 : 1;
    bool typing = edit.id == wid;
    if (typing && focus != wid) {   // focus went elsewhere: commit what was typed
        double out; if (evalExpr(edit.buf, out)) v = snap(out);
        edit.id = 0; edit.buf.clear(); typing = false; editedFlag = true;
    }
    int bw = ctl.h;
    SDL_Rect minus{ctl.x, ctl.y, bw, ctl.h}, plus{ctl.x + ctl.w - bw, ctl.y, bw, ctl.h};
    SDL_Rect mid{ctl.x + bw, ctl.y, std::max(0, ctl.w - 2 * bw), ctl.h};
    if (ctl.w < 3 * bw || typing) { mid = ctl; minus.w = plus.w = 0; }   // too narrow for end buttons, or the text needs the room

    // end buttons: once on press, then repeating while held
    Hit hm = hit(wid ^ 0x1, minus, enabled && minus.w > 0), hp = hit(wid ^ 0x2, plus, enabled && plus.w > 0);
    auto bump = [&](double dir) { double nv = snap(v + dir * step * mod); if (nv != v) { v = nv; changed = true; } };
    for (int side = 0; side < 2; ++side) {
        const Hit& hb = side ? hp : hm;
        double dir = side ? 1 : -1;
        if (hb.pressed) { bump(dir); num.repeatAt = in.ticks + 350; }
        else if (hb.down && !hb.released && (int32_t)(in.ticks - num.repeatAt) >= 0) { bump(dir); num.repeatAt = in.ticks + 60; }   // wrap-safe
        if (hb.released) editedFlag = true;
    }

    // the value: press and drag scrubs, release without moving types, wheel steps
    Hit hv = hit(wid, mid, enabled);
    if (hv.pressed) { num.id = wid; num.startX = in.mx; num.startV = v; num.moved = false; num.dbl = hv.dbl; }
    if (active == wid && num.id == wid && in.lDown && !typing) {
        int dx = in.mx - num.startX;
        if (std::abs(dx) >= 3) num.moved = true;
        if (num.moved) {   // one step per 4 px
            double nv = snap(num.startV + std::floor(dx / 4.0) * step * mod);
            if (nv != v) { v = nv; changed = true; }
        }
    }
    if (hv.released && num.id == wid) {
        if (num.moved) editedFlag = true;
        else if (!typing) { takeFocus(wid); startTyping(wid, v, isInt ? 0 : decimals); typing = true; }
        else if (num.dbl) edit.selAll = true;
        num.id = 0;
    }
    if (hv.hover && in.wheel != 0 && !typing) { bump(in.wheel); editedFlag = true; wheelConsumed = true; }
    if (typing && in.lPressed && !hv.over && pointerAvail()) {   // click elsewhere: commit
        double out; if (evalExpr(edit.buf, out)) v = snap(out);
        blur(); typing = false; editedFlag = true;
    }
    if (typing) {
        KeyResult kr = editKeys(edit.buf, 32, true);
        if (kr.enter || kr.tab) {
            double out; if (evalExpr(edit.buf, out)) v = snap(out);
            editedFlag = true;
            if (kr.tab) moveFocus(wid, kr.tab); else blur();
            typing = false;
        } else if (kr.esc) { blur(); typing = false; }
    } else if (focus == wid && enabled) {   // focus arrived through Tab: type with the number selected
        startTyping(wid, v, isInt ? 0 : decimals); typing = true;
    }
    if (v != before) changed = true;

    // ---- draw
    if (visible(r)) {
        if (lab.w > 0) txt(label, lab.x, textY(lab), enabled ? th.textDim : th.textDisabled, std::max(0, lab.w - th.gap));
        bool scrubbing = active == wid && num.moved;
        uint32_t bg = !enabled ? th.surface : scrubbing ? th.accentDim : hv.hover || typing ? th.hover : th.surface2;
        fill(ctl, bg, 255, th.radius);
        if (typing) ring(ctl, th.focus);
        else stroke(ctl, scrubbing ? th.accent : hv.hover ? th.borderLight : th.border, 255, th.radius);
        if (minus.w > 0) {
            for (int side = 0; side < 2; ++side) {
                const SDL_Rect& b = side ? plus : minus;
                const Hit& hb = side ? hp : hm;
                if (hb.hover || hb.down) fill(inset(b, 2), hb.down ? th.accentDim : th.hover, 255, th.radius);
                int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
                uint32_t c = !enabled ? th.textDisabled : hb.hover ? th.text : th.textDim;
                fill({cx - 4, cy, 9, 2}, c);
                if (side) fill({cx, cy - 4, 2, 9}, c);
            }
            fill({minus.x + minus.w, ctl.y + 6, 1, ctl.h - 12}, th.border);
            fill({plus.x - 1, ctl.y + 6, 1, ctl.h - 12}, th.border);
        }
        if (typing) drawEditText(mid, edit.buf, "", true, th.text);
        else {
            std::string s = fmtNumber(v, isInt ? 0 : decimals);
            int uw = unit && *unit ? textW(unit) + th.gap : 0, sw = textW(s);
            int avail = mid.w - 2 * th.gap;
            uint32_t c = enabled ? th.text : th.textDisabled;
            pushClip(mid);
            if (sw + uw <= avail) {
                int x = mid.x + (mid.w - sw - uw) / 2;
                txt(s, x, textY(mid), c);
                if (uw) txt(unit, x + sw + th.gap, textY(mid), enabled ? th.textDim : th.textDisabled);
            } else txt(s, mid.x + th.gap, textY(mid), c, avail);
            popClip();
        }
    }
    tipFor(hv, wid, r, tip, nullptr);
    return changed;
}

// ---------------------------------------------------------------------------------------------------------------- overlays

// Positions a popup below an anchor (above it when there is no room), clamped to the window; maxH is the room it has.
SDL_Rect Context::Impl::placePopup(uint64_t pid, SDL_Rect anchor, int w, int& maxH, bool& above) {
    WState& s = st[pid];
    int h = s.contentH > 0 ? s.contentH : 0;
    w = std::min(w, std::max(0, winW - 2 * th.gap));
    SDL_Rect r{anchor.x, anchor.y + anchor.h + 2, w, h};
    int roomBelow = winH - th.gap - r.y, roomAbove = anchor.y - 2 - th.gap;
    above = h > roomBelow && roomAbove > roomBelow;
    if (above) { maxH = std::max(roomAbove, th.rowH); r.h = std::min(h, maxH); r.y = anchor.y - 2 - r.h; }
    else { maxH = std::max(roomBelow, 3 * th.rowH); r.h = std::min(h, maxH); }
    r.x = clampi(r.x, th.gap, std::max(th.gap, winW - w - th.gap));
    r.y = clampi(r.y, th.gap, std::max(th.gap, winH - r.h - th.gap));
    return r;
}

// Starts an overlay layer: handles Esc and click-outside, records the backdrop and frame, opens the content panel.
bool Context::Impl::beginOverlay(uint64_t pid, SDL_Rect r, bool modal, bool closeOutside, bool& open, int pad, bool scroll, int maxH,
                                 bool above) {
    WState& s = st[pid];
    if (!open) { s.open = false; return false; }
    int idx = ovIndex(pid);
    bool wasOpen = idx >= 0, underModal = false;   // a modal above this overlay takes Esc and clicks
    for (size_t i = (size_t)std::max(idx + 1, 0); i < ovPrev.size(); ++i) underModal = underModal || ovPrev[i].modal;
    if (wasOpen && idx == (int)ovPrev.size() - 1 && !escConsumed && hasKey(in, SDLK_ESCAPE)) {   // Esc closes the topmost
        escConsumed = true; open = false; s.open = false;
        return false;
    }
    if (wasOpen && closeOutside && !underModal && (in.lPressed || in.rPressed)) {
        int ownerIdx = pointerOwner == 0 || pointerOwner == kOutside ? -1 : ovIndex(pointerOwner);
        if (ownerIdx < idx) { open = false; s.open = false; return false; }
    }
    if (!s.open) { s.open = true; s.hl = -1; }
    if (++layerCount >= (int)layers.size()) layers.emplace_back();
    layers[layerCount].reset();
    curLayer = layerCount;
    pushClipAbsolute({0, 0, winW, winH});
    if (modal) fill({0, 0, winW, winH}, th.overlayShade, 140);
    OpenOverlay o{pid, modal, above, curLayer, r, maxH, (int)layers[curLayer].cmds.size(), 0, panels.size(), ovCur.size()};
    ovCur.push_back({pid, r, modal});   // in opening order, which is the z-order: a popup inside a modal sits above it
    SDL_Rect frame = r;
    if (frame.w <= 0) frame.w = 1;   // the size is fitted at endOverlay; the commands must exist to be patched
    if (frame.h <= 0) frame.h = 1;
    fill(frame, th.surface2, 255, th.radius);
    o.strokeCmd = (int)layers[curLayer].cmds.size();
    stroke(frame, th.borderLight, 255, th.radius);
    ovStack.push_back(o);
    SDL_Rect provisional = r;
    if (s.contentH == 0) provisional.h = std::max(r.h, maxH);   // first frame: the height is unknown, so give the content all the room
    beginPanelImpl(pid ^ 0x9, provisional, scroll, false, pad);
    return true;
}

void Context::Impl::endOverlay(bool fitHeight) {
    Panel p = panels.back();
    int ch = contentH() + 2 * p.pad;
    endPanelImpl();
    OpenOverlay o = ovStack.back();
    ovStack.pop_back();
    WState& s = st[o.id];
    s.contentH = ch;
    int fitted = std::min(ch, o.maxH);
    if (fitHeight && fitted != o.r.h) {   // size the frame to its content (the first frame was laid out with a provisional height)
        if (o.above) o.r.y += o.r.h - fitted;   // anchored above: keep the bottom edge
        o.r.h = fitted;
        Layer& L = layers[o.layer];
        L.cmds[(size_t)o.fillCmd].r = o.r;
        L.cmds[(size_t)o.strokeCmd].r = o.r;
    }
    if (o.ovIdx < ovCur.size() && ovCur[o.ovIdx].id == o.id) ovCur[o.ovIdx].r = o.r;
    popClip();
    curLayer = ovStack.empty() ? 0 : ovStack.back().layer;
}

void Context::Impl::mruPush(const std::string& name) {
    auto it = std::find(mru.begin(), mru.end(), name);
    if (it != mru.end()) mru.erase(it);
    mru.insert(mru.begin(), name);
    if (mru.size() > 8) mru.pop_back();
}

void Context::Impl::drawTooltip() {
    // the pointer must rest on one widget for the delay; moving restarts it, pressing hides it until the hover changes
    bool moved = std::abs(in.mx - hoverX) > 2 || std::abs(in.my - hoverY) > 2;
    if (tipId != hoverPrev) { hoverPrev = tipId; tipShown = false; tipSuppressed = false; moved = true; }
    if (moved && !tipShown) { hoverSince = in.ticks; hoverX = in.mx; hoverY = in.my; }
    if (in.lPressed || in.rPressed || in.mPressed) { tipSuppressed = true; tipShown = false; }
    if (tipId == 0 || tipSuppressed || tipText.empty()) return;
    if (!tipShown && in.ticks - hoverSince < (uint32_t)th.tooltipDelayMs) return;
    tipShown = true;
    int tw = textW(tipText), sw = tipShortcut.empty() ? 0 : textW(tipShortcut) + th.pad;
    int w = tw + sw + 2 * th.pad, h = fontH() + 2 * th.gap + 2;
    SDL_Rect r{tipRect.x + (tipRect.w - w) / 2, tipRect.y + tipRect.h + 4, w, h};
    if (r.y + h > winH - 2) r.y = tipRect.y - 4 - h;
    r.x = clampi(r.x, 2, std::max(2, winW - w - 2));
    r.y = clampi(r.y, 2, std::max(2, winH - h - 2));
    fill(r, th.tooltipBg, 245, th.radius);
    stroke(r, th.borderLight, 255, th.radius);
    txt(tipText, r.x + th.pad, textY(r), th.text);
    if (sw) txt(tipShortcut, r.x + th.pad + tw + th.pad, textY(r), th.textDim);
}

void Context::Impl::drawToasts() {
    auto expired = [&](const Toast& t) { return (int32_t)(t.until - in.ticks) <= 0; };
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), expired), toasts.end());
    int y = winH - 40;
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
        int left = (int32_t)(it->until - in.ticks);
        uint8_t a = (uint8_t)(left < 400 ? 255 * left / 400 : 255);
        int w = textW(it->text) + 2 * th.pad + 6, h = fontH() + 2 * th.gap + 4;
        SDL_Rect r{(winW - w) / 2, y - h, w, h};
        fill(r, th.tooltipBg, (uint8_t)(a * 240 / 255), th.radius);
        stroke(r, th.border, a, th.radius);
        fill({r.x + 1, r.y + 3, 3, h - 6}, it->style == TextStyle::Normal ? th.accent : styleColor(it->style), a, 1);
        txt(it->text, r.x + th.pad + 4, textY(r), styleColor(it->style), -1, a);
        y -= h + th.gap;
    }
}

// ================================================================================================================= Context

Context::Context() : impl_(new Impl) {}
Context::~Context() { delete impl_; }

void Context::begin(SDL_Renderer* ren, const Input& input) {
    Impl& m = *impl_;
    m.ren = ren;
    m.in = input;
    m.winW = input.winW; m.winH = input.winH;
    if ((m.winW <= 0 || m.winH <= 0) && ren) SDL_GetRendererOutputSize(ren, &m.winW, &m.winH);
    m.winW = std::max(0, m.winW); m.winH = std::max(0, m.winH);   // a negative clip rect would switch clipping off
    m.frameCalls = 0;
    if (ren) SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    m.ids.clear(); m.panels.clear(); m.clips.clear(); m.ovStack.clear();
    m.layerCount = 0; m.curLayer = 0;
    if (m.layers.empty()) m.layers.emplace_back();
    m.editedFlag = m.listActivatedFlag = m.wheelConsumed = m.escConsumed = m.tabConsumed = false;
    m.focusSeen = m.activeSeen = m.listFocusSeen = false;
    m.tipId = 0; m.tipText.clear(); m.tipShortcut.clear(); m.hintText.clear();
    m.lastId = 0; m.lastOver = false;
    std::swap(m.ovPrev, m.ovCur); m.ovCur.clear();
    std::swap(m.fieldsPrev, m.fieldsCur); m.fieldsCur.clear();
    // who owns the pointer: the topmost overlay under it; nobody when overlays are open but none is under it, and
    // nothing below a modal
    m.pointerOwner = 0;
    for (size_t i = m.ovPrev.size(); i-- > 0;) {
        if (contains(m.ovPrev[i].r, m.in.mx, m.in.my)) { m.pointerOwner = m.ovPrev[i].id; break; }
        if (m.ovPrev[i].modal) break;
    }
    if (m.pointerOwner == 0 && !m.ovPrev.empty()) m.pointerOwner = kOutside;
    m.mouseOverUi = !m.ovPrev.empty();
    if (!m.in.lDown && !m.in.lReleased) { m.active = 0; m.num.id = 0; }   // a release we never saw
    m.clips.push_back({0, 0, m.winW, m.winH});
    if (ren) SDL_RenderSetClipRect(ren, nullptr);
    Panel root;   // widgets declared outside any panel flow over the window
    root.outer = root.inner = {0, 0, m.winW, m.winH};
    root.last = {0, 0, 0, 0};
    m.panels.push_back(root);
}

void Context::end() {
    Impl& m = *impl_;
    while (!m.ovStack.empty()) { m.closeOverlayPanels(); m.endOverlay(false); }   // anything left open is closed for the caller
    while (m.panels.size() > 1) m.endPanelImpl();
    if (m.focus && !m.focusSeen) m.blur();
    if (m.active && !m.activeSeen) m.active = 0;
    if (m.listFocus && !m.listFocusSeen) m.listFocus = 0;
    if (m.in.lReleased) { m.active = 0; m.num.id = 0; }
    if (!m.ren) return;
    for (int i = 1; i <= m.layerCount; ++i) {   // replay the deferred layers in the order they were opened
        m.curLayer = i;
        for (const Cmd& c : m.layers[(size_t)i].cmds) m.exec(c, nullptr);
    }
    m.curLayer = 0;
    SDL_RenderSetClipRect(m.ren, nullptr);
    m.clips.assign(1, SDL_Rect{0, 0, m.winW, m.winH});
    m.drawTooltip();
    m.drawToasts();
    g_lastFrameCalls = m.frameCalls;
}

// anyPopupOpen() counts what is open now and what was open when the frame began: the Esc or the click that closed a
// popup belonged to the toolkit, so the viewport must not act on it too (wantsMouse() already works this way).
bool Context::anyPopupOpen() const { const Impl& m = *impl_; return !m.ovCur.empty() || !m.ovPrev.empty(); }
bool Context::wantsMouse() const { return impl_->mouseOverUi || anyPopupOpen() || impl_->active != 0; }
bool Context::wantsKeyboard() const { return impl_->focus != 0; }
const Input& Context::input() const { return impl_->in; }
SDL_Renderer* Context::renderer() const { return impl_->ren; }

void Context::pushId(const char* id) { impl_->ids.push_back(hashStr(impl_->seed(), id)); }
void Context::pushId(int id) { impl_->ids.push_back(hashInt(impl_->seed(), id)); }
void Context::popId() { if (!impl_->ids.empty()) impl_->ids.pop_back(); }

// ---- panels and layout

void Context::beginPanel(const char* id, SDL_Rect r, bool scroll, bool background, int pad) {
    impl_->beginPanelImpl(impl_->id(id), r, scroll, background, pad);
}
void Context::endPanel() {   // never pops the root or an overlay's own panel: a stray endPanel must not unbalance the stack
    Impl& m = *impl_;
    if (m.panels.size() > (m.ovStack.empty() ? 1 : m.ovStack.back().panelDepth + 1)) m.endPanelImpl();
}
SDL_Rect Context::panelRect() const { return impl_->panels.back().inner; }
int Context::contentHeight() const { return impl_->contentH(); }

// A row begun while the previous one still has empty columns closes that one first, so rows never overlap.
static void startRow(Panel& p, int columns, int height, const Theme& th) {
    if (p.rowCols > 0 && p.rowIdx > 0) p.y = p.rowY + p.rowH + th.gap;
    p.sameLine = false;
    p.rowCols = clampi(columns, 1, 8); p.rowIdx = 0; p.rowH = height < 0 ? th.rowH : height;
}
void Context::row(int columns, int height) {
    Panel& p = impl_->cur();
    startRow(p, columns, height, impl_->th);
    p.weightSum = (float)p.rowCols;
    for (int i = 0; i < 8; ++i) p.weights[i] = 1;
}
void Context::row(const std::vector<float>& weights, int height) {
    Panel& p = impl_->cur();
    startRow(p, (int)weights.size(), height, impl_->th);   // an empty list gives one column
    p.weightSum = 0;
    for (int i = 0; i < p.rowCols; ++i) {
        p.weights[i] = i < (int)weights.size() ? std::max(0.f, weights[(size_t)i]) : 1;
        p.weightSum += p.weights[i];
    }
}
void Context::space(int px) { impl_->cur().y += px; }
void Context::separator() {
    Impl& m = *impl_;
    SDL_Rect r = m.alloc(m.th.gap * 2 + 1);
    if (m.visible(r)) m.fill({r.x, r.y + m.th.gap, r.w, 1}, m.th.border);
}
SDL_Rect Context::next(int height) { return impl_->alloc(height); }
void Context::sameLine() { impl_->cur().sameLine = true; }

// ---- text

void Context::label(const std::string& text, TextStyle style, Align align) {
    Impl& m = *impl_;
    int tw = m.textW(text);
    SDL_Rect r = m.alloc(m.lineH(), tw, true);
    if (!m.visible(r)) return;
    int x = r.x;
    if (tw <= r.w) x = align == Align::Centre ? r.x + (r.w - tw) / 2 : align == Align::Right ? r.x + r.w - tw : r.x;
    m.styledText(text, x, m.textY(r), style, tw <= r.w ? -1 : r.w);
}

void Context::labelWrapped(const std::string& text, TextStyle style) {
    Impl& m = *impl_;
    Panel& p = m.cur();
    int w = p.rowCols > 0 ? p.inner.w / p.rowCols : p.inner.w;
    std::vector<std::string> lines = font::wrap(text, m.th.face, m.th.fontScale, std::max(1, w));
    SDL_Rect r = m.alloc(std::max(1, (int)lines.size()) * m.lineH() - m.th.gap);
    if (!m.visible(r)) return;
    int y = r.y;
    for (const std::string& l : lines) { m.styledText(l, r.x, y, style, r.w); y += m.lineH(); }
}

void Context::keyValue(const std::string& key, const std::string& value) {
    Impl& m = *impl_;
    SDL_Rect r = m.alloc(m.lineH());
    if (!m.visible(r)) return;
    int kw = std::min(m.textW(key), r.w / 2), vw = std::min(m.textW(value), std::max(0, r.w - kw - m.th.gap));
    m.txt(key, r.x, m.textY(r), m.th.textDim, kw);
    m.txt(value, r.x + r.w - vw, m.textY(r), m.th.text, vw);
}

// ---- buttons

bool Context::button(const char* id, const std::string& label, icons::Id icon, const char* tip, bool enabled, bool active, const char* shortcut) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    int iconW = icon != icons::None ? 16 + (label.empty() ? 0 : m.th.gap) : 0;
    SDL_Rect r = m.alloc(m.th.buttonH, iconW + m.textW(label) + 2 * m.th.pad, true);
    Hit h = m.hit(wid, r, enabled);
    if (m.visible(r)) {
        m.buttonFrame(r, h, enabled, active);
        uint32_t c = m.stateText(enabled, active);
        int cw = iconW + m.textW(label), x = r.x + std::max(m.th.gap, (r.w - cw) / 2);
        m.pushClip(inset(r, 1));
        if (icon != icons::None) { m.ico(icon, x, r.y + (r.h - 16) / 2, c); x += iconW; }
        m.txt(label, x, m.textY(r), c, std::max(0, r.x + r.w - m.th.gap - x));
        m.popClip();
    }
    m.tipFor(h, wid, r, tip, shortcut);
    return h.clicked;
}

bool Context::iconButton(const char* id, icons::Id icon, const char* tip, bool enabled, bool active, const char* shortcut) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    SDL_Rect r = m.alloc(m.th.iconButton, m.th.iconButton, false);
    Hit h = m.hit(wid, r, enabled);
    if (m.visible(r)) {
        m.buttonFrame(r, h, enabled, active);
        m.ico(icon, r.x + (r.w - 16) / 2, r.y + (r.h - 16) / 2, m.stateText(enabled, active));
    }
    m.tipFor(h, wid, r, tip, shortcut);
    return h.clicked;
}

bool Context::toolButton(const char* id, icons::Id icon, const std::string& label, bool active, const char* tip, const char* shortcut) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    int small = std::max(1, m.th.fontScale - 1);   // the label under the icon is one step smaller: the button is 44 px wide
    SDL_Rect r = m.alloc(40, 44, true);
    Hit h = m.hit(wid, r, true);
    if (m.visible(r)) {
        m.buttonFrame(r, h, true, active);
        uint32_t c = m.stateText(true, active);
        m.pushClip(inset(r, 1));
        int lh = font::height(m.th.face, small), top = r.y + std::max(2, (r.h - 16 - 2 - lh) / 2);
        m.ico(icon, r.x + (r.w - 16) / 2, top, c);
        int tw = m.textW(label, small);   // 1 px margins: a seven-letter label (41 px at 1x) fits the 44 px button
        m.txt(label, r.x + std::max(1, (r.w - tw) / 2), top + 16 + 2, c, tw > r.w - 2 ? std::max(0, r.w - 2) : -1, 255, false, small);
        if (shortcut && std::strlen(shortcut) <= 2) {
            int sw = font::width(shortcut, font::Face::Small, 1);
            m.txt(shortcut, r.x + r.w - sw - 3, r.y + 2, c, -1, active ? 200 : 140, false, 1, font::Face::Small);
        }
        m.popClip();
    }
    m.tipFor(h, wid, r, tip, shortcut);
    return h.clicked;
}

bool Context::toggle(const char* id, const std::string& label, bool& value, const char* tip, bool enabled) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    const int sw = 36, sh = 18;
    SDL_Rect r = m.alloc(m.th.rowH, m.textW(label) + m.th.gap * 2 + sw, true);
    Hit h = m.hit(wid, r, enabled);
    bool changed = false;
    if (h.clicked) { value = !value; changed = true; m.editedFlag = true; }
    if (m.visible(r)) {
        SDL_Rect sw_r{r.x + r.w - sw, r.y + (r.h - sh) / 2, sw, sh};
        uint32_t track = !enabled ? m.th.surface2 : value ? (h.hover ? m.th.focus : m.th.accent)
                                                           : (h.hover ? m.th.borderLight : m.th.border);
        m.fill(sw_r, track, 255, sh / 2);
        if (!enabled) m.stroke(sw_r, m.th.border, 255, sh / 2);
        int kx = value ? sw_r.x + sw - sh + 2 : sw_r.x + 2;
        m.fill({kx, sw_r.y + 2, sh - 4, sh - 4}, !enabled ? m.th.textDisabled : value ? m.th.textOnAccent : m.th.text, 255, (sh - 4) / 2);
        m.txt(label, r.x, m.textY(r), enabled ? m.th.text : m.th.textDisabled, std::max(0, r.w - sw - m.th.gap * 2));
    }
    m.tipFor(h, wid, r, tip, nullptr);
    return changed;
}

bool Context::checkbox(const char* id, const std::string& label, bool& value, const char* tip, bool enabled) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    const int bs = 18;
    SDL_Rect r = m.alloc(m.th.rowH, bs + m.th.gap * 2 + m.textW(label), true);
    Hit h = m.hit(wid, r, enabled);
    bool changed = false;
    if (h.clicked) { value = !value; changed = true; m.editedFlag = true; }
    if (m.visible(r)) {
        SDL_Rect b{r.x, r.y + (r.h - bs) / 2, bs, bs};
        uint32_t bg = !enabled ? m.th.surface : value ? m.th.accent : (h.hover ? m.th.hover : m.th.surface2);
        m.fill(b, bg, 255, m.th.radius);
        m.stroke(b, !enabled ? m.th.border : value ? m.th.accent : h.hover ? m.th.borderLight : m.th.border, 255, m.th.radius);
        if (value) m.ico(icons::Check, b.x + 1, b.y + 1, enabled ? m.th.textOnAccent : m.th.textDisabled);
        m.txt(label, b.x + bs + m.th.gap * 2, m.textY(r), enabled ? m.th.text : m.th.textDisabled, std::max(0, r.w - bs - m.th.gap * 2));
    }
    m.tipFor(h, wid, r, tip, nullptr);
    return changed;
}

int Context::segmented(const char* id, const std::vector<std::string>& options, int current, const char* tip, bool enabled) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    int n = (int)options.size();
    if (n == 0) return current;
    int widest = 0;
    for (const auto& o : options) widest = std::max(widest, m.textW(o));
    SDL_Rect r = m.alloc(m.th.rowH, n * (widest + 2 * m.th.pad), true);
    int result = current;
    Hit any;
    if (m.visible(r)) { m.fill(r, enabled ? m.th.surface2 : m.th.surface, 255, m.th.radius); }
    for (int i = 0; i < n; ++i) {
        int x0 = r.x + r.w * i / n, x1 = r.x + r.w * (i + 1) / n;
        SDL_Rect c{x0, r.y, x1 - x0, r.h};
        Hit h = m.hit(hashInt(wid, i), c, enabled);
        if (h.over) any = h;
        if (h.clicked && i != current) { result = i; m.editedFlag = true; }
        if (!m.visible(c)) continue;
        bool sel = i == current;
        if (sel) m.fill(inset(c, 1), enabled ? m.th.accent : m.th.border, 255, m.th.radius);
        else if (h.hover) m.fill(inset(c, 1), m.th.hover, 255, m.th.radius);
        if (i > 0 && !sel && i - 1 != current) m.fill({c.x, c.y + 6, 1, c.h - 12}, m.th.border);
        int tw = m.textW(options[(size_t)i]);   // short labels: clip rather than ellipsize
        m.pushClip(inset(c, 1));
        uint32_t tc = !enabled ? m.th.textDisabled : sel ? m.th.textOnAccent : h.hover ? m.th.text : m.th.textDim;
        m.txt(options[(size_t)i], c.x + (c.w - tw) / 2, m.textY(c), tc);
        m.popClip();
    }
    if (m.visible(r)) m.stroke(r, m.th.border, 255, m.th.radius);
    m.lastId = wid; m.lastRect = r; m.lastOver = any.over;
    m.tipFor(any, wid, r, tip, nullptr);
    return result;
}

// ---- values

bool Context::dragFloat(const char* id, const std::string& label, float& v, float step, float lo, float hi, const char* unit, int decimals,
                        const char* tip, bool enabled) {
    double d = v;
    bool changed = impl_->valueField(id, label, d, step, lo, hi, unit, decimals, tip, enabled, false);
    v = (float)d;
    return changed;
}

bool Context::dragInt(const char* id, const std::string& label, int& v, int step, int lo, int hi, const char* unit, const char* tip, bool enabled) {
    double d = v;
    bool changed = impl_->valueField(id, label, d, step, lo, hi, unit, 0, tip, enabled, true);
    v = (int)std::lround(d);
    return changed;
}

bool Context::slider(const char* id, const std::string& label, float& v, float lo, float hi, const char* fmt, const char* tip, bool enabled) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    SDL_Rect r = m.alloc(m.th.fieldH), lab, ctl;
    m.splitLabel(r, label, lab, ctl);
    char buf[48];
    std::snprintf(buf, sizeof buf, fmt ? fmt : "%.1f", (double)v);
    std::string vs = buf;
    int vw = m.textW(vs) + m.th.gap;
    SDL_Rect track{ctl.x + m.th.gap, ctl.y, std::max(0, ctl.w - vw - 2 * m.th.gap), ctl.h};
    Hit h = m.hit(wid, track, enabled);
    float before = v;
    if (h.pressed || (m.active == wid && m.in.lDown && track.w > 1)) {
        float t = clampd((m.in.mx - track.x) / double(std::max(1, track.w - 1)), 0, 1);
        v = lo + (hi - lo) * t;
    }
    if (h.released) m.editedFlag = true;
    if (h.hover && m.in.wheel != 0) { v += m.in.wheel * (hi - lo) / 100; m.editedFlag = true; m.wheelConsumed = true; }   // 1% per notch
    v = (float)clampd(v, std::min(lo, hi), std::max(lo, hi));
    bool changed = v != before;
    if (m.visible(r)) {
        if (lab.w > 0) m.txt(label, lab.x, m.textY(lab), enabled ? m.th.textDim : m.th.textDisabled, std::max(0, lab.w - m.th.gap));
        int cy = track.y + track.h / 2, th_ = 4;
        float t = hi != lo ? (v - lo) / (hi - lo) : 0;
        int kx = track.x + (int)std::lround(t * std::max(0, track.w - 1));
        m.fill({track.x, cy - th_ / 2, track.w, th_}, enabled ? m.th.border : m.th.surface2, 255, 2);
        m.fill({track.x, cy - th_ / 2, kx - track.x, th_}, enabled ? m.th.accent : m.th.border, 255, 2);
        bool held = m.active == wid;
        int ks = held ? 16 : 14;
        uint32_t kc = !enabled ? m.th.textDisabled : held ? m.th.focus : h.hover ? m.th.text : m.th.textDim;
        m.fill({kx - ks / 2, cy - ks / 2, ks, ks}, kc, 255, ks / 2);
        if (fmt) {
            std::snprintf(buf, sizeof buf, fmt, (double)v);
            vs = buf;
            m.txt(vs, ctl.x + ctl.w - m.textW(vs) - m.th.gap, m.textY(ctl), enabled ? m.th.text : m.th.textDisabled);
        }
    }
    m.tipFor(h, wid, r, tip, nullptr);
    return changed;
}

bool Context::textField(const char* id, std::string& text, const char* placeholder, bool* submitted, bool* cancelled, int maxLen) {
    Impl& m = *impl_;
    if (submitted) *submitted = false;
    if (cancelled) *cancelled = false;
    SDL_Rect r = m.alloc(m.th.fieldH);
    return m.textEdit(m.id(id), r, text, placeholder, submitted, cancelled, maxLen);
}

bool Context::dropdown(const char* id, const std::string& label, const std::vector<std::string>& options, int& current, const char* tip,
                       bool enabled) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    SDL_Rect r = m.alloc(m.th.fieldH), lab, ctl;
    m.splitLabel(r, label, lab, ctl);
    WState& s = m.st[wid];
    Hit h = m.hit(wid, ctl, enabled);
    if (h.clicked) { s.open = !s.open; s.hl = current; }
    bool open = s.open && enabled;
    if (m.visible(r)) {
        if (lab.w > 0) m.txt(label, lab.x, m.textY(lab), enabled ? m.th.textDim : m.th.textDisabled, std::max(0, lab.w - m.th.gap));
        m.fill(ctl, !enabled ? m.th.surface : (h.hover || open) ? m.th.hover : m.th.surface2, 255, m.th.radius);
        if (open) m.ring(ctl, m.th.focus); else m.stroke(ctl, h.hover ? m.th.borderLight : m.th.border, 255, m.th.radius);
        uint32_t c = enabled ? m.th.text : m.th.textDisabled;
        if (current >= 0 && current < (int)options.size())
            m.txt(options[(size_t)current], ctl.x + m.th.pad, m.textY(ctl), c, std::max(0, ctl.w - 2 * m.th.pad - 20));
        m.ico(open ? icons::ChevronUp : icons::ChevronDown, ctl.x + ctl.w - 16 - m.th.gap, ctl.y + (ctl.h - 16) / 2,
              enabled ? m.th.textDim : m.th.textDisabled);
    }
    m.tipFor(h, wid, r, tip, nullptr);
    bool changed = false;
    if (open) {
        uint64_t pid = wid ^ 0x3;
        int n = (int)options.size(), maxH = 0;
        bool above = false, keep = true;
        SDL_Rect pr = m.placePopup(pid, ctl, ctl.w, maxH, above);
        if (m.beginOverlay(pid, pr, false, true, keep, 4, true, maxH, above)) {
            if (m.keysAvail()) for (SDL_Keycode k : m.in.keys) {
                if (k == SDLK_DOWN) s.hl = n ? (s.hl + 1) % n : -1;
                else if (k == SDLK_UP) s.hl = n ? (clampi(s.hl, 0, n) - 1 + n) % n : -1;
                else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && s.hl >= 0 && s.hl < n) {
                    if (s.hl != current) { current = s.hl; changed = true; }
                    keep = false;
                }
            }
            for (int i = 0; i < n; ++i) {
                SDL_Rect ir = m.menuRow();
                Hit ih = m.hit(hashInt(pid, i), ir, true);
                if (ih.over) s.hl = i;
                if (ih.clicked) { if (i != current) { current = i; changed = true; } keep = false; }
                if (!m.visible(ir)) continue;
                if (i == s.hl) m.fill(ir, m.th.hover, 255, m.th.radius);
                if (i == current) m.ico(icons::Check, ir.x + 2, ir.y + (ir.h - 16) / 2, m.th.accent);
                m.txt(options[(size_t)i], ir.x + 16 + m.th.gap * 2, m.textY(ir), i == current ? m.th.accent : m.th.text,
                      std::max(0, ir.w - 16 - 3 * m.th.gap));
            }
            m.endOverlay(true);
        }
        if (!keep) { s.open = false; m.dropOverlay(pid); }
        if (changed) m.editedFlag = true;
    }
    return changed;
}

bool Context::edited() const { return impl_->editedFlag; }

// ---- structure

bool Context::section(const char* id, const std::string& title, bool defaultOpen, const char* badge) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    WState& s = m.st[wid];
    if (!s.openInit) { s.open = defaultOpen; s.openInit = true; }
    SDL_Rect r = m.alloc(m.th.sectionH);
    Hit h = m.hit(wid, r, true);
    if (h.clicked) s.open = !s.open;
    if (m.visible(r)) {
        m.fill(r, h.hover ? m.th.hover : m.th.surface2, 255, m.th.radius);
        m.ico(s.open ? icons::ChevronDown : icons::ChevronRight, r.x + 2, r.y + (r.h - 16) / 2, m.th.textDim);
        int bw = badge ? m.textW(badge) : 0;
        m.txt(title, r.x + 16 + m.th.gap * 2, m.textY(r), m.th.text, std::max(0, r.w - 16 - 3 * m.th.gap - bw - m.th.gap), 255, true);
        if (bw) m.txt(badge, r.x + r.w - bw - m.th.gap * 2, m.textY(r), m.th.textDim);
    }
    return s.open;
}

int Context::tabs(const char* id, const std::vector<std::string>& names, int current) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    int n = (int)names.size();
    if (n == 0) return current;
    SDL_Rect r = m.alloc(m.th.buttonH);
    int result = current;
    if (m.visible(r)) m.fill({r.x, r.y + r.h - 1, r.w, 1}, m.th.border);
    // names that would be cut at the interface scale are all drawn one step smaller instead, so no tab name ends in an ellipsis
    int scale = m.th.fontScale;
    for (const std::string& nm : names)
        if (m.textW(nm) > r.w / n - 2 * m.th.gap) { scale = std::max(1, m.th.fontScale - 1); break; }
    const int th = font::height(m.th.face, scale);
    for (int i = 0; i < n; ++i) {
        SDL_Rect t{r.x + r.w * i / n, r.y, r.w * (i + 1) / n - r.w * i / n, r.h};
        Hit h = m.hit(hashInt(wid, i), t, true);
        if (h.clicked) result = i;
        if (!m.visible(t)) continue;
        bool sel = i == current;
        if (sel) m.fill({t.x, t.y, t.w, t.h - 1}, m.th.selection, 255, m.th.radius);
        else if (h.hover) m.fill({t.x, t.y, t.w, t.h - 1}, m.th.hover, 255, m.th.radius);
        if (sel) m.fill({t.x + 2, t.y + t.h - 2, t.w - 4, 2}, m.th.accent);
        int tw = std::min(m.textW(names[(size_t)i], scale), std::max(0, t.w - 2 * m.th.gap));
        uint32_t tc = sel || h.hover ? m.th.text : m.th.textDim;
        m.txt(names[(size_t)i], t.x + (t.w - tw) / 2, t.y + (t.h - th) / 2, tc, tw, 255, false, scale);
    }
    return result;
}

int Context::swatchGrid(const char* id, const std::vector<SwatchItem>& items, int current, int cell, bool showNames) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    int n = (int)items.size();
    Panel& p = m.cur();
    cell = std::max(1, cell);
    int availW = p.rowCols > 0 ? p.inner.w / p.rowCols : p.inner.w;
    int cols = std::max(1, (availW + m.th.gap) / (cell + m.th.gap)), rows = (n + cols - 1) / cols;
    int cellH = cell + (showNames ? m.lineH() : 0);
    SDL_Rect r = m.alloc(std::max(0, rows * (cellH + m.th.gap) - m.th.gap));
    int clicked = -1;
    Hit any;
    for (int i = 0; i < n; ++i) {
        SDL_Rect c{r.x + (i % cols) * (cell + m.th.gap), r.y + (i / cols) * (cellH + m.th.gap), cell, cell};
        SDL_Rect hitR{c.x, c.y, cell, cellH};
        Hit h = m.hit(hashInt(wid, i), hitR, true);
        if (h.clicked) clicked = i;
        if (h.over) {
            const SwatchItem& it = items[(size_t)i];
            any = h; m.tipFor(h, hashInt(wid, i), hitR, it.tip.empty() ? it.name.c_str() : it.tip.c_str(), nullptr);
        }
        if (!m.visible(hitR)) continue;
        m.fill(c, items[(size_t)i].color, 255, m.th.radius);
        if (i == current) { m.ring(c, m.th.accent); }
        else m.stroke(c, h.hover ? m.th.text : m.th.borderLight, 255, m.th.radius);
        if (showNames) m.txt(items[(size_t)i].name, c.x, c.y + cell + m.th.gap / 2, i == current ? m.th.text : m.th.textDim, cell);
    }
    m.lastId = wid; m.lastRect = r; m.lastOver = any.over;
    return clicked;
}

int Context::listView(const char* id, const std::vector<std::string>& items, int selected, int visibleRows,
                      const std::vector<std::string>* details) {
    Impl& m = *impl_;
    uint64_t wid = m.id(id);
    WState& s = m.st[wid];
    int n = (int)items.size(), rowH = m.th.rowH;
    SDL_Rect r = m.alloc(std::max(1, visibleRows) * rowH + 2);
    SDL_Rect inner = inset(r, 1);
    int contentH = n * rowH, maxScroll = std::max(0, contentH - inner.h);
    bool canScroll = maxScroll > 0;
    if (m.listFocus == wid) m.listFocusSeen = true;
    Hit box;   // only "is the pointer over the list": the rows claim the press themselves
    box.over = m.pointerAvail() && contains(m.clip(), m.in.mx, m.in.my) && contains(inner, m.in.mx, m.in.my);
    if (box.over && m.in.lPressed) { m.listFocus = wid; m.listFocusSeen = true; }
    else if (m.in.lPressed && !box.over && m.listFocus == wid && m.pointerAvail()) m.listFocus = 0;
    int result = -1;
    if (m.listFocus == wid && n > 0 && m.keysAvail()) {   // keyboard: arrows move the selection, Enter activates
        int sel = clampi(selected, -1, n - 1);   // a stale selection from the caller must not step out of the list
        for (SDL_Keycode k : m.in.keys) {
            if (k == SDLK_DOWN) sel = std::min(n - 1, sel + 1);
            else if (k == SDLK_UP) sel = std::max(0, sel - 1);
            else if (k == SDLK_HOME) sel = 0;
            else if (k == SDLK_END) sel = n - 1;
            else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && selected >= 0) m.listActivatedFlag = true;
        }
        if (sel != selected && sel >= 0) {   // keep the new selection in view
            result = sel; selected = sel;
            s.scrollY = clampi(s.scrollY, sel * rowH + rowH - inner.h, sel * rowH);
        }
    }
    if (box.over && m.in.wheel != 0 && canScroll && !m.wheelConsumed) { s.scrollY -= m.in.wheel * rowH * 2; m.wheelConsumed = true; }
    s.scrollY = clampi(s.scrollY, 0, maxScroll);
    SDL_Rect rowsR = inner;
    if (canScroll) rowsR.w = std::max(0, rowsR.w - m.th.scrollbarW);
    if (m.visible(r)) { m.fill(r, m.th.surface, 255, m.th.radius); }
    m.pushClip(inner);
    for (int i = 0; i < n; ++i) {
        SDL_Rect ir{rowsR.x, rowsR.y + i * rowH - s.scrollY, rowsR.w, rowH};
        if (ir.y + ir.h <= inner.y || ir.y >= inner.y + inner.h) continue;
        Hit h = m.hit(hashInt(wid, i), ir, true);
        if (h.clicked) { result = i; m.listFocus = wid; m.listFocusSeen = true; }
        if (h.pressed && h.dbl) { m.listActivatedFlag = true; result = i; }
        bool sel = i == selected;
        if (sel) { m.fill(ir, m.th.selection); m.fill({ir.x, ir.y, 3, ir.h}, m.listFocus == wid ? m.th.accent : m.th.borderLight); }
        else if (h.hover) m.fill(ir, m.th.hover);
        int dw = 0;
        if (details && i < (int)details->size()) {
            dw = std::min(m.textW((*details)[(size_t)i]), ir.w * 2 / 5);
            m.txt((*details)[(size_t)i], ir.x + ir.w - dw - m.th.pad, m.textY(ir), m.th.textDim, dw);
        }
        m.txt(items[(size_t)i], ir.x + m.th.pad, m.textY(ir), m.th.text, std::max(0, ir.w - 2 * m.th.pad - (dw ? dw + m.th.gap : 0)));
    }
    if (canScroll) {
        SDL_Rect lane{inner.x + inner.w - m.th.scrollbarW, inner.y, m.th.scrollbarW, inner.h};
        m.scrollbar(wid ^ 0x5, lane, inner.h, contentH, s.scrollY);
    }
    m.popClip();
    if (m.visible(r)) m.stroke(r, m.listFocus == wid ? m.th.borderLight : m.th.border, 255, m.th.radius);
    m.lastId = wid; m.lastRect = r; m.lastOver = box.over;
    return result;
}

bool Context::listActivated() const { return impl_->listActivatedFlag; }

// ---- overlays

void Context::tooltip(const std::string& text) {
    Impl& m = *impl_;
    if (!m.lastOver || m.lastId == 0) return;
    m.tipId = m.lastId; m.tipRect = m.lastRect; m.tipText = text; m.tipShortcut.clear();
}

void Context::toast(const std::string& text, int ms, TextStyle style) {
    Impl& m = *impl_;
    if (m.toasts.size() >= 5) m.toasts.erase(m.toasts.begin());
    m.toasts.push_back({text, m.in.ticks + (uint32_t)std::max(1, ms), style});
}

void Context::statusHint(const std::string& text) { if (impl_->lastOver) impl_->hintText = text; }
std::string Context::hoveredTip() const {
    const Impl& m = *impl_;
    return !m.hintText.empty() ? m.hintText : (m.tipId ? m.tipText : std::string());
}

bool Context::beginPopup(const char* id, SDL_Rect anchor, int w, bool& open) {
    Impl& m = *impl_;
    uint64_t pid = m.id(id);
    if (!open) { m.st[pid].open = false; return false; }
    int maxH = 0;
    bool above = false;
    SDL_Rect r = m.placePopup(pid, anchor, w, maxH, above);
    return m.beginOverlay(pid, r, false, true, open, m.th.pad, true, maxH, above);
}
void Context::endPopup() { if (!impl_->ovStack.empty()) { impl_->closeOverlayPanels(); impl_->endOverlay(true); } }

int Context::contextMenu(const char* id, bool& open, int atX, int atY, const std::vector<MenuItem>& items) {
    Impl& m = *impl_;
    uint64_t pid = m.id(id);
    if (!open) { m.st[pid].open = false; return -1; }
    WState& s = m.st[pid];
    bool column = false;
    int w = 0;
    for (const MenuItem& it : items) {
        column = column || it.icon != icons::None || it.checked;
        w = std::max(w, m.textW(it.label) + (it.shortcut.empty() ? 0 : m.textW(it.shortcut) + 3 * m.th.pad));
    }
    int left = m.th.pad + (column ? 16 + m.th.gap * 2 : 0);
    w = clampi(w + left + m.th.pad + 8, 160, 400);
    int maxH = 0;
    bool above = false;
    SDL_Rect r = m.placePopup(pid, {atX, atY, 0, 0}, w, maxH, above);
    int chosen = -1, n = (int)items.size();
    if (!m.beginOverlay(pid, r, false, true, open, 4, true, maxH, above)) return -1;
    bool keep = true;
    if (m.keysAvail()) for (SDL_Keycode k : m.in.keys) {   // arrows skip disabled items; from no highlight they start at either end
        int dir = k == SDLK_DOWN ? 1 : k == SDLK_UP ? -1 : 0;
        if (dir) {
            int i = s.hl >= 0 && s.hl < n ? s.hl : (dir > 0 ? -1 : 0);
            for (int t = 0; t < n; ++t) { i = ((i + dir) % n + n) % n; if (items[(size_t)i].enabled) { s.hl = i; break; } }
        } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && s.hl >= 0 && s.hl < n && items[(size_t)s.hl].enabled) {
            chosen = s.hl; keep = false;
        }
    }
    for (int i = 0; i < n; ++i) {
        const MenuItem& it = items[(size_t)i];
        SDL_Rect ir = m.menuRow();
        Hit h = m.hit(hashInt(pid, i), ir, it.enabled);
        if (h.over) s.hl = it.enabled ? i : -1;
        if (h.clicked) { chosen = i; keep = false; }
        if (m.visible(ir)) {
            if (i == s.hl && it.enabled) m.fill(ir, m.th.hover, 255, m.th.radius);
            uint32_t c = it.enabled ? m.th.text : m.th.textDisabled;
            int iy = ir.y + (ir.h - 16) / 2;
            if (it.checked) m.ico(icons::Check, ir.x + m.th.gap, iy, it.enabled ? m.th.accent : m.th.textDisabled);
            else if (it.icon != icons::None) m.ico(it.icon, ir.x + m.th.gap, iy, it.enabled ? m.th.textDim : m.th.textDisabled);
            int sw = it.shortcut.empty() ? 0 : m.textW(it.shortcut);
            m.txt(it.label, ir.x + left - m.th.pad + m.th.gap, m.textY(ir), c, std::max(0, ir.w - left - sw - 2 * m.th.pad));
            if (sw) m.txt(it.shortcut, ir.x + ir.w - sw - m.th.pad, m.textY(ir), it.enabled ? m.th.textDim : m.th.textDisabled);
        }
        if (it.separatorAfter && i + 1 < n) {
            SDL_Rect sr = m.alloc(m.th.gap + 1);
            if (m.visible(sr)) m.fill({sr.x, sr.y + m.th.gap / 2, sr.w, 1}, m.th.border);
        }
    }
    m.endOverlay(true);
    if (!keep) { open = false; s.open = false; m.dropOverlay(pid); }
    return chosen;
}

bool Context::beginModal(const char* id, const std::string& title, int w, int h, bool& open, bool closeButton) {
    Impl& m = *impl_;
    uint64_t pid = m.id(id);
    if (!open) { m.st[pid].open = false; return false; }
    w = std::min(w, m.winW - 2 * m.th.pad); h = std::min(h, m.winH - 2 * m.th.pad);
    SDL_Rect r{(m.winW - w) / 2, (m.winH - h) / 2, w, h};
    int titleH = m.th.sectionH + 2 * m.th.gap;
    if (!m.beginOverlay(pid, r, true, false, open, 0, false, r.h, false)) return false;
    // the title bar sits above the content panel; the content panel is a nested panel of the overlay's own panel
    SDL_Rect bar{r.x, r.y, r.w, titleH};
    m.fill({bar.x + 1, bar.y + titleH - 1, bar.w - 2, 1}, m.th.border);
    m.txt(title, bar.x + m.th.pad, m.textY(bar), m.th.text, std::max(0, bar.w - 2 * m.th.pad - (closeButton ? 28 : 0)), 255, true);
    if (closeButton) {
        SDL_Rect cb{bar.x + bar.w - 24 - m.th.gap, bar.y + (titleH - 24) / 2, 24, 24};
        Hit ch = m.hit(pid ^ 0xC, cb, true);
        if (ch.hover) m.fill(cb, ch.down ? m.th.accentDim : m.th.hover, 255, m.th.radius);
        m.ico(icons::Close, cb.x + 4, cb.y + 4, ch.hover ? m.th.text : m.th.textDim);
        m.tipFor(ch, pid ^ 0xC, cb, "Close", "Esc");
        if (ch.clicked) { open = false; m.endOverlay(false); m.st[pid].open = false; m.dropOverlay(pid); return false; }
    }
    m.cur().y = r.y + titleH;   // the overlay panel itself only hosts the content panel
    m.beginPanelImpl(pid ^ 0xD, {r.x + 1, r.y + titleH, r.w - 2, std::max(0, r.h - titleH - 1)}, true, false, m.th.pad);
    return true;
}

void Context::endModal() {
    Impl& m = *impl_;
    if (m.ovStack.empty()) return;
    m.closeOverlayPanels();   // the content panel, and anything the caller left open inside it
    m.endOverlay(false);
}

void Context::palette(const char* id, bool& open, std::vector<Command>& commands, std::string& query) {
    Impl& m = *impl_;
    uint64_t pid = m.id(id), fieldId = pid ^ 0xE;
    WState& s = m.st[pid];
    if (!open) { if (s.open) { s.open = false; if (m.focus == fieldId) m.blur(); } return; }
    bool justOpened = !s.open;
    if (justOpened) { query.clear(); m.takeFocus(fieldId); s.hl = 0; }
    // results: fuzzy matches by score, recently run ones first, at most ten
    m.palRes.clear();
    for (int i = 0; i < (int)commands.size(); ++i) {
        const Command& c = commands[(size_t)i];
        int sc = query.empty() ? 1 : fuzzyScore(query, c.name);
        if (sc == 0 && query.find(' ') != std::string::npos)   // "view heat" may name the category
            sc = fuzzyScore(query, c.category + " " + c.name) / 2;
        if (sc <= 0) continue;
        int rank = 0;
        for (size_t k = 0; k < m.mru.size(); ++k) if (m.mru[k] == c.name) { rank = (int)(m.mru.size() - k); break; }
        m.palRes.push_back({-(rank * 100000 + sc * 1000 - std::min(i, 999)), i});
    }
    std::sort(m.palRes.begin(), m.palRes.end());
    int n = std::min((int)m.palRes.size(), 10);
    int w = std::min(560, m.winW - 2 * m.th.pad);
    int h = m.th.pad * 2 + m.th.fieldH + m.th.gap + std::max(1, n) * m.th.rowH;
    SDL_Rect r{(m.winW - w) / 2, std::min(64, std::max(0, (m.winH - h) / 3)), w, h};
    int run = -1;
    if (!m.beginOverlay(pid, r, true, true, open, m.th.pad, false, std::max(h, m.winH - r.y - m.th.pad), false)) {
        if (!open && m.focus == fieldId) m.blur();
        return;
    }
    s.hl = clampi(s.hl, 0, std::max(0, n - 1));   // after beginOverlay, which starts a new overlay with no highlight
    if (m.keysAvail()) for (SDL_Keycode k : m.in.keys) {
        if (k == SDLK_DOWN && n) s.hl = (s.hl + 1) % n;
        else if (k == SDLK_UP && n) s.hl = (s.hl - 1 + n) % n;
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && s.hl >= 0 && s.hl < n) run = m.palRes[(size_t)s.hl].second;
    }
    SDL_Rect fr = m.alloc(m.th.fieldH);
    m.ico(icons::Search, fr.x + m.th.gap + 2, fr.y + (fr.h - 16) / 2, m.th.textDim);
    SDL_Rect fieldR{fr.x + 16 + m.th.gap * 2, fr.y, fr.w - 16 - m.th.gap * 2, fr.h};
    m.textEdit(fieldId, fieldR, query, "Type a command...", nullptr, nullptr, 64);
    if (m.focus != fieldId && !justOpened) m.takeFocus(fieldId);   // the box keeps focus while the palette is open
    if (n == 0) {
        SDL_Rect er = m.menuRow();
        m.txt("No matching command", er.x + m.th.pad, m.textY(er), m.th.textDim);
    }
    for (int i = 0; i < n; ++i) {
        const Command& c = commands[(size_t)m.palRes[(size_t)i].second];
        bool enabled = !c.enabled || c.enabled();
        SDL_Rect ir = m.menuRow();
        Hit h = m.hit(hashInt(pid, i), ir, enabled);
        if (h.over && enabled) s.hl = i;
        if (h.clicked) run = m.palRes[(size_t)i].second;
        if (i == s.hl) m.fill(ir, enabled ? m.th.selection : m.th.hover, 255, m.th.radius);
        uint32_t tc = enabled ? m.th.text : m.th.textDisabled, dc = enabled ? m.th.textDim : m.th.textDisabled;
        int x = ir.x + m.th.gap;
        if (c.icon != icons::None) m.ico(c.icon, x + 2, ir.y + (ir.h - 16) / 2, dc);
        x += 16 + m.th.gap * 2;
        int sw = c.shortcut.empty() ? 0 : m.textW(c.shortcut), nw = m.textW(c.name);
        int avail = ir.w - (x - ir.x) - sw - 2 * m.th.pad;
        m.txt(c.name, x, m.textY(ir), tc, std::min(nw, avail));
        if (!c.category.empty() && nw + m.th.pad < avail)
            m.txt(c.category, x + nw + m.th.pad, m.textY(ir), dc, avail - nw - m.th.pad);
        if (sw) m.txt(c.shortcut, ir.x + ir.w - sw - m.th.pad, m.textY(ir), dc);
    }
    m.endOverlay(true);
    if (run >= 0) {
        Command& c = commands[(size_t)run];
        if (!c.enabled || c.enabled()) {
            m.mruPush(c.name);
            open = false; s.open = false; m.blur(); m.dropOverlay(pid);
            std::function<void()> fn = c.run;   // a copy: the command may rebuild the table it lives in
            if (fn) fn();
        }
    }
    if (!open && m.focus == fieldId) m.blur();
}

// ---- drawing helpers

void Context::fillRect(SDL_Rect r, uint32_t rgb, uint8_t a, int radius) { impl_->fill(r, rgb, a, radius); }
void Context::strokeRect(SDL_Rect r, uint32_t rgb, uint8_t a, int radius) { impl_->stroke(r, rgb, a, radius); }
void Context::text(const std::string& s, int x, int y, TextStyle style, int scale) {
    impl_->txt(s, x, y, impl_->styleColor(style), -1, 255, impl_->styleBold(style), scale);
}
int Context::textWidth(const std::string& s, int scale) const { return impl_->textW(s, scale); }
void Context::icon(icons::Id id, int x, int y, uint32_t rgb, int scale) { impl_->ico(id, x, y, rgb, 255, scale); }
void Context::shade(SDL_Rect r, uint8_t a) { impl_->fill(r, impl_->th.overlayShade, a); }

int Context::lineHeight() const { return impl_->lineH(); }
int Context::buttonHeight() const { return impl_->th.buttonH; }
int Context::fieldHeight() const { return impl_->th.fieldH; }

// Not in the header: how many renderer calls the last finished frame made, for profiling in the gallery.
int debugDrawCalls() { return g_lastFrameCalls; }

// ---------------------------------------------------------------------------------------------------------------- fuzzy

int fuzzyScore(const std::string& query, const std::string& text) {
    if (query.find_first_not_of(' ') == std::string::npos) return 1;
    auto lower = [](char c) { return (char)std::tolower((unsigned char)c); };
    auto wordStart = [&](size_t i) { return i == 0 || !std::isalnum((unsigned char)text[i - 1]); };
    // two passes: one that jumps to word starts when it can, one plain; the better of the two counts
    auto run = [&](bool preferStarts) {
        int score = 0, prev = -2;
        size_t pos = 0;
        for (char qc : query) {
            if (qc == ' ') continue;
            qc = lower(qc);
            size_t found = std::string::npos;
            if (preferStarts) for (size_t i = pos; i < text.size(); ++i) if (lower(text[i]) == qc && wordStart(i)) { found = i; break; }
            if (found == std::string::npos) for (size_t i = pos; i < text.size(); ++i) if (lower(text[i]) == qc) { found = i; break; }
            if (found == std::string::npos) return 0;
            int s = 2;
            if (wordStart(found)) s += 8;
            if ((int)found == prev + 1) s += 6;
            if (found == 0) s += 4;
            s -= std::min(3, (int)(found - pos));
            score += s; prev = (int)found; pos = found + 1;
        }
        return std::max(1, score - (int)text.size() / 8);
    };
    return std::max(run(true), run(false));
}

}  // namespace ui
