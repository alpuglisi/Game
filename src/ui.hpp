#pragma once
// A small immediate-mode toolkit drawn with SDL2 primitives: the application describes its widgets every frame, the
// toolkit lays them out inside panels, tracks hover / press / focus, draws them, and reports what happened. One theme
// table holds every colour and metric. Nothing here knows about the simulation.
//
// Conventions
//  - Every widget takes an id string unique within its panel (pushId / popId nest ids for repeated groups). Ids are
//    hashed; the same id in two frames is the same widget, which is how focus, drag and scroll state survive.
//  - Widgets flow top to bottom inside the current panel; row(n) splits the next row into n equal columns (or
//    weighted ones) and the next n widgets fill them left to right.
//  - A widget returns true in the frame its value changed or it was clicked. Numeric fields change their value in
//    place and also return true, so a caller can push an undo entry once (see edited()).
//  - Right mouse button: a click outside any popup closes popups; the application opens context menus itself.
//  - Esc closes the topmost popup or clears text-field focus; Enter commits a text field. Tab moves focus between
//    text and numeric fields in the order they were declared.
//  - The viewport belongs to the application: it should ignore mouse input while wantsMouse() is true and keyboard
//    input while wantsKeyboard() is true.
#include <SDL.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "font.hpp"
#include "icons.hpp"

namespace ui {

struct Theme {
    // colours (0xRRGGBB); alpha is applied by the widgets where needed
    // the palette from docs/UI_DESIGN.md section 8: every text colour clears WCAG AA on the surface it sits on
    uint32_t bg = 0x121417, surface = 0x1b1e23, surface2 = 0x252930, hover = 0x2e333b, border = 0x3a4049, borderLight = 0x4a515c;
    uint32_t accent = 0x5aa9ff, accentDim = 0x2a3b52, focus = 0x8cc4ff, selection = 0x2a3b52, textOnAccent = 0x0e1116;
    uint32_t text = 0xe8eaed, textDim = 0xa6adb7, textDisabled = 0x6b737e;
    uint32_t success = 0x4cc38a, warning = 0xe6b450, danger = 0xff6b6b, info = 0x5aa9ff;
    uint32_t tooltipBg = 0x0c0e12, overlayShade = 0x000000;   // the shade behind a modal gets ~55% alpha
    // metrics in pixels, on a 4 px unit; interface text is the Ui face at 2x (a 12 x 16 cell)
    int pad = 8, gap = 4, rowH = 28, buttonH = 28, iconButton = 28, fieldH = 28, sectionH = 24, scrollbarW = 12, radius = 3;
    int tooltipDelayMs = 400;
    font::Face face = font::Face::Ui;
    int fontScale = 2;
};
Theme& theme();   // the one theme; the application may edit it at start-up

// the input of one frame, filled by the application from the SDL events it received
struct Input {
    int mx = 0, my = 0;                       // pointer in window pixels
    bool lDown = false, lPressed = false, lReleased = false;   // left button: held, went down this frame, went up this frame
    bool rDown = false, rPressed = false, rReleased = false;   // right
    bool mDown = false, mPressed = false, mReleased = false;   // middle
    bool lDouble = false;                     // the left press was a double click
    int wheel = 0;                            // wheel steps this frame (+ away from the user)
    bool ctrl = false, shift = false, alt = false;
    std::string typed;                        // text typed this frame (SDL_TEXTINPUT), UTF-8
    std::vector<SDL_Keycode> keys;            // key presses this frame (SDL_KEYDOWN, with repeat)
    uint32_t ticks = 0;                       // SDL_GetTicks()
    int winW = 0, winH = 0;
};

enum class TextStyle { Normal, Dim, Disabled, Heading, Section, Mono, Accent, Warning, Danger };
enum class Align { Left, Centre, Right };

struct SwatchItem { uint32_t color; std::string name; std::string tip; };       // a material chip
struct MenuItem { std::string label; std::string shortcut; icons::Id icon = icons::None; bool enabled = true, separatorAfter = false, checked = false; };
struct Command {   // what the command palette searches
    std::string name, category, shortcut, description;
    icons::Id icon = icons::None;
    std::function<bool()> enabled;            // may be empty = always
    std::function<void()> run;
};

class Context {
public:
    // ---- frame
    void begin(SDL_Renderer* ren, const Input& in);   // call once per frame before any widget
    void end();                                       // draws deferred layers (popups, tooltips, toasts); call after all widgets
    bool wantsMouse() const;                          // the pointer is over a panel, popup or modal (the viewport should ignore it)
    bool wantsKeyboard() const;                       // a text or numeric field has keyboard focus
    bool anyPopupOpen() const;
    const Input& input() const;
    SDL_Renderer* renderer() const;

    // ---- ids
    void pushId(const char* id);
    void pushId(int id);
    void popId();

    // ---- panels: a clipped region with a background, optional scrolling (wheel, scrollbar drag) and padding.
    // Widgets declared between beginPanel / endPanel flow inside it. Panels may nest.
    void beginPanel(const char* id, SDL_Rect r, bool scroll = true, bool background = true, int pad = -1);
    void endPanel();
    SDL_Rect panelRect() const;                       // the current panel's inner rect
    int contentHeight() const;                        // how tall the content of the current panel has become so far

    // ---- layout inside the current panel
    void row(int columns, int height = -1);           // the next `columns` widgets share one row of the given height (-1 = theme rowH)
    void row(const std::vector<float>& weights, int height = -1);   // columns with relative widths
    void space(int px);
    void separator();
    SDL_Rect next(int height);                        // reserve the next rect for custom drawing (viewport thumbnails, swatch previews)
    void sameLine();                                  // the next widget goes beside the previous one, sized to its content

