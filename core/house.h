// Lebendes Haus: Hausquerschnitt mit Räumen, Fenstern, Heizung und Energiefluss
//
// Aufbau:
//  * Ein Canvas zeichnet alles, was sich selten ändert: Himmel, Dächer, Wände,
//    Solarmodule, Raumfarben (nach Temperatur), Garagen, Hof, Auto, Strommast.
//    Neu gezeichnet wird nur, wenn sich Tag/Nacht oder eine Temperatur ändert.
//  * Darüber liegen LVGL-Objekte für alles, was lebt: Fenster (Licht, Rollladen,
//    offen), Heizung (Glutlinie, Funken), Energiefluss (Punkte), Beschriftungen.
//  * Rechts eine Seitenleiste: Übersicht oder der angetippte Raum.
//
// Koordinaten unten sind die des Mockups (viewBox -24 0 692 640) und werden
// mit X()/Y() auf die 650 × 600 Pixel der Szene umgerechnet.
#pragma once

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <lvgl.h>
#include "esphome/core/log.h"

namespace house {

// ---------------------------------------------------------------------------
// Farben
// ---------------------------------------------------------------------------
constexpr uint32_t CARD = 0x182031, CARD2 = 0x222B3E, LINE = 0x2C3650, TEXT = 0xEEF2F8, MUTE = 0x8892A8, DIM = 0x5C667C;
constexpr uint32_t AMBER = 0xFFB547, SKY = 0x5CC8FF, GREEN = 0x4ADE80, PINK = 0xFF5C9A, HEAT = 0xFF9A55, RED = 0xFF4D4D;
constexpr uint32_t WALL = 0x1A2131, ROOF = 0x262E40, ROOF_LINE = 0x3A4560;

inline lv_color_t C(uint32_t c) { return lv_color_hex(c); }
inline lv_color_t mixc(uint32_t from, uint32_t to, float k) {
  k = std::max(0.0f, std::min(1.0f, k));
  return lv_color_mix(C(to), C(from), (uint8_t) lroundf(k * 255));
}

// ---------------------------------------------------------------------------
// Szene: Umrechnung Mockup -> Pixel
// ---------------------------------------------------------------------------
constexpr int SCENE_W = 650, SCENE_H = 600;
constexpr float S = 650.0f / 692.0f;
inline int32_t X(float x) { return (int32_t) lroundf((x + 24) * S); }
inline int32_t Y(float y) { return (int32_t) lroundf(y * S) - 1; }
inline int32_t D(float d) { return std::max<int32_t>(1, (int32_t) lroundf(d * S)); }

// ---------------------------------------------------------------------------
// Grundriss (vom Mockup übernommen)
// ---------------------------------------------------------------------------
enum WinType : uint8_t { WIN, DOOR, SLIDE, SIDE, TRIPLE, ROOF_S, ROOF_N };
enum Shape : uint8_t { RECT, ROOFROOM, GARAGE };
enum Stairs : uint8_t { NONE, UP, REV };
constexpr float AUTO = -1;

struct WinDef {
  WinType t;
  float fx = AUTO, ax = AUTO, ay = AUTO;  // Mitte als Anteil der Breite oder feste Position
  int8_t cover = -1, cover2 = -1, cover3 = -1;  // Rollläden (Dreierfenster: je Scheibe)
  int8_t sensor = -1;                     // Fensterkontakt
  float ww = 0, h = 0, pw = 0, d = 0;     // eigene Breite/Höhe, Scheibenbreite, Abstand auf dem Dach
};

struct RoomDef {
  const char *id, *name, *floor;
  float x, y, w, h;
  Shape shape;
  Stairs stairs;
  bool door;
  std::vector<WinDef> wins;
};

// Technikraum (Heizung): ohne Temperatur, Heizung und Raumansicht
inline bool technik(const RoomDef &d) { return strcmp(d.id, "heiz") == 0; }
constexpr uint32_t TECH_ROOM = 0x2A2F3B;

inline const std::vector<RoomDef> &rooms_def() {
  static const std::vector<RoomDef> R = {
      {"mila", "Mila", "Dachgeschoss", 180, 82, 408, 93, ROOFROOM, NONE, false,
       {{ROOF_S, AUTO, AUTO, AUTO, 0, -1, -1, -1, 0, 0, 0, 117},
        {ROOF_N, AUTO, AUTO, AUTO, 1, -1, -1, -1, 0, 0, 0, 75},
        {ROOF_N, AUTO, AUTO, AUTO, 2, -1, -1, -1, 0, 0, 0, 180},
        {WIN, AUTO, 256, 144, 3},
        {TRIPLE, AUTO, 384 - (22 * 3 + 4) / 2.0f, 122, 4, 4, 5, -1, 0, 50, 22}}},
      {"schlaf", "Schlafzimmer", "Obergeschoss", 314, 175, 187, 100, RECT, NONE, false,
       {{SLIDE, 0.25f, AUTO, AUTO, 1}, {WIN, 0.55f, AUTO, AUTO, 0}, {WIN, 0.69f, AUTO, AUTO, 2}, {WIN, 0.83f, AUTO, AUTO, 3}}},
      {"malo", "Malo", "Obergeschoss", 501, 175, 93, 100, RECT, NONE, false, {{DOOR, 0.62f, AUTO, AUTO, 0}}},
      {"bad_og", "Bad", "Obergeschoss", 244, 175, 70, 100, RECT, NONE, false, {{WIN, 0.55f, AUTO, AUTO, -1, -1, -1, 0}}},
      {"flur_og", "Flur", "Obergeschoss", 174, 175, 70, 100, RECT, REV, false, {}},
      {"anbau", "Anbau", "Obergeschoss", 84, 185, 90, 90, RECT, NONE, false,
       {{DOOR, AUTO, 86, AUTO, 1, -1, -1, -1, 16}, {TRIPLE, 0.59f, AUTO, AUTO, 0, 0, 0}}},
      {"wohnen", "Wohnzimmer", "Erdgeschoss", 484, 275, 110, 100, RECT, NONE, false, {{DOOR, 0.62f, AUTO, AUTO, 0}}},
      {"essen", "Esszimmer", "Erdgeschoss", 384, 275, 100, 100, RECT, NONE, false,
       {{WIN, 0.22f, AUTO, AUTO, 0}, {SLIDE, 0.66f, AUTO, AUTO, 1, -1, -1, 0}}},
      {"kueche", "Küche", "Erdgeschoss", 313, 275, 71, 100, RECT, NONE, false, {{WIN, 0.5f, AUTO, AUTO, 0}}},
      {"flur_eg", "Flur", "Erdgeschoss", 174, 275, 70, 100, RECT, UP, false, {}},
      {"bad_eg", "Bad", "Erdgeschoss", 244, 275, 69, 100, RECT, NONE, false, {{WIN, 0.5f, AUTO, AUTO, -1, -1, -1, 0}}},
      {"eingang", "Eingang", "Anbau · Erdgeschoss", 84, 275, 90, 100, RECT, UP, true,
       {{SIDE, AUTO, 101, 315}, {SIDE, AUTO, 143, 315}}},
      {"garage", "Garagen", "zwei Doppelgaragen", 4, 275, 78, 195, GARAGE, NONE, false, {}},
      {"wasch", "Waschküche", "Keller", 84, 375, 90, 95, RECT, REV, false, {{WIN, 0.5f, AUTO, AUTO, -1, -1, -1, 0}}},
      {"heiz", "Heizung", "Keller", 174, 375, 108, 95, RECT, NONE, false, {}},
      {"bad_k", "Bad", "Keller", 282, 375, 72, 95, RECT, NONE, false, {{DOOR, 0.5f, AUTO, AUTO, -1, -1, -1, 0}}},
      {"buero", "Gr. Büro", "Keller", 354, 375, 120, 95, RECT, NONE, false, {{WIN, 0.5f}}},
      {"buero2", "Kl. Büro", "Keller", 474, 375, 120, 95, RECT, NONE, false, {{DOOR, 0.6f, AUTO, AUTO, -1, -1, -1, 0}}},
  };
  return R;
}

// ---------------------------------------------------------------------------
// Zustand
// ---------------------------------------------------------------------------
struct Light { std::string name, entity; bool on = false, known = false, swing = true; };
struct Cover { std::string name, entity; float pos = NAN; int dir = 0; };
struct Contact { std::string name, entity; bool open = false; };
struct Circuit { std::string name; float level = 0; };

struct Pane;
struct RoomState {
  float temp = NAN, hum = NAN, target = NAN;
  std::string action;  // hvac_action des Thermostats
  std::vector<Light> lights;
  std::vector<Cover> covers;
  std::vector<Contact> contacts;
  std::vector<Circuit> circuits;
  bool shares_circuits = false;  // zeigt fremde Kreise (Küche, Eingang): nicht doppelt zählen
  bool smoke = false;
  // Objekte
  lv_obj_t *hit = nullptr, *name = nullptr, *temp_l = nullptr, *chip_i = nullptr, *chip_v = nullptr;
  lv_obj_t *heat_glow = nullptr, *heat_line = nullptr, *alarm = nullptr;
  lv_obj_t *layer = nullptr;  // Behälter für alle Objekte des Raums
  int32_t gx = 0, gy = 0;     // seine Lage in der Szene
  std::vector<lv_obj_t *> sparks;
  std::vector<Pane *> panes;
};

struct Energy {
  float pv = 0, hoy = 0, house = 0, battery = 0, grid = 0, heater = 0, soc = NAN, car = 0, car_today = NAN;
};

struct Ui {
  lv_obj_t *canvas = nullptr, *scene = nullptr, *side = nullptr, *subtitle = nullptr;
  const lv_font_t *f11b = nullptr, *f11 = nullptr, *f12 = nullptr, *f13 = nullptr, *f14 = nullptr, *f14b = nullptr,
                  *f20b = nullptr, *i11 = nullptr, *i16 = nullptr, *i20 = nullptr, *i22 = nullptr;
};

inline Ui ui;
inline std::vector<RoomState> rooms;
inline std::map<std::string, int> room_index;
inline Energy energy;
inline bool night = false, locked = false, canvas_dirty = true, side_dirty = true;
inline int selected = -1;
inline std::function<void(const std::string &, const std::string &)> call_action;

inline RoomState *room(const std::string &id) {
  auto it = room_index.find(id);
  return it == room_index.end() ? nullptr : &rooms[it->second];
}

// ---------------------------------------------------------------------------
// Canvas: Zeichenhilfen
// ---------------------------------------------------------------------------
inline void c_rect(lv_layer_t *l, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col, lv_opa_t opa = LV_OPA_COVER,
                   int32_t radius = 0, uint32_t border = 0, int32_t bw = 0) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = C(col);
  d.bg_opa = opa;
  d.radius = radius;
  if (bw > 0) {
    d.border_color = C(border);
    d.border_width = bw;
    d.border_opa = LV_OPA_COVER;
  }
  lv_area_t a = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(l, &d, &a);
}

