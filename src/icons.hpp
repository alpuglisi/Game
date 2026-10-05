#pragma once
// STAND-IN: replaced by the font/icons branch
// Placeholder icon set with the real enum: every icon is a 16 px rounded square carrying the first letter of its
// name, so the toolkit can be laid out and tested before the drawn icons arrive.
#include <SDL.h>
#include "font.hpp"

namespace icons {

enum Id { None, Select, Grab, Move, Measure, Box, Circle, Wheel, Rocket, Pin, Motor, Spinner, Rod, Spring, Slider, Bond, Fan, Emitter,
          Pipe, Hose, Paint, Eraser, Cut, Delete, Group, Ungroup, Copy, Paste, Duplicate, FlipH, FlipV, Scale, Subtract, Undo, Redo,
          Play, Pause, Step, Stop, New, Open, Save, SaveAs, ZoomIn, ZoomOut, ZoomFit, Grid, Snap, Heat, Pressure, Electric, Focus,
          Scenes, Help, Settings, Search, Close, ChevronDown, ChevronRight, ChevronUp, Check, Warning, Info, Fixed, Gravity, Spark,
          Camera, Layers, Plus, Minus, Dot, ArrowLeft, ArrowRight, ArrowUp, ArrowDown, Count };

inline char letter(Id id) {
    static const char L[Count] = {' ', 'S', 'G', 'M', 'M', 'B', 'C', 'W', 'R', 'P', 'M', 'S', 'R', 'S', 'S', 'B', 'F', 'E',
                                  'P', 'H', 'P', 'E', 'C', 'D', 'G', 'U', 'C', 'P', 'D', 'F', 'F', 'S', 'S', 'U', 'R',
                                  '>', '|', '>', '[', 'N', 'O', 'S', 'S', '+', '-', 'Z', '#', 'S', 'H', 'P', 'E', 'F',
                                  'S', '?', 'S', 'S', 'X', 'V', '>', 'A', 'V', '!', 'I', 'F', 'G', 'S',
                                  'C', 'L', '+', '-', '.', '<', '>', 'A', 'V'};
    return (id >= 0 && id < Count) ? L[id] : '?';
}

// Draws a 16 px (times scale) placeholder at x, y in colour c.
inline void draw(SDL_Renderer* r, Id id, int x, int y, int scale, SDL_Color c) {
    if (id == None || id >= Count) return;
    int s = 16 * scale;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect box[3] = {{x + scale, y, s - 2 * scale, s}, {x, y + scale, scale, s - 2 * scale}, {x + s - scale, y + scale, scale, s - 2 * scale}};
    SDL_RenderDrawRects(r, box, 1);
    SDL_RenderFillRects(r, box + 1, 2);
    font::draw(r, std::string(1, letter(id)), x + (s - 5 * scale) / 2, y + (s - 7 * scale) / 2, scale, c);
}

}  // namespace icons