    // ---- text
    void label(const std::string& text, TextStyle style = TextStyle::Normal, Align align = Align::Left);
    void labelWrapped(const std::string& text, TextStyle style = TextStyle::Normal);
    void keyValue(const std::string& key, const std::string& value);   // dim key on the left, value on the right

    // ---- buttons
    // tip: tooltip text (with the shortcut appended in a dim style when given); active: drawn pressed (the current tool)
    bool button(const char* id, const std::string& label, icons::Id icon = icons::None, const char* tip = nullptr,
                bool enabled = true, bool active = false, const char* shortcut = nullptr);
    bool iconButton(const char* id, icons::Id icon, const char* tip, bool enabled = true, bool active = false, const char* shortcut = nullptr);
    bool toolButton(const char* id, icons::Id icon, const std::string& label, bool active, const char* tip, const char* shortcut = nullptr);   // icon above a short label; for tool strips
    bool toggle(const char* id, const std::string& label, bool& value, const char* tip = nullptr, bool enabled = true);
    bool checkbox(const char* id, const std::string& label, bool& value, const char* tip = nullptr, bool enabled = true);
    int segmented(const char* id, const std::vector<std::string>& options, int current, const char* tip = nullptr, bool enabled = true);   // returns the new index

    // ---- values. All return true when the value changed this frame.
    // dragValue: drag left/right to scrub (Shift fine, Ctrl coarse), click to type (Enter commits, Esc cancels), +/- buttons at the ends,
    // wheel over it steps; clamped to [lo, hi]; `unit` is drawn dim after the number; decimals = digits shown.
    bool dragFloat(const char* id, const std::string& label, float& v, float step, float lo, float hi, const char* unit = "", int decimals = 1, const char* tip = nullptr, bool enabled = true);
    bool dragInt(const char* id, const std::string& label, int& v, int step, int lo, int hi, const char* unit = "", const char* tip = nullptr, bool enabled = true);
    bool slider(const char* id, const std::string& label, float& v, float lo, float hi, const char* fmt = "%.1f", const char* tip = nullptr, bool enabled = true);
    bool textField(const char* id, std::string& text, const char* placeholder = "", bool* submitted = nullptr, bool* cancelled = nullptr, int maxLen = 64);
    bool dropdown(const char* id, const std::string& label, const std::vector<std::string>& options, int& current, const char* tip = nullptr, bool enabled = true);
    bool edited() const;                              // the last value widget was committed this frame (release after a drag, Enter after typing): push undo once

    // ---- structure
    bool section(const char* id, const std::string& title, bool defaultOpen = true, const char* badge = nullptr);   // collapsible header; returns open
    int tabs(const char* id, const std::vector<std::string>& names, int current);
    int swatchGrid(const char* id, const std::vector<SwatchItem>& items, int current, int cell = 26, bool showNames = false);   // returns the clicked index, else -1
    int listView(const char* id, const std::vector<std::string>& items, int selected, int visibleRows, const std::vector<std::string>* details = nullptr);   // returns the clicked index, else -1 (double click sets *activated)
    bool listActivated() const;                       // the last listView item was double-clicked or Enter was pressed on it

    // ---- overlays (deferred to end())
    void tooltip(const std::string& text);           // attach to the last widget; shown after the delay while hovered
    void toast(const std::string& text, int ms = 3000, TextStyle style = TextStyle::Normal);
    void statusHint(const std::string& text);        // the application draws status bars itself; this just records the hint for the hovered widget
    std::string hoveredTip() const;                   // the tip of the hovered widget this frame, for a status bar

    // popups: anchored boxes above everything; close on Esc, on a click outside, or when the application says so
    bool beginPopup(const char* id, SDL_Rect anchor, int w, bool& open);   // positions below/right of the anchor, kept on screen
    void endPopup();
    int contextMenu(const char* id, bool& open, int atX, int atY, const std::vector<MenuItem>& items);   // returns the chosen index, else -1; closes itself on choice
    bool beginModal(const char* id, const std::string& title, int w, int h, bool& open, bool closeButton = true);   // centred card with a shaded backdrop
    void endModal();
    // the command palette: a search box over `commands`, fuzzy-matched, recently used first, with shortcuts shown; Enter runs, Esc closes
    void palette(const char* id, bool& open, std::vector<Command>& commands, std::string& query);

    // ---- drawing helpers for the application (panels, custom controls)
    void fillRect(SDL_Rect r, uint32_t rgb, uint8_t a = 255, int radius = 0);
    void strokeRect(SDL_Rect r, uint32_t rgb, uint8_t a = 255, int radius = 0);
    void text(const std::string& s, int x, int y, TextStyle style = TextStyle::Normal, int scale = -1);
    int textWidth(const std::string& s, int scale = -1) const;
    void icon(icons::Id id, int x, int y, uint32_t rgb, int scale = 1);
    void shade(SDL_Rect r, uint8_t a);                // darken a region

    // ---- measuring, so the application can size docks
    int lineHeight() const;
    int buttonHeight() const;
    int fieldHeight() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
public:
    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

// fuzzy match used by the palette and swatch search: returns a score > 0 when every character of `query` appears in
// order in `text` (case-insensitive), higher for word starts and consecutive runs; 0 = no match
int fuzzyScore(const std::string& query, const std::string& text);

}  // namespace ui