inline void c_vgrad(lv_layer_t *l, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t top, uint32_t bottom) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_COVER;
  d.bg_grad.dir = LV_GRAD_DIR_VER;
  d.bg_grad.stops_count = 2;
  d.bg_grad.stops[0].color = C(top);
  d.bg_grad.stops[0].opa = LV_OPA_COVER;
  d.bg_grad.stops[0].frac = 0;
  d.bg_grad.stops[1].color = C(bottom);
  d.bg_grad.stops[1].opa = LV_OPA_COVER;
  d.bg_grad.stops[1].frac = 255;
  lv_area_t a = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(l, &d, &a);
}

inline void c_circle(lv_layer_t *l, float cx, float cy, float r, uint32_t col, lv_opa_t opa = LV_OPA_COVER) {
  int32_t R = std::max<int32_t>(1, lroundf(r * S));
  c_rect(l, X(cx) - R, Y(cy) - R, 2 * R, 2 * R, col, opa, LV_RADIUS_CIRCLE);
}

inline void c_line(lv_layer_t *l, float x1, float y1, float x2, float y2, uint32_t col, float w, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.p1.x = X(x1);
  d.p1.y = Y(y1);
  d.p2.x = X(x2);
  d.p2.y = Y(y2);
  d.color = C(col);
  d.width = std::max<int32_t>(1, lroundf(w * S));
  d.opa = opa;
  d.round_start = d.round_end = 1;
  lv_draw_line(l, &d);
}

inline void c_tri(lv_layer_t *l, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col,
                  lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.p[0].x = X(x0);
  d.p[0].y = Y(y0);
  d.p[1].x = X(x1);
  d.p[1].y = Y(y1);
  d.p[2].x = X(x2);
  d.p[2].y = Y(y2);
  d.color = C(col);
  d.opa = opa;
  lv_draw_triangle(l, &d);
}

struct Pt { float x, y; };

// Füllt ein (sternförmiges) Polygon als Fächer um den Schwerpunkt
inline void c_poly(lv_layer_t *l, const std::vector<Pt> &p, uint32_t col, lv_opa_t opa = LV_OPA_COVER) {
  if (p.size() < 3)
    return;
  float cx = 0, cy = 0;
  for (auto &q : p) {
    cx += q.x;
    cy += q.y;
  }
  cx /= p.size();
  cy /= p.size();
  // Zweimal zeichnen, damit zwischen den Dreiecken keine hellen Nähte bleiben
  for (int pass = 0; pass < 2; pass++)
    for (size_t i = 0; i < p.size(); i++) {
      auto &a = p[i], &b = p[(i + 1) % p.size()];
      c_tri(l, cx, cy, a.x, a.y, b.x, b.y, col, opa);
    }
}

// Füllt ein beliebiges Polygon zeilenweise (nahtlos, auch für nicht-konvexe Formen)
inline void c_fill(lv_layer_t *l, const std::vector<Pt> &p, uint32_t col) {
  if (p.size() < 3)
    return;
  std::vector<float> xs, ys;
  for (auto &q : p) {
    xs.push_back((q.x + 24) * S);
    ys.push_back(q.y * S - 1);
  }
  float y0 = *std::min_element(ys.begin(), ys.end()), y1 = *std::max_element(ys.begin(), ys.end());
  std::vector<float> cut;
  for (int y = (int) floorf(y0); y <= (int) ceilf(y1); y++) {
    float yc = y + 0.5f;
    cut.clear();
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
      if ((ys[i] > yc) != (ys[j] > yc))
        cut.push_back(xs[i] + (yc - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]));
    }
    std::sort(cut.begin(), cut.end());
    for (size_t k = 0; k + 1 < cut.size(); k += 2) {
      int32_t a = lroundf(cut[k]), b = lroundf(cut[k + 1]);
      if (b > a)
        c_rect(l, a, y, b - a, 1, col);
    }
  }
}

inline void c_polyline(lv_layer_t *l, const std::vector<Pt> &p, uint32_t col, float w) {
  for (size_t i = 1; i < p.size(); i++)
    c_line(l, p[i - 1].x, p[i - 1].y, p[i].x, p[i].y, col, w);
}

// Mini-Pfadparser (M, L, Q, Z, absolute Koordinaten), Kurven werden geglättet
inline std::vector<Pt> path(const char *s, float ox = 0, float oy = 0) {
  std::vector<Pt> out;
  Pt cur{0, 0};
  char cmd = 'M';
  auto num = [&](const char *&p) {
    while (*p == ' ' || *p == ',')
      p++;
    return (float) strtod(p, (char **) &p);
  };
  const char *p = s;
  while (*p) {
    while (*p == ' ')
      p++;
    if (!*p)
      break;
    if (strchr("MLQZ", *p))
      cmd = *p++;
    if (cmd == 'Z')
      break;
    if (cmd == 'M' || cmd == 'L') {
      cur = {num(p), num(p)};
      out.push_back({cur.x + ox, cur.y + oy});
    } else if (cmd == 'Q') {
      Pt c{num(p), num(p)}, e{num(p), num(p)};
      for (int i = 1; i <= 8; i++) {
        float t = i / 8.0f, u = 1 - t;
        out.push_back({u * u * cur.x + 2 * u * t * c.x + t * t * e.x + ox, u * u * cur.y + 2 * u * t * c.y + t * t * e.y + oy});
      }
      cur = e;
    }
  }
  return out;
}

// Rechteck in einem gedrehten Rahmen (Dachfläche) als Polygon
inline std::vector<Pt> rot_rect(float tx, float ty, float deg, float x, float y, float w, float h) {
  float a = deg * (float) M_PI / 180.0f, c = cosf(a), s = sinf(a);
  auto r = [&](float px, float py) { return Pt{tx + px * c - py * s, ty + px * s + py * c}; };
  return {r(x, y), r(x + w, y), r(x + w, y + h), r(x, y + h)};
}

inline uint32_t tint(float t) {
  if (std::isnan(t))
    t = 20;
  float k = std::max(0.0f, std::min(1.0f, (t - 17) / 7));
  int r = lroundf(30 + (92 - 30) * k), g = lroundf(52 + (52 - 52) * k), b = lroundf(86 + (36 - 86) * k);
  return (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
// Canvas zeichnen
// ---------------------------------------------------------------------------
inline void draw_panels(lv_layer_t *l, float tx, float ty, float deg, int rows, int cols, float x0, int skip) {
  for (int row = 0; row < rows; row++)
    for (int i = 0; i < cols; i++) {
      if (i == skip)
        continue;
      auto q = rot_rect(tx, ty, deg, x0 + i * 21, -rows * 12 + row * 12, 19, 11);
      c_poly(l, q, 0x16233F);
      c_polyline(l, {q[0], q[1], q[2], q[3], q[0]}, 0x3D5A8C, 0.8f);
    }
}

inline void render_canvas() {
  if (ui.canvas == nullptr)
    return;
  lv_layer_t layer;
  lv_canvas_init_layer(ui.canvas, &layer);
  lv_layer_t *l = &layer;

  // Himmel, Sonne/Mond, Sterne, Wolken
  c_vgrad(l, 0, 0, SCENE_W, Y(470), night ? 0x05080F : 0x1D3D66, night ? 0x121B30 : 0x3B6C9A);
  if (night) {
    const float st[][2] = {{120, 60}, {210, 30}, {300, 40}, {450, 25}, {540, 70}, {620, 40}, {600, 150}, {150, 140}, {620, 250}};
    for (auto &s : st)
      c_circle(l, s[0], s[1], 1.4f, 0xFFFFFF, 200);
    c_circle(l, 40, 112, 16, 0xE6ECF7);
    c_circle(l, 48, 106, 14, 0x0B1222);
  } else {
    for (int i = 6; i >= 1; i--)
      c_circle(l, 40, 112, 18 + i * 5, 0xFFE38A, 18);
    c_circle(l, 40, 112, 18, 0xFFE38A);
  }
  const float cl[][3] = {{230, 36, 0.8f}, {560, 66, 1}};
  for (auto &c : cl) {
    const float b[][3] = {{0, 0, 22}, {20, -8, 16}, {-20, -4, 14}, {36, 2, 12}};
    for (auto &q : b)
      c_circle(l, c[0] + q[0] * c[2], c[1] + q[1] * c[2], q[2] * c[2], 0x9FB4CF, night ? 40 : 140);
  }

  // Erde, Wiese, Hof
  c_vgrad(l, 0, Y(470), SCENE_W, SCENE_H - Y(470), 0x231B13, 0x0E0B08);
  c_rect(l, 0, Y(467), SCENE_W, D(4), 0x2E4A2A);
  c_rect(l, 0, Y(471), SCENE_W, D(66), 0x262C38);
  for (float y = 482; y < 537; y += 11)
    c_line(l, -40, y, 680, y, 0x2F3644, 1);
  for (int i = 0; i < 40; i++)
    for (int j = 0; j < 6; j++) {
      float x = -40 + i * 18 + (j % 2) * 9;
      c_line(l, x, 471 + j * 11, x, 482 + j * 11, 0x2F3644, 1);
    }
  c_rect(l, 0, Y(535), SCENE_W, D(3), 0x1B2029);

  // Strommast mit Freileitungen
  c_line(l, 628, 330, 628, 470, 0x4B556B, 4);
  c_line(l, 614, 340, 642, 340, 0x4B556B, 3);
  for (float x : {616.0f, 640.0f}) {
    char d[64];
    snprintf(d, sizeof d, "M%.0f 338 Q%.0f 348 %.0f 342", x, x + 40, x + 90);
    c_polyline(l, path(d), 0x56627C, 1.3f);
    c_circle(l, x, 336, 2.2f, 0x7C879E);
  }

  // Wände und Dächer
  c_rect(l, X(172), Y(175), D(424), D(295), WALL);
  c_rect(l, X(82), Y(185), D(94), D(285), WALL);
  c_rect(l, X(2), Y(275), D(82), D(195), WALL);
  c_tri(l, 154, 177, 384, 68, 614, 177, ROOF);
  c_polyline(l, {{154, 177}, {384, 68}, {614, 177}}, ROOF_LINE, 2);
  c_tri(l, 72, 187, 129, 138, 186, 187, ROOF);
  c_polyline(l, {{72, 187}, {129, 138}, {186, 187}}, ROOF_LINE, 2);
  // Garagendach = Balkon des Anbaus
  c_rect(l, X(0), Y(270), D(86), D(6), ROOF_LINE, LV_OPA_COVER, 2);
  c_polyline(l, {{4, 270}, {4, 256}, {82, 256}, {82, 270}}, 0x56627C, 2);
  for (float x = 14; x < 82; x += 10)
    c_line(l, x, 256, x, 270, 0x56627C, 1);
  // Solarmodule: Deye auf der ganzen Südseite (mit Lücke fürs Dachfenster), Hoymiles auf dem Anbau
  draw_panels(l, 154, 177, -25.4f, 2, 11, 12, 5);
  draw_panels(l, 72, 187, -40.7f, 2, 3, 8, -1);

  // Räume (Farbe nach Temperatur)
  auto &R = rooms_def();
  for (size_t i = 0; i < R.size(); i++) {
    auto &d = R[i];
    uint32_t col = technik(d) ? TECH_ROOM : tint(rooms[i].temp);
    if (d.shape == ROOFROOM)
      c_tri(l, 180, 175, 384, 82, 588, 175, col);
    else
      c_rect(l, X(d.x + 1), Y(d.y + 1), D(d.w - 2), D(d.h - 2), col, LV_OPA_COVER, D(4));
    if (d.shape == GARAGE) {
      c_line(l, d.x, 373, d.x + d.w, 373, ROOF_LINE, 2);
      const float g[][2] = {{d.x + 5, 310}, {d.x + 42, 310}, {d.x + 5, 410}, {d.x + 42, 410}};
      for (auto &q : g) {
        float h = q[1] < 400 ? 60 : 58;
        c_rect(l, X(q[0]), Y(q[1]), D(31), D(h), 0x2A3346, LV_OPA_COVER, D(2), 0x45506A, 1);
        for (int k = 1; k < 6; k++)
          c_line(l, q[0] + 2, q[1] + k * 10, q[0] + 29, q[1] + k * 10, ROOF_LINE, 1);
      }
    }
    if (d.stairs != NONE) {
      std::vector<Pt> st;
      bool rev = d.stairs == REV;
      for (int k = 0; k <= 6; k++) {
        float xa = rev ? d.x + d.w - 4 - k * 9 : d.x + 4 + k * 9, xb = rev ? xa - 9 : xa + 9, y = d.y + d.h - 4 - k * 12;
        st.push_back({xa, y});
        st.push_back({xb, y});
      }
      c_polyline(l, st, 0x56627C, 2);
    }
    if (d.door) {
      float dx = d.x + d.w / 2 - 11;
      c_rect(l, X(dx), Y(d.y + d.h - 60), D(22), D(56), 0x5A3B22, LV_OPA_COVER, D(2), 0x7A5232, 1);
      c_circle(l, dx + 17, d.y + d.h - 30, 1.6f, 0xC9A46A);
    }
  }

  // Technik im Heizungsraum (Kreise; Symbole als Labels darüber)
  const float tech[][3] = {{200, 450, (float) GREEN}, {230, 418, (float) TEXT}, {262, 450, 0xFF7A45}};
  for (auto &t : tech) {
    c_circle(l, t[0], t[1], 13, (uint32_t) t[2]);
    c_circle(l, t[0], t[1], 11.5f, 0x0E1422);
  }

  // Wallbox (go-e) und Ladekabel
  c_rect(l, X(88), Y(432), D(13), D(20), 0xE9EDF3, LV_OPA_COVER, D(3), 0x9AA6BA, 1);
  c_polyline(l, path("M94 452 Q94 466 80 466 L4 466 Q-2 467 -1 480 Q0 492 3 499"), 0x1E2430, 2.4f);

  // Tesla Model Y (Seitenansicht, Front rechts)
  const float ox = -6, oy = 478;
  auto body = path("M6 40 L5 29 Q5 23 10 20 L19 16 Q32 3 58 1 L82 1 Q95 2 106 12 L128 17 Q143 19 147 27 L147 38 Q147 41 143 41 L10 41 Q6 41 6 40 Z", ox, oy);
  c_fill(l, body, 0xF4F6FA);
  body.push_back(body.front());
  c_polyline(l, body, 0xC9D1DE, 0.8f);
  c_fill(l, path("M23 16 Q35 5 58 4.5 L82 4.5 Q93 5.5 101 14 Z", ox, oy), 0x1B2433);
  c_line(l, ox + 70, oy + 4.5f, ox + 69, oy + 14, 0xF4F6FA, 2.2f);
  c_line(l, ox + 48, oy + 26, ox + 100, oy + 26, 0xDDE3EC, 1);
  c_line(l, ox + 5.5f, oy + 25, ox + 12, oy + 25, 0xD9534F, 2.6f);
  c_line(l, ox + 140, oy + 22, ox + 146, oy + 24, 0xBFD8FF, 2.2f);
  for (float wx : {32.0f, 118.0f}) {
    c_circle(l, ox + wx, oy + 42, 14.5f, 0x10141B);
    c_circle(l, ox + wx, oy + 42, 12, 0x14181F);
    c_circle(l, ox + wx, oy + 42, 7, 0x5C6678);
    c_circle(l, ox + wx, oy + 42, 2.2f, 0x2A303B);
  }

  lv_canvas_finish_layer(ui.canvas, &layer);
  canvas_dirty = false;
}

// ---------------------------------------------------------------------------
// Kleine LVGL-Helfer
// ---------------------------------------------------------------------------
inline lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col, lv_opa_t opa = LV_OPA_COVER,
                     int32_t radius = 0) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, C(col), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

inline lv_obj_t *text(lv_obj_t *parent, const char *t, const lv_font_t *f, uint32_t col) {
  lv_obj_t *o = lv_label_create(parent);
  lv_label_set_text(o, t);
  if (f)
    lv_obj_set_style_text_font(o, f, 0);
  lv_obj_set_style_text_color(o, C(col), 0);
  return o;
}

inline void set_hidden(lv_obj_t *o, bool h) {
  if (o == nullptr)
    return;
  if (h != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
    if (h)
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void fmt_temp(char *buf, size_t n, float v) {
  if (std::isnan(v))
    snprintf(buf, n, "--°");
  else
    snprintf(buf, n, "%.1f°", v);
  if (char *d = strchr(buf, '.'))
    *d = ',';
}

inline void fmt_kw(char *buf, size_t n, float w) {
  snprintf(buf, n, "%.1f", std::fabs(w) / 1000.0f);
  if (char *d = strchr(buf, '.'))
    *d = ',';
}

// Animationen
inline void opa_cb(void *v, int32_t x) { lv_obj_set_style_opa((lv_obj_t *) v, x, 0); }
inline void w_cb(void *v, int32_t x) { lv_obj_set_width((lv_obj_t *) v, x); }
inline void h_cb(void *v, int32_t x) { lv_obj_set_height((lv_obj_t *) v, x); }
inline void rot_cb(void *v, int32_t x) { lv_obj_set_style_transform_rotation((lv_obj_t *) v, x, 0); }
inline void ty_cb(void *v, int32_t x) {
  lv_obj_set_style_translate_y((lv_obj_t *) v, -x, 0);
  // Funken: oben verglühen
  int32_t span = (int32_t) (intptr_t) lv_obj_get_user_data((lv_obj_t *) v);
  int32_t o = span > 0 ? (x < span / 6 ? x * 255 * 6 / span : 255 - (x - span / 6) * 255 / std::max<int32_t>(1, span * 5 / 6)) : 0;
  lv_obj_set_style_opa((lv_obj_t *) v, LV_CLAMP(0, o, 230), 0);
}

inline void anim(void *var, lv_anim_exec_xcb_t cb, int32_t a, int32_t b, uint32_t ms, lv_anim_path_cb_t path = lv_anim_path_ease_out,
                 int repeat = 0, uint32_t reverse_ms = 0, uint32_t delay = 0) {
  lv_anim_delete(var, cb);
  lv_anim_t an;
  lv_anim_init(&an);
  lv_anim_set_var(&an, var);
  lv_anim_set_exec_cb(&an, cb);
  lv_anim_set_values(&an, a, b);
  lv_anim_set_duration(&an, ms);
  lv_anim_set_path_cb(&an, path);
  if (repeat)
    lv_anim_set_repeat_count(&an, repeat < 0 ? LV_ANIM_REPEAT_INFINITE : repeat);
  if (reverse_ms)
    lv_anim_set_reverse_duration(&an, reverse_ms);
  if (delay)
    lv_anim_set_delay(&an, delay);
  lv_anim_start(&an);
}

// ---------------------------------------------------------------------------
// Fenster
// ---------------------------------------------------------------------------
struct Pane {
  int room;
  int cover, sensor;
  lv_obj_t *glow, *glass, *shutter, *sash, *dot;
  int32_t w, h;
  bool open = false, first = true;
};
inline std::vector<Pane *> panes;

inline Pane *make_pane(int room, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int cover, int sensor, float rot = 0) {
  auto *p = new Pane{room, cover, sensor};
  p->w = w;
  p->h = h;
  p->glow = box(parent, x + 2, y + 2, std::max<int32_t>(2, w - 4), std::max<int32_t>(2, h - 4), 0xFFC86B, LV_OPA_TRANSP, 4);
  lv_obj_set_style_shadow_color(p->glow, C(0xFFC86B), 0);
  lv_obj_set_style_shadow_width(p->glow, D(30), 0);
  lv_obj_set_style_shadow_spread(p->glow, D(3), 0);
  lv_obj_set_style_shadow_opa(p->glow, LV_OPA_TRANSP, 0);
  p->glass = box(parent, x, y, w, h, 0x6FA8D8, LV_OPA_COVER, 1);
  lv_obj_set_style_border_color(p->glass, C(0x3A465C), 0);
  lv_obj_set_style_border_width(p->glass, 1, 0);
  lv_obj_set_style_clip_corner(p->glass, false, 0);
  p->sash = box(p->glass, 0, h * 12 / 100, w, h * 76 / 100, 0xBFE6FF, 90);
  lv_obj_set_style_border_color(p->sash, C(SKY), 0);
  lv_obj_set_style_border_width(p->sash, 1, 0);
  set_hidden(p->sash, true);
  p->shutter = box(p->glass, 0, 0, w, 0, 0x8F9AAF);
  p->dot = box(parent, x + w - D(3), y - D(3), D(6), D(6), SKY, LV_OPA_COVER, LV_RADIUS_CIRCLE);
  set_hidden(p->dot, true);
  if (rot != 0) {
    for (lv_obj_t *o : {p->glow, p->glass, p->dot}) {
      lv_obj_set_style_transform_pivot_x(o, lv_obj_get_style_width(o, LV_PART_MAIN) / 2, 0);
      lv_obj_set_style_transform_pivot_y(o, lv_obj_get_style_height(o, LV_PART_MAIN) / 2, 0);
      lv_obj_set_style_transform_rotation(o, lroundf(rot * 10), 0);
    }
  }
  panes.push_back(p);
  rooms[room].panes.push_back(p);
  return p;
}

inline void build_windows(int ri) {
  auto &d = rooms_def()[ri];
  const float FLOOR_H = 56, WIN_W = 18, WIN_H = 28;
  for (auto &w : d.wins) {
    if (w.t == ROOF_S || w.t == ROOF_N) {
      bool n = w.t == ROOF_N;
      float tx = n ? 614 : 154, ty = 177, deg = n ? 25.4f : -25.4f;
      float x = n ? -w.d - 22 : w.d, y = -23, ww = n ? 22 : 19, wh = 22;
      auto q = rot_rect(tx, ty, deg, x, y, ww, wh);
      float cx = (q[0].x + q[2].x) / 2, cy = (q[0].y + q[2].y) / 2;
      make_pane(ri, ui.scene, X(cx) - D(ww) / 2, Y(cy) - D(wh) / 2, D(ww), D(wh), w.cover, w.sensor, deg);
      continue;
    }
    if (w.t == TRIPLE) {
      float ph = w.h > 0 ? w.h : WIN_H, pw = w.pw > 0 ? w.pw : WIN_W, total = pw * 3 + 4;
      float x0 = w.ax != AUTO ? w.ax : d.x + d.w * w.fx - total / 2;
      float y0 = w.ay != AUTO ? w.ay : (w.h > 0 ? d.y + d.h - ph - 4 : d.y + d.h - 4 - FLOOR_H);
      int cv[3] = {w.cover, w.cover2, w.cover3};
      for (int i = 0; i < 3; i++)
        make_pane(ri, ui.scene, X(x0 + i * (pw + 2)), Y(y0), D(pw), D(ph), cv[i], w.sensor);
      continue;
    }
    float ww = w.ww > 0 ? w.ww : (w.t == WIN ? WIN_W : w.t == DOOR ? 24 : w.t == SLIDE ? 38 : 15);
    float wh = (w.t == WIN) ? WIN_H : FLOOR_H;
    float x = w.ax != AUTO ? w.ax : d.x + d.w * w.fx - ww / 2;
    float y = w.ay != AUTO ? w.ay : d.y + d.h - 4 - FLOOR_H;
    make_pane(ri, ui.scene, X(x), Y(y), D(ww), D(wh), w.cover, w.sensor);
  }
}

inline void paint_pane(Pane *p) {
  auto &r = rooms[p->room];
  bool on = false;
  for (auto &l : r.lights)
    on |= l.on;
  bool open = p->sensor >= 0 && p->sensor < (int) r.contacts.size() && r.contacts[p->sensor].open;
  lv_obj_set_style_bg_color(p->glass, C(on ? 0xFFC86B : open ? 0x0C1424 : night ? 0x0B1220 : 0x6FA8D8), 0);
  lv_obj_set_style_border_color(p->glass, C(open ? SKY : 0x3A465C), 0);
  lv_obj_set_style_shadow_opa(p->glow, on ? (night ? 170 : 110) : 0, 0);
  float pos = (p->cover >= 0 && p->cover < (int) r.covers.size() && !std::isnan(r.covers[p->cover].pos)) ? r.covers[p->cover].pos : 100;
  int32_t sh = lroundf((p->h - 2) * (100 - pos) / 100.0f);
  int32_t now = lv_obj_get_style_height(p->shutter, LV_PART_MAIN);
  if (sh != now)
    anim(p->shutter, h_cb, now, sh, 900);
  if (open != p->open || p->first) {
    // Flügel schwingt nach innen auf (wird schmaler) bzw. wieder zu
    set_hidden(p->sash, false);
    if (open)
      anim(p->sash, w_cb, p->first ? p->w * 42 / 100 : p->w, p->w * 42 / 100, p->first ? 1 : 700, lv_anim_path_overshoot);
    else
      set_hidden(p->sash, true);
    set_hidden(p->dot, !open);
    if (open)
      anim(p->dot, opa_cb, 255, 90, 800, lv_anim_path_ease_in_out, -1, 800);
    else
      lv_anim_delete(p->dot, opa_cb);
    p->open = open;
    p->first = false;
  }
}

// ---------------------------------------------------------------------------
// Heizung: Glutlinie, Schein, Funken; Heizwert hinter der Temperatur
// ---------------------------------------------------------------------------
inline float avg_open(const RoomState &r) {
  if (r.circuits.empty())
    return 0;
  float s = 0;
  for (auto &c : r.circuits)
    s += c.level;
  return s / r.circuits.size();
}

inline uint32_t heat_color(float v) {
  if (v <= 0)
    return 0x2A3346;
  lv_color_t c = v < 50 ? mixc(0x7A2E12, 0xFF8A3D, v / 50) : mixc(0xFF8A3D, 0xFFD67A, (v - 50) / 50);
  return lv_color_to_u32(c) & 0xFFFFFF;
}

inline void paint_heat(int ri) {
  auto &d = rooms_def()[ri];
  auto &r = rooms[ri];
  if (r.heat_line == nullptr)
    return;
  float k = avg_open(r) / 100.0f;
  bool on = k > 0.005f;
  set_hidden(r.heat_line, !on);
  set_hidden(r.heat_glow, !on);
  if (on) {
    int32_t hh = lroundf(D(d.h) * (0.22f + 0.4f * k));
    int32_t bottom = d.shape == ROOFROOM ? Y(172) : Y(d.y + d.h - 2);
    lv_obj_set_y(r.heat_glow, bottom - hh - r.gy);
    lv_obj_set_height(r.heat_glow, hh);
    lv_obj_set_style_bg_grad_opa(r.heat_glow, lroundf(255 * (0.18f + 0.3f * k)), 0);
    lv_obj_set_style_bg_color(r.heat_line, C(heat_color(45 + 35 * k)), 0);
    lv_obj_set_style_shadow_color(r.heat_line, C(heat_color(40 + 30 * k)), 0);
    uint32_t dur = lroundf(4200 - 2200 * k);
    anim(r.heat_line, opa_cb, 255, lroundf(255 * (0.45f + 0.15f * k)), dur / 2, lv_anim_path_ease_in_out, -1, dur / 2);
  } else {
    lv_anim_delete(r.heat_line, opa_cb);
  }
  // Funken: Anzahl nach Breite und Öffnung
  size_t want = on ? (size_t) std::max(1L, lroundf(D(d.w) / 56.0f * (0.4f + k))) : 0;
  for (size_t i = 0; i < r.sparks.size(); i++) {
    lv_obj_t *s = r.sparks[i];
    bool use = i < want;
    set_hidden(s, !use);
    if (use) {
      int32_t span = lroundf(D(d.h) * (0.3f + 0.35f * k));
      lv_obj_set_user_data(s, (void *) (intptr_t) span);
      anim(s, ty_cb, 0, span, lroundf(5500 - 2000 * k), lv_anim_path_ease_out, -1, 0, (uint32_t) ((i * 1700) % 5000));
    } else {
      lv_anim_delete(s, ty_cb);
    }
  }
  // Heizsymbol + Wert hinter der Temperatur
  int pct = lroundf(avg_open(r));
  bool show = pct > 0;
  set_hidden(r.chip_i, !show);
  bool icon_only = d.w < 66 && d.shape != ROOFROOM;
  set_hidden(r.chip_v, !show || icon_only);
  if (show) {
    char b[12];
    snprintf(b, sizeof b, "%d %%", pct);
    lv_label_set_text(r.chip_v, b);
    lv_obj_update_layout(r.temp_l);
    lv_obj_align_to(r.chip_i, r.temp_l, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_align_to(r.chip_v, r.chip_i, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
  }
}

// Raum-Behälter: Beim Neuzeichnen prüft LVGL zuerst den Behälter und nur bei
// Überschneidung die Objekte darin – statt hunderte Einzelobjekte pro Ausschnitt.
// Verschiebt alle Szene-Objekte ab Index `from` in den Behälter des Raums.
inline void adopt(RoomState &r, uint32_t from) {
  std::vector<lv_obj_t *> objs;
  for (uint32_t k = from; k < lv_obj_get_child_count(ui.scene); k++) {
    lv_obj_t *o = lv_obj_get_child(ui.scene, (int32_t) k);
    if (o != r.layer)
      objs.push_back(o);
  }
  for (lv_obj_t *o : objs) {
    int32_t x = lv_obj_get_style_x(o, LV_PART_MAIN), y = lv_obj_get_style_y(o, LV_PART_MAIN);
    lv_obj_set_parent(o, r.layer);
    lv_obj_set_pos(o, x - r.gx, y - r.gy);
  }
}

// Legt den Behälter um alle Objekte ab Index `from` (inkl. Schatten) und den Raum selbst
inline void make_room_layer(int ri, uint32_t from) {
  auto &d = rooms_def()[ri];
  auto &r = rooms[ri];
  lv_obj_t *sc = ui.scene;
  lv_obj_update_layout(sc);
  lv_area_t sa;
  lv_obj_get_coords(sc, &sa);
  // Raum mit Rand (später angelegte Heizungs-Objekte liegen darin)
  int32_t x1, y1, x2, y2;
  if (d.shape == ROOFROOM) {
    x1 = X(180); y1 = Y(82); x2 = X(588); y2 = Y(175);
  } else {
    x1 = X(d.x); y1 = Y(d.y); x2 = X(d.x + d.w); y2 = Y(d.y + d.h);
  }
  x1 -= 12; y1 -= 12; x2 += 12; y2 += 12;
  for (uint32_t k = from; k < lv_obj_get_child_count(sc); k++) {
    lv_obj_t *o = lv_obj_get_child(sc, (int32_t) k);
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    int32_t e = lv_obj_calculate_ext_draw_size(o, LV_PART_MAIN) + 8;  // + Reserve für gedrehte Dachfenster
    x1 = std::min(x1, c.x1 - sa.x1 - e);
    y1 = std::min(y1, c.y1 - sa.y1 - e);
    x2 = std::max(x2, c.x2 - sa.x1 + e);
    y2 = std::max(y2, c.y2 - sa.y1 + e);
  }
  r.layer = lv_obj_create(sc);
  lv_obj_remove_style_all(r.layer);
  lv_obj_set_pos(r.layer, x1, y1);
  lv_obj_set_size(r.layer, x2 - x1 + 1, y2 - y1 + 1);
  lv_obj_remove_flag(r.layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(r.layer, LV_OBJ_FLAG_SCROLLABLE);
  r.gx = x1;
  r.gy = y1;
  adopt(r, from);
}

inline void build_heat(int ri) {
  auto &d = rooms_def()[ri];
  auto &r = rooms[ri];
  if (r.circuits.empty())
    return;
  bool roofroom = d.shape == ROOFROOM;
  float x0 = roofroom ? 186 : d.x + 4, x1 = roofroom ? 582 : d.x + d.w - 4;
  int32_t fy = roofroom ? Y(172) : Y(d.y + d.h - 4);
  // Schein: senkrechter Verlauf von unten (orange) nach oben (transparent)
  r.heat_glow = box(ui.scene, X(roofroom ? 300 : d.x + 1), fy, roofroom ? D(170) : D(d.w - 2), 10, 0xFF8A3D);
  lv_obj_set_style_bg_grad_color(r.heat_glow, C(0xFF8A3D), 0);
  lv_obj_set_style_bg_grad_dir(r.heat_glow, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_opa(r.heat_glow, LV_OPA_TRANSP, 0);
  // Glutlinie mit Lichthof
  r.heat_line = box(ui.scene, X(x0), fy, X(x1) - X(x0), 2, 0xFF8A3D, LV_OPA_COVER, 1);
  lv_obj_set_style_shadow_width(r.heat_line, D(7), 0);
  lv_obj_set_style_shadow_opa(r.heat_line, 110, 0);
  // Funken (werden je nach Bedarf benutzt)
  int n = std::max(1, (int) lroundf(D(x1 - x0) / 56.0f * 1.4f));
  for (int i = 0; i < n; i++) {
    int32_t sx = X(x0) + (X(x1) - X(x0)) * (2 * i + 1) / (2 * n) + ((i * 37) % 13) - 6;
    lv_obj_t *s = box(ui.scene, sx, fy - 4, 2, 2, 0xFFD9A0, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    lv_obj_set_style_opa(s, 0, 0);
    set_hidden(s, true);
    r.sparks.push_back(s);
  }
}

// ---------------------------------------------------------------------------
// Energiefluss: Punkte laufen entlang von Linien
//
// Alle Punkte zeichnet eine einzige durchsichtige Ebene selbst (kein Objekt je
// Punkt). Ein LVGL-Timer bewegt sie im Bildschirmtakt; neu gezeichnet werden
// nur die kleinen Flächen um die Punkte.
// ---------------------------------------------------------------------------
constexpr int32_t DOT_R = 3;

struct Flow {
  std::vector<lv_point_precise_t> pts;
  lv_obj_t *line = nullptr;
  std::vector<lv_point_t> dots;  // aktuelle Position je Punkt (Szene-Koordinaten)
  float len = 0, pos = 0, speed = 0;
  int dir = 1;
  uint32_t color;
};
inline std::map<std::string, Flow> flows;
inline lv_obj_t *dot_layer = nullptr;

inline void make_flow(const char *key, std::vector<Pt> pts, uint32_t color) {
  Flow &f = flows[key];
  f.color = color;
  for (auto &p : pts)
    f.pts.push_back({X(p.x), Y(p.y)});
  for (size_t i = 1; i < f.pts.size(); i++)
    f.len += hypotf(f.pts[i].x - f.pts[i - 1].x, f.pts[i].y - f.pts[i - 1].y);
  f.line = lv_line_create(ui.scene);
  lv_line_set_points(f.line, f.pts.data(), f.pts.size());
  lv_obj_set_style_line_color(f.line, C(color), 0);
  lv_obj_set_style_line_width(f.line, 2, 0);
  lv_obj_set_style_line_opa(f.line, 50, 0);
  // ein Punkt je ~85 px, kurze Leitungen haben einen
  f.dots.resize(std::max(1, (int) lroundf(f.len / 85)));
}

// Fläche um einen Punkt (Bildschirm-Koordinaten) als ungültig markieren
inline void invalidate_dot(const lv_point_t &p, const lv_point_t &q) {
  lv_area_t a;
  lv_obj_get_coords(dot_layer, &a);
  lv_area_t d = {a.x1 + std::min(p.x, q.x) - DOT_R - 1, a.y1 + std::min(p.y, q.y) - DOT_R - 1,
                 a.x1 + std::max(p.x, q.x) + DOT_R + 1, a.y1 + std::max(p.y, q.y) + DOT_R + 1};
  lv_obj_invalidate_area(dot_layer, &d);
}
inline void invalidate_dot(const lv_point_t &p) { invalidate_dot(p, p); }

inline void flow_set(const char *key, float watts, int dir = 1, uint32_t color = 0) {
  auto it = flows.find(key);
  if (it == flows.end())
    return;
  Flow &f = it->second;
  float speed = std::fabs(watts) < 30 ? 0 : 20 + std::min(std::fabs(watts) / 1000.0f, 6.0f) * 14;
  bool was_on = f.speed > 0, on = speed > 0;
  f.speed = speed;
  f.dir = dir;
  bool recolor = color && color != f.color;
  if (recolor) {
    f.color = color;
    lv_obj_set_style_line_color(f.line, C(color), 0);
  }
  if (was_on != on)
    lv_obj_set_style_line_opa(f.line, on ? 56 : 15, 0);
  // Nur die Flächen der Punkte neu zeichnen, wenn der Fluss an/aus geht oder die Farbe wechselt
  if (dot_layer && (was_on != on || recolor))
    for (auto &p : f.dots)
      invalidate_dot(p);
}

inline void flows_step(float dt) {
  for (auto &kv : flows) {
    Flow &f = kv.second;
    if (f.speed == 0)
      continue;
    // kurze Leitungen nicht durchrasen: ein Durchlauf dauert mindestens 1,5 s
    float v = std::min(f.speed * S, f.len / 1.5f);
    f.pos = fmodf(f.pos + f.dir * v * dt + f.len, f.len);
    for (size_t i = 0; i < f.dots.size(); i++) {
      float d = fmodf(f.pos + i * f.len / f.dots.size(), f.len);
      lv_point_t np = f.dots[i];
      for (size_t k = 1; k < f.pts.size(); k++) {
        float seg = hypotf(f.pts[k].x - f.pts[k - 1].x, f.pts[k].y - f.pts[k - 1].y);
        if (d <= seg || k == f.pts.size() - 1) {
          float t = seg > 0 ? std::min(1.0f, d / seg) : 0;
          np = {(int32_t) lroundf(f.pts[k - 1].x + (f.pts[k].x - f.pts[k - 1].x) * t),
                (int32_t) lroundf(f.pts[k - 1].y + (f.pts[k].y - f.pts[k - 1].y) * t)};
          break;
        }
        d -= seg;
      }
      if (np.x != f.dots[i].x || np.y != f.dots[i].y) {
        // alte und neue Position als eine Fläche: LVGL merkt sich nur 32 Flächen pro Bild,
        // darüber zeichnet es den ganzen Bildschirm neu
        invalidate_dot(f.dots[i], np);
        f.dots[i] = np;
      }
    }
  }
}

inline void dot_draw_cb(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(dot_layer, &a);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_opa = LV_OPA_COVER;
  for (auto &kv : flows) {
    Flow &f = kv.second;
    if (f.speed == 0)
      continue;
    d.bg_color = C(f.color);
    for (auto &p : f.dots) {
      lv_area_t r = {a.x1 + p.x - DOT_R, a.y1 + p.y - DOT_R, a.x1 + p.x + DOT_R - 1, a.y1 + p.y + DOT_R - 1};
      // nur Punkte zeichnen, die im gerade neu gezeichneten Ausschnitt liegen
      const lv_area_t &c = layer->_clip_area;
      if (r.x2 < c.x1 || r.x1 > c.x2 || r.y2 < c.y1 || r.y1 > c.y2)
        continue;
      lv_draw_rect(layer, &d, &r);
    }
  }
}

inline void flow_timer_cb(lv_timer_t *) {
  static uint32_t last = lv_tick_get();
  uint32_t now = lv_tick_get();
  float dt = lv_tick_diff(now, last) / 1000.0f;
  last = now;
  flows_step(std::min(dt, 0.1f));
}

inline void make_dot_layer() {
  dot_layer = lv_obj_create(ui.scene);
  lv_obj_remove_style_all(dot_layer);
  lv_obj_set_pos(dot_layer, 0, 0);
  lv_obj_set_size(dot_layer, SCENE_W, SCENE_H);
  lv_obj_remove_flag(dot_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(dot_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(dot_layer, dot_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
  // ~60 Bilder pro Sekunde, im LVGL-Takt statt im ESPHome-Intervall
  lv_timer_create(flow_timer_cb, 16, nullptr);
}

inline lv_obj_t *wallbox_led = nullptr, *charge_led = nullptr;
inline std::vector<lv_obj_t *> insulator_sparks;

inline void update_flows() {
  auto &e = energy;
  flow_set("pv", e.pv);
  flow_set("hoy", e.hoy);
  flow_set("battery", e.battery, e.battery <= 0 ? 1 : -1);  // Deye: + Entladen, - Laden
  flow_set("heater", e.heater);
  flow_set("grid", e.grid, e.grid >= 0 ? 1 : -1, e.grid >= 0 ? PINK : SKY);
  flow_set("wire1", e.grid, e.grid >= 0 ? -1 : 1, e.grid >= 0 ? PINK : SKY);
  flow_set("wire2", e.grid, e.grid >= 0 ? -1 : 1, e.grid >= 0 ? PINK : SKY);
  flow_set("wallbox", e.car);
  flow_set("car", e.car);
  bool charging = e.car > 30;
  set_hidden(charge_led, !charging);
  if (wallbox_led)
    lv_obj_set_style_bg_color(wallbox_led, C(charging ? GREEN : SKY), 0);
}

// ---------------------------------------------------------------------------
// Rauchalarm
// ---------------------------------------------------------------------------
inline lv_obj_t *banner = nullptr, *banner_text = nullptr;
inline void update_alarm() {
  std::string where;
  for (size_t i = 0; i < rooms.size(); i++) {
    auto &r = rooms[i];
    if (r.alarm == nullptr)
      continue;
    set_hidden(r.alarm, !r.smoke);
    if (r.smoke) {
      anim(r.alarm, opa_cb, 60, 200, 500, lv_anim_path_ease_in_out, -1, 500);
      if (!where.empty())
        where += ", ";
      where += rooms_def()[i].name;
    } else {
      lv_anim_delete(r.alarm, opa_cb);
    }
  }
  if (banner) {
    set_hidden(banner, where.empty());
    if (!where.empty()) {
      std::string t = "Rauchmelder: " + where + "\nRaus über Treppenhaus oder Balkontüren im Keller. Feuerwehr: 112";
      lv_label_set_text(banner_text, t.c_str());
    }
  }
}

// ---------------------------------------------------------------------------
// Seitenleiste
// ---------------------------------------------------------------------------
inline void open_room(int ri);
inline void build_side();

inline lv_obj_t *card(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_width(o, lv_pct(100));
  lv_obj_set_height(o, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(o, C(CARD), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, C(LINE), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_radius(o, 18, 0);
  lv_obj_set_style_pad_all(o, 12, 0);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(o, 4, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

inline lv_obj_t *row(lv_obj_t *parent, const char *icon, uint32_t icol, const char *label, const char *value, uint32_t vcol = MUTE) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, lv_pct(100), 30);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *i = text(o, icon, ui.i20, icol);
  lv_obj_align(i, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *t = text(o, label, ui.f14, TEXT);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 30, 0);
  lv_obj_t *v = text(o, value, ui.f13, vcol);
  lv_obj_set_width(v, 190);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  return o;
}

inline lv_obj_t *caps(lv_obj_t *parent, const char *t) {
  lv_obj_t *o = text(parent, t, ui.f12, MUTE);
  lv_obj_set_style_text_letter_space(o, 1, 0);
  return o;
}

inline lv_obj_t *button(lv_obj_t *parent, const char *icon, const char *label, uint32_t col, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_height(b, 46);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_bg_color(b, C(CARD), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, C(col == GREEN ? 0x2E7A4D : LINE), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 8, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  text(b, icon, ui.i20, col);
  text(b, label, ui.f14, col);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

inline std::string all_light_entities() {
  std::string s;
  for (auto &r : rooms)
    for (auto &l : r.lights)
      if (!l.entity.empty())
        s += (s.empty() ? "" : ",") + l.entity;
  return s;
}

inline std::string lock_entity;

// Übersicht: einmal aufbauen, danach nur Texte und Farben aktualisieren
struct Overview {
  lv_obj_t *kpi[4] = {}, *kv[4] = {}, *ks[4] = {}, *ki[4] = {};
  lv_obj_t *rv[6] = {};
  lv_obj_t *lock_btn = nullptr, *lock_icon = nullptr, *lock_text = nullptr;
};
inline Overview ov;
inline bool ov_dirty = false;

inline void set_text(lv_obj_t *o, const char *t) {
  // nur bei echter Änderung neu zeichnen
  if (o && strcmp(lv_label_get_text(o), t) != 0)
    lv_label_set_text(o, t);
}

inline void update_overview() {
  if (selected >= 0 || ov.kv[0] == nullptr)
    return;
  auto &e = energy;
  char a[48], b[64], p1[12], p2[12];
  fmt_kw(a, sizeof a, e.pv + e.hoy);
  strcat(a, " kW");
  fmt_kw(p1, sizeof p1, e.pv);
  fmt_kw(p2, sizeof p2, e.hoy);
  snprintf(b, sizeof b, "Deye %s · Hoym. %s", p1, p2);
  set_text(ov.kv[0], a);
  set_text(ov.ks[0], b);
  fmt_kw(a, sizeof a, e.house);
  strcat(a, " kW");
  set_text(ov.kv[1], a);
  if (std::isnan(e.soc))
    snprintf(a, sizeof a, "-- %%");
  else
    snprintf(a, sizeof a, "%d %%", (int) lroundf(e.soc));
  fmt_kw(p1, sizeof p1, e.battery);
  snprintf(b, sizeof b, e.battery <= 0 ? "lädt %s kW" : "gibt %s kW", p1);
  set_text(ov.kv[2], a);
  set_text(ov.ks[2], b);
  fmt_kw(a, sizeof a, e.grid);
  strcat(a, " kW");
  set_text(ov.kv[3], a);
  set_text(ov.ks[3], e.grid >= 0 ? "Bezug" : "Einspeisung");
  uint32_t gc = e.grid >= 0 ? PINK : SKY;
  lv_obj_set_style_text_color(ov.ki[3], C(gc), 0);
  lv_obj_set_style_bg_color(lv_obj_get_parent(ov.ki[3]), C(gc), 0);

  int lights_on = 0, circ_open = 0, circ_all = 0, covers_n = 0;
  float covers_sum = 0;
  std::string open_list;
  for (size_t i = 0; i < rooms.size(); i++) {
    auto &r = rooms[i];
    for (auto &l : r.lights)
      lights_on += l.on;
    for (auto &c : r.covers)
      if (!std::isnan(c.pos)) {
        covers_sum += c.pos;
        covers_n++;
      }
    for (auto &c : r.contacts)
      if (c.open) {
        if (!open_list.empty())
          open_list += ", ";
        open_list += rooms_def()[i].name;
      }
    if (!r.shares_circuits)
      for (auto &c : r.circuits) {
        circ_all++;
        circ_open += c.level > 0.5f;
      }
  }
  snprintf(a, sizeof a, "%d", lights_on);
  set_text(ov.rv[0], a);
  set_text(ov.rv[1], open_list.empty() ? "keins" : open_list.c_str());
  if (covers_n)
    snprintf(a, sizeof a, "Ø %d %% offen", (int) lroundf(covers_sum / covers_n));
  else
    snprintf(a, sizeof a, "--");
  set_text(ov.rv[2], a);
  snprintf(a, sizeof a, "%d von %d Kreisen offen", circ_open, circ_all);
  set_text(ov.rv[3], a);
  fmt_kw(p1, sizeof p1, e.car);
  if (std::isnan(e.car_today)) {
    snprintf(b, sizeof b, e.car > 30 ? "lädt %s kW" : "bereit", p1);
  } else {
    char t[12];
    snprintf(t, sizeof t, "%.1f", e.car_today);
    if (char *dd = strchr(t, '.'))
      *dd = ',';
    if (e.car > 30)
      snprintf(b, sizeof b, "lädt %s kW · heute %s kWh", p1, t);
    else
      snprintf(b, sizeof b, "bereit · heute %s kWh", t);
  }
  set_text(ov.rv[4], b);
  fmt_kw(p1, sizeof p1, e.heater);
  snprintf(b, sizeof b, e.heater > 30 ? "%s kW" : "aus", p1);
  set_text(ov.rv[5], b);

  set_text(ov.lock_icon, locked ? "\U000F033E" : "\U000F0FC6");
  set_text(ov.lock_text, locked ? "Abgeschlossen" : "Abschließen");
  uint32_t lc = locked ? GREEN : TEXT;
  lv_obj_set_style_text_color(ov.lock_icon, C(lc), 0);
  lv_obj_set_style_text_color(ov.lock_text, C(lc), 0);
  lv_obj_set_style_border_color(ov.lock_btn, C(locked ? 0x2E7A4D : LINE), 0);
  ov_dirty = false;
}

inline void build_overview() {
  lv_obj_t *side = ui.side;
  caps(side, "ENERGIE JETZT");
  lv_obj_t *grid = lv_obj_create(side);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_column(grid, 8, 0);
  const struct { const char *icon; uint32_t col; const char *sub; } K[4] = {
      {"\U000F0A72", AMBER, ""}, {"\U000F1903", TEXT, "Haus"}, {"\U000F12A3", GREEN, ""}, {"\U000F0D3E", PINK, ""}};
  for (int n = 0; n < 4; n++) {
    lv_obj_t *k = box(grid, 0, 0, 166, 62, CARD, LV_OPA_COVER, 16);
    lv_obj_set_style_border_color(k, C(LINE), 0);
    lv_obj_set_style_border_width(k, 1, 0);
    lv_obj_t *bub = box(k, 10, 12, 36, 36, K[n].col, 40, 12);
    ov.ki[n] = text(bub, K[n].icon, ui.i20, K[n].col);
    lv_obj_center(ov.ki[n]);
    ov.kv[n] = text(k, "--", ui.f20b, TEXT);
    lv_obj_set_pos(ov.kv[n], 56, 9);
    ov.ks[n] = text(k, K[n].sub, ui.f12, MUTE);
    lv_obj_set_pos(ov.ks[n], 56, 36);
    ov.kpi[n] = k;
  }

  lv_obj_t *st = card(side);
  const struct { const char *icon; uint32_t col; const char *label; } R6[6] = {
      {"\U000F0335", AMBER, "Lichter an"},      {"\U000F11DC", SKY, "Fenster offen"}, {"\U000F111C", MUTE, "Rollläden"},
      {"\U000F0438", 0xFF8A3D, "Fußbodenheizung"}, {"\U000F05F1", GREEN, "Wallbox"},  {"\U000F0F92", 0xFF7A45, "Heizstab"}};
  for (int n = 0; n < 6; n++)
    ov.rv[n] = lv_obj_get_child(row(st, R6[n].icon, R6[n].col, R6[n].label, ""), 2);

  lv_obj_t *btns = lv_obj_create(side);
  lv_obj_remove_style_all(btns);
  lv_obj_set_size(btns, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(btns, 8, 0);
  button(btns, "\U000F12CD", "Alles aus", TEXT, [](lv_event_t *) {
    for (auto &r : rooms)
      for (auto &l : r.lights)
        l.on = false;
    for (auto *p : panes)
      paint_pane(p);
    ov_dirty = true;
    if (call_action)
      call_action("homeassistant.turn_off", all_light_entities());
  }, nullptr);
  ov.lock_btn = button(btns, "\U000F0FC6", "Abschließen", TEXT, [](lv_event_t *) {
    if (call_action && !lock_entity.empty())
      call_action(locked ? "lock.unlock" : "lock.lock", lock_entity);
    locked = !locked;
    ov_dirty = true;
  }, nullptr);
  ov.lock_icon = lv_obj_get_child(ov.lock_btn, 0);
  ov.lock_text = lv_obj_get_child(ov.lock_btn, 1);
  if (ui.subtitle)
    lv_label_set_text(ui.subtitle, "Alle Etagen");
  update_overview();
}

// Licht-Kachel in der Raumansicht: Pendeln wie im Room-Panel
inline void swing_cb(void *var, int32_t v) {
  static const int32_t T[] = {0, 198, 462, 726, 924, 1100};
  static const int32_t A[] = {0, 90, -60, 30, -10, 0};
  int i = 0;
  while (i < 4 && v > T[i + 1])
    i++;
  float f = (1.0f - cosf((v - T[i]) / (float) (T[i + 1] - T[i]) * (float) M_PI)) / 2.0f;
  lv_obj_set_style_transform_rotation((lv_obj_t *) var, A[i] + lroundf((A[i + 1] - A[i]) * f), 0);
}

struct Ref { int room, idx; };
inline std::vector<Ref *> refs;  // Speicher für Event-Daten der aktuellen Ansicht
inline Ref *ref(int room, int idx) {
  auto *r = new Ref{room, idx};
  refs.push_back(r);
  return r;
}

inline void build_room(int ri) {
  lv_obj_t *side = ui.side;
  auto &d = rooms_def()[ri];
  auto &r = rooms[ri];
  char a[64], t1[16], t2[16];
  // Kopf
  lv_obj_t *head = card(side);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 10, 0);
  lv_obj_t *back = box(head, 0, 0, 36, 36, CARD2, LV_OPA_COVER, 12);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(text(back, "\U000F004D", ui.i20, TEXT));
  lv_obj_add_event_cb(back, [](lv_event_t *) { open_room(-1); }, LV_EVENT_CLICKED, nullptr);
  text(head, d.name, ui.f20b, TEXT);
  fmt_temp(t1, sizeof t1, r.temp);
  if (std::isnan(r.hum))
    snprintf(a, sizeof a, "%s\n%s", t1, d.floor);
  else
    snprintf(a, sizeof a, "%s · %d %%\n%s", t1, (int) lroundf(r.hum), d.floor);
  lv_obj_t *meta = text(head, a, ui.f13, MUTE);
  lv_obj_set_flex_grow(meta, 1);
  lv_obj_set_style_text_align(meta, LV_TEXT_ALIGN_RIGHT, 0);

  // Licht
  if (!r.lights.empty()) {
    caps(side, "LICHT");
    lv_obj_t *grid = lv_obj_create(side);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    for (size_t i = 0; i < r.lights.size(); i++) {
      auto &l = r.lights[i];
      lv_obj_t *tile = box(grid, 0, 0, 166, 62, l.on ? 0x3A2911 : CARD, LV_OPA_COVER, 16);
      lv_obj_set_style_border_color(tile, C(l.on ? 0x7A5A2A : LINE), 0);
      lv_obj_set_style_border_width(tile, 1, 0);
      lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t *bub = box(tile, 10, 12, 38, 38, l.on ? AMBER : CARD2, LV_OPA_COVER, 13);
      lv_obj_t *ic = text(bub, l.swing ? "\U000F0769" : "\U000F0335", ui.i22, l.on ? 0x2B1B06 : DIM);
      lv_obj_center(ic);
      lv_obj_t *nm = text(tile, l.name.c_str(), ui.f14b, TEXT);
      lv_obj_set_pos(nm, 58, 12);
      lv_obj_t *s = text(tile, l.on ? "An" : "Aus", ui.f12, l.on ? AMBER : MUTE);
      lv_obj_set_pos(s, 58, 33);
      lv_obj_add_event_cb(tile, [](lv_event_t *e) {
        auto *rf = (Ref *) lv_event_get_user_data(e);
        auto &l = rooms[rf->room].lights[rf->idx];
        l.on = !l.on;
        if (call_action && !l.entity.empty())
          call_action("homeassistant.toggle", l.entity);
        for (auto *p : rooms[rf->room].panes)
          paint_pane(p);
        side_dirty = true;
        if (l.swing) {
          lv_obj_t *icon = lv_obj_get_child(lv_obj_get_child((lv_obj_t *) lv_event_get_target(e), 0), 0);
          lv_obj_set_style_transform_pivot_x(icon, lv_obj_get_width(icon) / 2, 0);
          lv_obj_set_style_transform_pivot_y(icon, 0, 0);
          anim(icon, swing_cb, 0, 1100, 1100, lv_anim_path_linear);
        }
      }, LV_EVENT_CLICKED, ref(ri, i));
    }
  }

  // Rollläden
  if (!r.covers.empty()) {
    caps(side, "ROLLLÄDEN");
    lv_obj_t *cc = card(side);
    for (size_t i = 0; i < r.covers.size(); i++) {
      auto &c = r.covers[i];
      lv_obj_t *o = lv_obj_create(cc);
      lv_obj_remove_style_all(o);
      lv_obj_set_size(o, lv_pct(100), 46);
      lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *mini = box(o, 0, 4, 26, 38, 0x78C3FF, LV_OPA_COVER, 5);
      lv_obj_set_style_bg_grad_color(mini, C(0xD9F0FF), 0);
      lv_obj_set_style_bg_grad_dir(mini, LV_GRAD_DIR_VER, 0);
      lv_obj_set_style_border_color(mini, C(0x3A465C), 0);
      lv_obj_set_style_border_width(mini, 2, 0);
      float pos = std::isnan(c.pos) ? 100 : c.pos;
      box(mini, 0, 0, 22, lroundf(34 * (100 - pos) / 100), 0xA7B2C6);
      lv_obj_t *nm = text(o, c.name.c_str(), ui.f14b, TEXT);
      lv_obj_set_pos(nm, 36, 4);
      if (std::isnan(c.pos))
        snprintf(a, sizeof a, "--");
      else if (c.pos >= 99)
        snprintf(a, sizeof a, "Offen");
      else if (c.pos <= 1)
        snprintf(a, sizeof a, "Zu");
      else
        snprintf(a, sizeof a, "%d %%", (int) lroundf(c.pos));
      lv_obj_t *ps = text(o, a, ui.f12, SKY);
      lv_obj_set_pos(ps, 36, 24);
      const char *icons[3] = {"\U000F0143", "\U000F04DB", "\U000F0140"};
      for (int k = 0; k < 3; k++) {
        lv_obj_t *bt = box(o, 190 + k * 44, 7, 40, 32, CARD2, LV_OPA_COVER, 11);
        lv_obj_add_flag(bt, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(text(bt, icons[k], ui.i20, TEXT));
        lv_obj_add_event_cb(bt, [](lv_event_t *e) {
          auto *rf = (Ref *) lv_event_get_user_data(e);
          int k = rf->idx % 10, ci = rf->idx / 10;
          auto &c = rooms[rf->room].covers[ci];
          static const char *const act[3] = {"cover.open_cover", "cover.stop_cover", "cover.close_cover"};
          if (call_action && !c.entity.empty())
            call_action(act[k], c.entity);
        }, LV_EVENT_CLICKED, ref(ri, (int) i * 10 + k));
      }
    }
  }

  // Fensterkontakte
  if (!r.contacts.empty()) {
    lv_obj_t *cc = card(side);
    for (auto &c : r.contacts)
      row(cc, "\U000F11DC", c.open ? SKY : MUTE, c.name.c_str(), c.open ? "offen" : "zu", c.open ? SKY : MUTE);
  }

  // Fußbodenheizung
  if (!r.circuits.empty()) {
    lv_obj_t *cc = card(side);
    caps(cc, "FUSSBODENHEIZUNG");
    for (auto &c : r.circuits) {
      lv_obj_t *o = lv_obj_create(cc);
      lv_obj_remove_style_all(o);
      lv_obj_set_size(o, lv_pct(100), 20);
      lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *nm = text(o, c.name.c_str(), ui.f13, TEXT);
      lv_obj_set_width(nm, 120);
      lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t *bar = box(o, 128, 7, 140, 6, CARD2, LV_OPA_COVER, 3);
      box(bar, 0, 0, lroundf(140 * std::min(100.0f, c.level) / 100), 6, heat_color(c.level), LV_OPA_COVER, 3);
      snprintf(a, sizeof a, "%d %%", (int) lroundf(c.level));
      lv_obj_t *v = text(o, a, ui.f13, MUTE);
      lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    }
  }

  // Thermostat
  if (!std::isnan(r.target)) {
    lv_obj_t *cc = card(side);
    fmt_temp(t2, sizeof t2, r.target);
    snprintf(a, sizeof a, "Ziel %s · %s", t2, r.action == "heating" ? "heizt" : r.action == "cooling" ? "kühlt" : "ruht");
    row(cc, "\U000F0393", HEAT, "Thermostat", a);
  }
  if (ui.subtitle) {
    snprintf(a, sizeof a, "%s · %s", d.floor, d.name);
    lv_label_set_text(ui.subtitle, a);
  }
}

inline void build_side() {
  if (ui.side == nullptr)
    return;
  int32_t scroll = lv_obj_get_scroll_y(ui.side);
  lv_obj_clean(ui.side);
  ov = Overview{};
  for (auto *r : refs)
    delete r;
  refs.clear();
  if (selected >= 0)
    build_room(selected);
  else
    build_overview();
  lv_obj_update_layout(ui.side);
  lv_obj_scroll_to_y(ui.side, scroll, LV_ANIM_OFF);
  side_dirty = false;
}

inline void open_room(int ri) {
  if (selected >= 0 && rooms[selected].hit)
    lv_obj_set_style_border_opa(rooms[selected].hit, LV_OPA_TRANSP, 0);
  selected = ri;
  if (ri >= 0 && rooms[ri].hit)
    lv_obj_set_style_border_opa(rooms[ri].hit, LV_OPA_COVER, 0);
  lv_obj_scroll_to_y(ui.side, 0, LV_ANIM_OFF);
  build_side();
}

// ---------------------------------------------------------------------------
// Aufbau
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// FPS-Anzeige: fertige Bilder pro Sekunde und Zeit pro Bild (Zeichnen + Kopieren)
// ---------------------------------------------------------------------------
inline bool show_fps = true;
inline lv_obj_t *fps_label = nullptr;
inline uint32_t fps_frames = 0, fps_busy = 0, fps_t0 = 0, fps_window = 0;
inline uint64_t fps_px = 0;  // neu gezeichnete Pixel
inline uint32_t fps_flush = 0, fps_f0 = 0;  // Zeit fürs Kopieren in die Anzeige

inline void fps_init() {
  lv_display_t *disp = lv_display_get_default();
  lv_display_add_event_cb(disp, [](lv_event_t *) { fps_t0 = lv_tick_get(); }, LV_EVENT_RENDER_START, nullptr);
  lv_display_add_event_cb(disp, [](lv_event_t *) {
    fps_frames++;
    fps_busy += lv_tick_elaps(fps_t0);
  }, LV_EVENT_RENDER_READY, nullptr);
  lv_display_add_event_cb(disp, [](lv_event_t *) { fps_f0 = lv_tick_get(); }, LV_EVENT_FLUSH_START, nullptr);
  lv_display_add_event_cb(disp, [](lv_event_t *) { fps_flush += lv_tick_elaps(fps_f0); }, LV_EVENT_FLUSH_FINISH, nullptr);
  lv_display_add_event_cb(disp, [](lv_event_t *e) {
    auto *a = (const lv_area_t *) lv_event_get_param(e);
    if (a)
      fps_px += (uint64_t) lv_area_get_width(a) * lv_area_get_height(a);
  }, LV_EVENT_INVALIDATE_AREA, nullptr);
  fps_window = lv_tick_get();
  if (show_fps) {
    fps_label = text(lv_layer_top(), "-- fps", ui.f11, MUTE);
    lv_obj_set_pos(fps_label, 10, 582);
  }
}

inline void fps_update() {
  uint32_t ms = lv_tick_elaps(fps_window);
  if (ms < 900)
    return;
  float fps = fps_frames * 1000.0f / ms;
  float per = fps_frames ? (float) fps_busy / fps_frames : 0;
  float copy = fps_frames ? (float) fps_flush / fps_frames : 0;
  // Anteil des Bildschirms, der pro Bild neu gezeichnet wird
  float share = fps_frames ? 100.0f * fps_px / fps_frames / (1024.0f * 600.0f) : 0;
  // fps · Zeit pro Bild (davon Kopieren) · Anteil des Bildschirms
  char buf[64];
  snprintf(buf, sizeof buf, "%d fps · %.1f ms (Kopie %.1f) · %d %%", (int) lroundf(fps), per, copy, (int) lroundf(share));
  if (fps_label)
    lv_label_set_text(fps_label, buf);
  if (show_fps)
    ESP_LOGI("house", "%s", buf);
  fps_frames = fps_busy = 0;
  fps_px = 0;
  fps_flush = 0;
  fps_window = lv_tick_get();
}

inline void build() {
  auto &R = rooms_def();
  rooms.resize(R.size());
  for (size_t i = 0; i < R.size(); i++)
    room_index[R[i].id] = i;
  rooms[room_index["kueche"]].shares_circuits = true;
  rooms[room_index["eingang"]].shares_circuits = true;

  lv_obj_t *sc = ui.scene;
  lv_obj_set_flex_flow(ui.side, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ui.side, 10, 0);
  lv_obj_set_scroll_dir(ui.side, LV_DIR_VER);
  // Technik-Symbole im Heizungsraum
  const struct { const char *icon; float x, y; uint32_t col; } tech[] = {
      {"\U000F12A3", 200, 450, GREEN}, {"\U000F1903", 230, 418, TEXT}, {"\U000F0F92", 262, 450, 0xFF7A45}};
  for (auto &t : tech) {
    lv_obj_t *l = text(sc, t.icon, ui.i16, t.col);
    lv_obj_update_layout(l);
    lv_obj_set_pos(l, X(t.x) - lv_obj_get_width(l) / 2, Y(t.y) - lv_obj_get_height(l) / 2);
  }
  // LEDs an Wallbox und Ladebuchse
  wallbox_led = box(sc, X(91), Y(436), D(7), 3, GREEN, LV_OPA_COVER, 2);
  anim(wallbox_led, opa_cb, 255, 80, 1200, lv_anim_path_ease_in_out, -1, 1200);
  charge_led = box(sc, X(-6 + 9) - 3, Y(478 + 21) - 3, 6, 6, GREEN, LV_OPA_COVER, LV_RADIUS_CIRCLE);
  anim(charge_led, opa_cb, 255, 60, 800, lv_anim_path_ease_in_out, -1, 800);

  // Räume: Fenster, Heizung, Beschriftung, Tippfläche
  for (size_t i = 0; i < R.size(); i++) {
    auto &d = R[i];
    auto &r = rooms[i];
    uint32_t from = lv_obj_get_child_count(sc);
    build_windows(i);
    bool roofroom = d.shape == ROOFROOM;
    int32_t hx = roofroom ? X(300) : X(d.x + 1), hy = roofroom ? Y(128) : Y(d.y + 1);
    int32_t hw = roofroom ? X(560) - X(300) : D(d.w - 2), hh = roofroom ? Y(175) - Y(128) : D(d.h - 2);
    r.alarm = box(sc, hx, hy, hw, hh, RED, 0, D(4));
    set_hidden(r.alarm, true);
    r.name = text(sc, d.name, ui.f11b, 0xE3E8F2);
    r.temp_l = text(sc, "--°", ui.f11, 0xB7C0D2);
    if (roofroom) {
      lv_obj_set_pos(r.name, X(470), Y(148));
      lv_obj_set_pos(r.temp_l, X(462), Y(161));
    } else if (d.shape == GARAGE) {
      lv_obj_set_pos(r.name, X(d.x + 6), Y(282));
      lv_obj_set_pos(r.temp_l, X(d.x + 6), Y(295));
    } else {
      lv_obj_set_pos(r.name, X(d.x + 6), Y(d.y + 5));
      lv_obj_set_pos(r.temp_l, X(d.x + 6), Y(d.y + 18));
    }
    set_hidden(r.temp_l, technik(d));
    r.chip_i = text(sc, "\U000F0438", ui.i11, HEAT);
    r.chip_v = text(sc, "", ui.f11b, HEAT);
    set_hidden(r.chip_i, true);
    set_hidden(r.chip_v, true);
    // Tippfläche mit Rahmen bei Auswahl
    r.hit = box(sc, hx, hy, hw, hh, 0, LV_OPA_TRANSP, D(4));
    lv_obj_set_style_border_color(r.hit, C(TEXT), 0);
    lv_obj_set_style_border_width(r.hit, 2, 0);
    lv_obj_set_style_border_opa(r.hit, LV_OPA_TRANSP, 0);
    make_room_layer(i, from);
    if (technik(d))
      continue;
    lv_obj_add_flag(r.hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r.hit, [](lv_event_t *e) { open_room((int) (intptr_t) lv_event_get_user_data(e)); }, LV_EVENT_CLICKED,
                        (void *) (intptr_t) i);
  }

  // Energiefluss (Mockup-Koordinaten)
  make_flow("pv", {{232, 132}, {232, 176}, {314, 176}, {314, 398}, {230, 398}, {230, 405}}, AMBER);
  make_flow("hoy", {{112, 162}, {150, 193}, {150, 418}, {217, 418}}, 0xFFD27A);
  make_flow("battery", {{220, 428}, {200, 428}, {200, 437}}, GREEN);
  make_flow("heater", {{240, 428}, {262, 428}, {262, 437}}, 0xFF7A45);
  make_flow("grid", {{628, 340}, {628, 556}, {231, 556}, {231, 431}}, PINK);
  make_flow("wallbox", {{226, 431}, {226, 464}, {94, 464}, {94, 452}}, GREEN);
  make_flow("car", {{94, 452}, {94, 462}, {86, 466}, {4, 466}, {-1, 474}, {0, 490}, {3, 499}}, GREEN);
  make_flow("wire1", path("M616 338 Q656 348 706 342"), PINK);
  make_flow("wire2", path("M640 338 Q680 348 730 342"), PINK);
  make_dot_layer();
  // Funken an den Isolatoren
  for (float x : {616.0f, 640.0f}) {
    lv_obj_t *s = box(sc, X(x) - 4, Y(334) - 4, 8, 8, 0xFFF1A8, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    lv_obj_set_style_shadow_color(s, C(0xFFF1A8), 0);
    lv_obj_set_style_shadow_width(s, 8, 0);
    lv_obj_set_style_opa(s, 0, 0);
    insulator_sparks.push_back(s);
  }
  // Heizung zuletzt, damit Glut und Funken über den Räumen liegen (Kreise kommen aus HA)
  for (size_t i = 0; i < R.size(); i++)
    if (R[i].shape == RECT || R[i].shape == ROOFROOM)
      rooms[i].circuits.reserve(6);

  // Warnbanner (oben über allem)
  banner = box(lv_layer_top(), 16, 64, 620, 64, 0x3A0D0D, LV_OPA_COVER, 16);
  lv_obj_set_style_border_color(banner, C(RED), 0);
  lv_obj_set_style_border_width(banner, 1, 0);
  lv_obj_t *bi = text(banner, "\U000F0238", ui.i22, RED);
  lv_obj_align(bi, LV_ALIGN_LEFT_MID, 14, 0);
  banner_text = text(banner, "", ui.f13, 0xFFE1E1);
  lv_obj_align(banner_text, LV_ALIGN_LEFT_MID, 48, 0);
  set_hidden(banner, true);

  for (auto *p : panes)
    paint_pane(p);
  render_canvas();
  update_flows();
  build_side();
  fps_init();
}

// Heizkreise legen ihre Objekte beim ersten Wert an
inline void ensure_heat(int ri) {
  if (rooms[ri].heat_line == nullptr && !rooms[ri].circuits.empty()) {
    uint32_t from = lv_obj_get_child_count(ui.scene);
    build_heat(ri);
    adopt(rooms[ri], from);
    // Beschriftung über die Glut legen
    lv_obj_move_foreground(rooms[ri].name);
    lv_obj_move_foreground(rooms[ri].temp_l);
    lv_obj_move_foreground(rooms[ri].chip_i);
    lv_obj_move_foreground(rooms[ri].chip_v);
    lv_obj_move_foreground(rooms[ri].hit);
  }
}

// Takt (40 ms): Funken an den Isolatoren (der Fluss hat einen eigenen LVGL-Timer)
inline void tick() {
  static uint32_t last = lv_tick_get(), spark_t = 0;
  uint32_t now = lv_tick_get();
  float dt = lv_tick_diff(now, last) / 1000.0f;
  last = now;
  // Isolatoren funken ab und zu kurz (nur wenn Strom fließt)
  spark_t += (uint32_t) (dt * 1000);
  if (std::fabs(energy.grid) > 30 && spark_t > 3400) {
    spark_t = 0;
    for (size_t i = 0; i < insulator_sparks.size(); i++)
      anim(insulator_sparks[i], opa_cb, 255, 0, 220, lv_anim_path_ease_out, 0, 0, i * 900);
  }
}

inline void slow_tick() {
  fps_update();
  if (canvas_dirty)
    render_canvas();
  if (side_dirty)
    build_side();
  else if (ov_dirty)
    update_overview();
}

// ---------------------------------------------------------------------------
// Werte aus Home Assistant (aus bindings/*.yml)
// ---------------------------------------------------------------------------
template<typename T> inline T &at(std::vector<T> &v, int idx) {
  if ((int) v.size() <= idx)
    v.resize(idx + 1);
  return v[idx];
}

// Geänderter Raum: Raumansicht neu aufbauen, falls er offen ist, sonst nur die Übersicht auffrischen
inline void mark_changed(int ri) {
  if (selected == ri)
    side_dirty = true;
  else if (selected < 0)
    ov_dirty = true;
}

inline void room_temp(const std::string &id, float t) {
  auto *r = room(id);
  if (!r || std::isnan(t))
    return;
  if (std::isnan(r->temp) || lroundf(r->temp * 2) != lroundf(t * 2))
    canvas_dirty = true;  // Farbe in 0,5-°C-Schritten neu
  r->temp = t;
  char b[16];
  fmt_temp(b, sizeof b, t);
  lv_label_set_text(r->temp_l, b);
  paint_heat(room_index[id]);
  if (selected == room_index[id])
    side_dirty = true;
}

inline void room_humidity(const std::string &id, float h) {
  if (auto *r = room(id)) {
    r->hum = h;
    if (selected == room_index[id])
      side_dirty = true;
  }
}

inline void room_target(const std::string &id, float t, const std::string &action) {
  if (auto *r = room(id)) {
    if (!std::isnan(t))
      r->target = t;
    if (!action.empty())
      r->action = action;
    if (selected == room_index[id])
      side_dirty = true;
  }
}

inline void light(const std::string &id, int idx, const std::string &entity, const std::string &name, bool on, bool swing) {
  auto *r = room(id);
  if (!r)
    return;
  auto &l = at(r->lights, idx);
  l.entity = entity;
  l.name = name;
  l.swing = swing;
  l.on = on;
  l.known = true;
  for (auto *p : r->panes)
    paint_pane(p);
  mark_changed(room_index[id]);
}

inline void cover(const std::string &id, int idx, const std::string &entity, const std::string &name, float pos) {
  auto *r = room(id);
  if (!r || std::isnan(pos))
    return;
  auto &c = at(r->covers, idx);
  c.entity = entity;
  c.name = name;
  c.pos = pos;
  for (auto *p : r->panes)
    if (p->cover == idx)
      paint_pane(p);
  mark_changed(room_index[id]);
}

inline void contact(const std::string &id, int idx, const std::string &entity, const std::string &name, bool open) {
  auto *r = room(id);
  if (!r)
    return;
  auto &c = at(r->contacts, idx);
  c.entity = entity;
  c.name = name;
  c.open = open;
  for (auto *p : r->panes)
    if (p->sensor == idx)
      paint_pane(p);
  mark_changed(room_index[id]);
}

// Ventilöffnung: Homematic liefert 0..1, andere 0..100
inline void circuit(const std::string &id, int idx, const std::string &name, float level) {
  auto *r = room(id);
  if (!r || std::isnan(level))
    return;
  auto &c = at(r->circuits, idx);
  c.name = name;
  c.level = level <= 1.0f ? level * 100 : level;
  int ri = room_index[id];
  ensure_heat(ri);
  paint_heat(ri);
  mark_changed(room_index[id]);
}

inline void smoke(const std::string &id, bool on) {
  if (auto *r = room(id)) {
    r->smoke = on;
    update_alarm();
  }
}

inline void set_energy(const std::string &key, float v) {
  if (std::isnan(v))
    v = 0;
  auto &e = energy;
  if (key == "pv")
    e.pv = v;
  else if (key == "hoy")
    e.hoy = v;
  else if (key == "house")
    e.house = v;
  else if (key == "battery")
    e.battery = v;
  else if (key == "grid")
    e.grid = v;
  else if (key == "heater")
    e.heater = v;
  else if (key == "soc")
    e.soc = v;
  else if (key == "car")
    e.car = v;
  else if (key == "car_today")
    e.car_today = v;
  update_flows();
  if (selected < 0)
    ov_dirty = true;
}

inline void set_lock(const std::string &entity, bool is_locked) {
  lock_entity = entity;
  locked = is_locked;
  if (selected < 0)
    ov_dirty = true;
}

inline void set_night(bool n) {
  if (n == night)
    return;
  night = n;
  canvas_dirty = true;
  for (auto *p : panes)
    paint_pane(p);
}

}  // namespace house
