/**
 * @file main.cpp
 * @brief ADS-B Radar Display — Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 *
 * Key design choices
 * ──────────────────
 * • HTTP fetch runs on Core 0 (FreeRTOS task) — the sweep animation on Core 1
 *   never blocks, so the arm moves smoothly even during network calls.
 * • A mutex protects the shared aircraft array.
 * • PPI-style painting: blips appear only when the sweep arm crosses their
 *   bearing; on the next pass the arm erases and repaints with fresh data.
 * • Labels are erased by redrawing the exact text in black, not a dumb
 *   fillRect, so nearby labels are not clobbered.
 *
 * Controls:
 *   Encoder CW/CCW  — zoom in / zoom out
 *   Touch blip      — show aircraft detail
 *   Touch elsewhere — dismiss detail
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

#include "pins.h"
#include "config.h"
#include "waveshare_init.h"

// ─── Colours (RGB565: RRRRR GGGGGG BBBBB) ────────────────────────────────────
static const uint16_t C_BG       = 0x0000;  // black
static const uint16_t C_RING     = 0x0120;  // dark green ring
static const uint16_t C_GRID     = 0x00A0;  // dim green crosshair
static const uint16_t C_SWEEP    = 0x07E0;  // bright green sweep arm
static const uint16_t C_BLIP     = 0x07E0;  // blip colour
static const uint16_t C_SEL      = 0xFFFF;  // selected aircraft (white)
static const uint16_t C_LBL      = 0x03E0;  // callsign label
static const uint16_t C_BOX_BG   = 0x0020;  // detail box background
static const uint16_t C_BOX_BORD = 0x03E0;  // detail box border

// ─── Display ─────────────────────────────────────────────────────────────────

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO, LCD_D2, LCD_D3);
Arduino_GFX *gfx = new Arduino_SH8601(bus, LCD_RST, 0, 360, 360);

#define SCREEN_WIDTH  360
#define SCREEN_HEIGHT 360
#define CX            180
#define CY            180
#define SCREEN_RADIUS 172

static const float DEG2RAD    = M_PI / 180.0f;
static const float NM_PER_DEG = 60.0f;

// ─── Touch ───────────────────────────────────────────────────────────────────

#define CST816_ADDR 0x15

void touch_init() {
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);  delay(20);
    digitalWrite(TOUCH_RST, HIGH); delay(50);
    pinMode(TOUCH_INT, INPUT_PULLUP);
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(100000);
}

static int  touch_x = -1, touch_y = -1;

bool read_touch() {
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) { Wire.endTransmission(true); return false; }
    Wire.requestFrom(CST816_ADDR, 7);
    if (Wire.available() < 7) return false;
    uint8_t p[7]; for (int i = 0; i < 7; i++) p[i] = Wire.read();
    if (p[2] == 0) return false;
    int x = ((p[3] & 0x0F) << 8) | p[4];
    int y = ((p[5] & 0x0F) << 8) | p[6];
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return false;
    touch_x = x; touch_y = y;
    return true;
}

// ─── Encoder ─────────────────────────────────────────────────────────────────

volatile int encoder_steps = 0;
static uint8_t enc_prev_a = HIGH, enc_prev_b = HIGH;
static uint8_t enc_deb_a = 0, enc_deb_b = 0;

void poll_encoder() {
    uint8_t a = digitalRead(ENCODER_A), b = digitalRead(ENCODER_B);
    if (a==LOW){enc_deb_a=(a!=enc_prev_a)?0:enc_deb_a+1;}
    else{if(a!=enc_prev_a&&++enc_deb_a>=2){enc_deb_a=0;encoder_steps++;}else enc_deb_a=0;}
    enc_prev_a=a;
    if (b==LOW){enc_deb_b=(b!=enc_prev_b)?0:enc_deb_b+1;}
    else{if(b!=enc_prev_b&&++enc_deb_b>=2){enc_deb_b=0;encoder_steps--;}else enc_deb_b=0;}
    enc_prev_b=b;
}

// ─── Aircraft (shared between Core 0 fetch and Core 1 render) ────────────────

#define MAX_AIRCRAFT 64

struct Aircraft {
    // Data from ADS-B
    char    hex[8], callsign[10];
    float   lat, lon;
    int     altitude, speed, heading;
    bool    has_pos;
    uint32_t seen_ms;
    // Derived
    float   bearing;      // 0=N clockwise
    // Paint state (Core 1 only)
    int     paint_x, paint_y, paint_hdg;
    char    paint_cs[10];
    bool    paint_valid;
};

static Aircraft aircraft[MAX_AIRCRAFT];
static int      aircraft_count = 0;
static float    range_nm       = DEFAULT_RANGE_NM;
static int      selected_idx   = -1;

// Mutex protecting aircraft[] and aircraft_count
static SemaphoreHandle_t ac_mutex;

float bearing_to(float lat, float lon) {
    float dlat = lat - HOME_LAT;
    float dlon = (lon - HOME_LON) * cosf(HOME_LAT * DEG2RAD);
    float b = atan2f(dlon, dlat) * (180.0f / M_PI);
    return b < 0 ? b + 360.0f : b;
}

bool latlon_to_screen(float lat, float lon, int *sx, int *sy) {
    float dlat = lat - HOME_LAT;
    float dlon = (lon - HOME_LON) * cosf(HOME_LAT * DEG2RAD);
    float dist = sqrtf(dlat*dlat + dlon*dlon) * NM_PER_DEG;
    if (dist > range_nm) return false;
    float scale = (float)SCREEN_RADIUS / range_nm;
    *sx = (int)(CX + dlon * NM_PER_DEG * scale);
    *sy = (int)(CY - dlat * NM_PER_DEG * scale);
    return true;
}

// ─── Core 0 Fetch Task ───────────────────────────────────────────────────────

static volatile bool fetch_requested = false;
static volatile bool fetch_busy      = false;

void fetch_task(void *pv) {
    for (;;) {
        if (fetch_requested && !fetch_busy) {
            fetch_busy      = true;
            fetch_requested = false;

            if (WiFi.status() == WL_CONNECTED) {
                HTTPClient http;
                http.begin(String("http://") + ADSB_HOST + ":" + ADSB_PORT + ADSB_PATH);
                http.setTimeout(2500);
                int code = http.GET();
                if (code == 200) {
                    // Parse into a local temporary buffer
                    JsonDocument doc;
                    deserializeJson(doc, *http.getStreamPtr());
                    http.end();

                    // Build new list
                    Aircraft tmp[MAX_AIRCRAFT];
                    int tmp_count = 0;
                    for (JsonObject ac : doc["aircraft"].as<JsonArray>()) {
                        if (tmp_count >= MAX_AIRCRAFT) break;
                        Aircraft &a = tmp[tmp_count];
                        strlcpy(a.hex, ac["hex"] | "------", sizeof(a.hex));
                        const char *fl = ac["flight"];
                        if (fl) strlcpy(a.callsign, fl, sizeof(a.callsign));
                        else    strlcpy(a.callsign, a.hex, sizeof(a.callsign));
                        for (int i=strlen(a.callsign)-1;i>=0&&a.callsign[i]==' ';i--) a.callsign[i]='\0';
                        a.has_pos  = ac["lat"].is<float>() && ac["lon"].is<float>();
                        a.lat      = ac["lat"] | 0.0f;
                        a.lon      = ac["lon"] | 0.0f;
                        a.altitude = ac["alt_baro"] | -1;
                        a.speed    = (int)(ac["gs"].as<float>());
                        a.heading  = (int)(ac["track"].as<float>());
                        a.seen_ms  = millis();
                        a.bearing  = a.has_pos ? bearing_to(a.lat, a.lon) : -1.0f;
                        // Preserve paint state from previous matching entry
                        a.paint_valid = false;
                        a.paint_x = a.paint_y = 0;
                        a.paint_cs[0] = '\0';
                        xSemaphoreTake(ac_mutex, portMAX_DELAY);
                        for (int j = 0; j < aircraft_count; j++) {
                            if (strcmp(aircraft[j].hex, a.hex) == 0) {
                        a.paint_valid = aircraft[j].paint_valid;
                                a.paint_x     = aircraft[j].paint_x;
                                a.paint_y     = aircraft[j].paint_y;
                                a.paint_hdg   = aircraft[j].paint_hdg;
                                strlcpy(a.paint_cs, aircraft[j].paint_cs, sizeof(a.paint_cs));
                                break;
                            }
                        }
                        xSemaphoreGive(ac_mutex);
                        tmp_count++;
                    }

                    // Atomically replace aircraft list
                    xSemaphoreTake(ac_mutex, portMAX_DELAY);
                    memcpy(aircraft, tmp, tmp_count * sizeof(Aircraft));
                    aircraft_count = tmp_count;
                    xSemaphoreGive(ac_mutex);
                } else {
                    http.end();
                }
            }
            fetch_busy = false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ─── Rendering (Core 1) ───────────────────────────────────────────────────────

static float sweep_angle = 0.0f;
static float prev_sweep  = -1.0f;

void draw_blip_shape(int cx, int cy, int hdg, uint16_t col) {
    float a = hdg * DEG2RAD, sz = 6;
    int x0=cx+(int)(sinf(a)*sz),            y0=cy-(int)(cosf(a)*sz);
    int x1=cx+(int)(sinf(a+2.4f)*sz*.7f),   y1=cy-(int)(cosf(a+2.4f)*sz*.7f);
    int x2=cx+(int)(sinf(a-2.4f)*sz*.7f),   y2=cy-(int)(cosf(a-2.4f)*sz*.7f);
    gfx->drawTriangle(x0,y0,x1,y1,x2,y2,col);
}

void restore_rings_and_cross() {
    for (int r = 1; r <= 3; r++)
        gfx->drawCircle(CX, CY, SCREEN_RADIUS*r/3, C_RING);
    gfx->drawFastHLine(CX-SCREEN_RADIUS, CY, SCREEN_RADIUS*2, C_GRID);
    gfx->drawFastVLine(CX, CY-SCREEN_RADIUS, SCREEN_RADIUS*2, C_GRID);
    gfx->fillCircle(CX, CY, 3, C_BLIP);
    // Restore range labels at 12-o'clock on each ring
    gfx->setTextColor(C_LBL, C_BG);
    gfx->setTextSize(1);
    for (int r = 1; r <= 3; r++) {
        gfx->setCursor(CX+4, CY - SCREEN_RADIUS*r/3 + 1);
        gfx->printf("%.0f", range_nm*r/3.0f);
    }
}

void draw_static_bg() {
    gfx->fillScreen(C_BG);
    restore_rings_and_cross();
    gfx->setTextColor(C_LBL, C_BG);
    gfx->setTextSize(1);
    for (int r = 1; r <= 3; r++) {
        gfx->setCursor(CX+4, CY - SCREEN_RADIUS*r/3 + 1);
        gfx->printf("%.0f", range_nm*r/3.0f);
    }
}

/** True if segment (x1,y1)→(x2,y2) passes within `thresh` pixels of point (px,py) */
static bool line_near(int x1, int y1, int x2, int y2, int px, int py, float thresh) {
    float dx = x2-x1, dy = y2-y1;
    float len2 = dx*dx + dy*dy;
    if (len2 < 1.0f) return false;
    // Project point onto segment, clamp to [0,1]
    float t = ((px-x1)*dx + (py-y1)*dy) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = x1 + t*dx - px;
    float cy = y1 + t*dy - py;
    return (cx*cx + cy*cy) < thresh*thresh;
}

void erase_sweep(float a_deg) {
    float a  = a_deg * DEG2RAD;
    int   ex = CX + (int)(sinf(a) * SCREEN_RADIUS);
    int   ey = CY - (int)(cosf(a) * SCREEN_RADIUS);
    gfx->drawLine(CX, CY, ex, ey, C_BG);
    restore_rings_and_cross();

    // Restore any painted blips the sweep line crossed through
    xSemaphoreTake(ac_mutex, portMAX_DELAY);
    for (int i = 0; i < aircraft_count; i++) {
        Aircraft &ac = aircraft[i];
        if (!ac.paint_valid) continue;

        // Check triangle centre and several points along the callsign label
        int  lx = ac.paint_x, ly = ac.paint_y;
        int  label_len = strlen(ac.paint_cs);
        bool hit = line_near(CX, CY, ex, ey, lx, ly, 10.0f);   // blip triangle
        for (int c = 0; c <= label_len && !hit; c++) {           // scan label chars
            hit = line_near(CX, CY, ex, ey, lx+8+c*6, ly, 7.0f);
        }
        if (!hit) continue;

        // Redraw this blip
        bool sel = (i == selected_idx);
        draw_blip_shape(lx, ly, ac.paint_hdg, sel ? C_SEL : C_BLIP);
        gfx->setTextColor(sel ? C_SEL : C_LBL, C_BG);
        gfx->setTextSize(1);
        gfx->setCursor(lx+8, ly-4);
        gfx->print(ac.paint_cs);
    }
    xSemaphoreGive(ac_mutex);
}

void draw_sweep(float a_deg) {
    float a = a_deg * DEG2RAD;
    gfx->drawLine(CX, CY, CX+(int)(sinf(a)*SCREEN_RADIUS), CY-(int)(cosf(a)*SCREEN_RADIUS), C_SWEEP);
}


/** Erase a blip — fillRect over icon area (robust regardless of heading), then exact text overdraw */
void erase_blip(int x, int y, int hdg, const char *cs) {
    // Wipe a box big enough to cover the triangle in any orientation
    gfx->fillRect(x-9, y-9, 18, 18, C_BG);
    // Erase label by overdrawing exact text in black
    gfx->setTextColor(C_BG, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(x+8, y-4);
    gfx->print(cs);
    restore_rings_and_cross();
}

/** Paint a blip and record paint state */
void paint_blip(Aircraft &ac, int sx, int sy) {
    bool sel = (&ac - aircraft) == selected_idx;
    draw_blip_shape(sx, sy, ac.heading, sel ? C_SEL : C_BLIP);
    gfx->setTextColor(sel ? C_SEL : C_LBL, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(sx+8, sy-4);
    gfx->print(ac.callsign);
    ac.paint_x   = sx;
    ac.paint_y   = sy;
    ac.paint_hdg = ac.heading;
    strlcpy(ac.paint_cs, ac.callsign, sizeof(ac.paint_cs));
    ac.paint_valid = true;
}

/**
 * For each aircraft whose bearing is crossed by the sweep arm advancing
 * from prev_angle to new_angle: erase old painted position, repaint new.
 */
void sweep_paint_aircraft(float prev_angle, float new_angle) {
    xSemaphoreTake(ac_mutex, portMAX_DELAY);

    for (int i = 0; i < aircraft_count; i++) {
        Aircraft &ac = aircraft[i];
        if (!ac.has_pos || ac.bearing < 0) continue;
        if (millis() - ac.seen_ms > (uint32_t)(AIRCRAFT_MAX_AGE_S * 1000)) {
            // Expired — erase if still painted
            if (ac.paint_valid) {
                erase_blip(ac.paint_x, ac.paint_y, ac.heading, ac.paint_cs);
                ac.paint_valid = false;
            }
            continue;
        }

        bool crossed = (prev_angle <= new_angle)
            ? (ac.bearing >= prev_angle && ac.bearing < new_angle)
            : (ac.bearing >= prev_angle || ac.bearing < new_angle);
        if (!crossed) continue;

        // Erase old painted position (if any)
        if (ac.paint_valid)
            erase_blip(ac.paint_x, ac.paint_y, ac.heading, ac.paint_cs);

        // Paint new position
        int sx, sy;
        if (latlon_to_screen(ac.lat, ac.lon, &sx, &sy))
            paint_blip(ac, sx, sy);
        else
            ac.paint_valid = false;
    }

    xSemaphoreGive(ac_mutex);
}

// ─── Detail box ──────────────────────────────────────────────────────────────

static bool detail_visible = false;

void draw_detail_box() {
    xSemaphoreTake(ac_mutex, portMAX_DELAY);
    if (selected_idx < 0 || selected_idx >= aircraft_count) {
        xSemaphoreGive(ac_mutex); return;
    }
    Aircraft ac = aircraft[selected_idx];  // local copy
    xSemaphoreGive(ac_mutex);

    gfx->fillRect(100, 24, 160, 68, C_BOX_BG);
    gfx->drawRect(100, 24, 160, 68, C_BOX_BORD);
    gfx->setTextColor(C_LBL, C_BOX_BG);
    gfx->setTextSize(1);
    gfx->setCursor(106, 32); gfx->print(ac.callsign[0] ? ac.callsign : ac.hex);
    gfx->setCursor(106, 46); gfx->printf("Alt: %d ft", ac.altitude);
    gfx->setCursor(106, 60); gfx->printf("Spd: %d kts", ac.speed);
    gfx->setCursor(106, 74); gfx->printf("Hdg: %d deg", ac.heading);
    detail_visible = true;
}

void erase_detail_box() {
    gfx->fillRect(100, 24, 160, 68, C_BG);
    restore_rings_and_cross();
    detail_visible = false;
}

void draw_range_label() {
    gfx->fillRect(CX-40, SCREEN_HEIGHT-18, 80, 14, C_BG);
    gfx->setTextColor(C_LBL, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(CX-30, SCREEN_HEIGHT-14);
    gfx->printf("%.0f nm", range_nm);
}

int find_nearest(int tx, int ty) {
    int best = -1; float best_d = 18.0f;
    xSemaphoreTake(ac_mutex, portMAX_DELAY);
    for (int i = 0; i < aircraft_count; i++) {
        if (!aircraft[i].paint_valid) continue;
        float d = sqrtf((float)((tx-aircraft[i].paint_x)*(tx-aircraft[i].paint_x)+
                                (ty-aircraft[i].paint_y)*(ty-aircraft[i].paint_y)));
        if (d < best_d) { best_d=d; best=i; }
    }
    xSemaphoreGive(ac_mutex);
    return best;
}

void full_redraw() {
    // Invalidate all paint state so aircraft repaint on next sweep pass
    xSemaphoreTake(ac_mutex, portMAX_DELAY);
    for (int i = 0; i < aircraft_count; i++) aircraft[i].paint_valid = false;
    xSemaphoreGive(ac_mutex);
    draw_static_bg();
    draw_sweep(sweep_angle);
    draw_range_label();
    prev_sweep = sweep_angle;
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    pinMode(ENCODER_A, INPUT_PULLUP);
    pinMode(ENCODER_B, INPUT_PULLUP);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    gfx->begin();
    init_waveshare_amoled_glass((Arduino_ESP32QSPI *)bus);
    touch_init();

    // Splash screen while WiFi connects
    gfx->fillScreen(C_BG);
    gfx->setTextColor(C_SWEEP, C_BG);
    gfx->setTextSize(2);
    gfx->setCursor(45, 155); gfx->print("ADS-B Radar");
    gfx->setTextSize(1);
    gfx->setCursor(60, 180); gfx->print("Connecting WiFi...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.printf("WiFi: %s\n", WiFi.localIP().toString().c_str());

    ac_mutex = xSemaphoreCreateMutex();

    // Fetch task on Core 0 — main loop (sweep/render) runs on Core 1
    xTaskCreatePinnedToCore(fetch_task, "fetch", 8192, nullptr, 1, nullptr, 0);

    // Initial data fetch (blocking, before render starts)
    fetch_requested = true;
    while (fetch_busy || fetch_requested) delay(50);

    full_redraw();
}

void loop() {
    unsigned long now = millis();

    // Encoder poll
    static unsigned long last_enc_ms = 0;
    if (now - last_enc_ms >= 3) { poll_encoder(); last_enc_ms = now; }

    // Encoder → zoom
    static int last_steps = 0;
    int delta = encoder_steps - last_steps;
    if (delta != 0) {
        last_steps = encoder_steps;
        if (delta > 0) range_nm = max((float)MIN_RANGE_NM, range_nm / 1.5f);
        else           range_nm = min((float)MAX_RANGE_NM, range_nm * 1.5f);
        full_redraw();
        draw_range_label();
    }

    // Touch → select aircraft
    static bool was_touching = false;
    bool touching = read_touch();
    if (touching && !was_touching) {
        int hit = find_nearest(touch_x, touch_y);
        if (hit >= 0) {
            selected_idx = hit;
            xSemaphoreTake(ac_mutex, portMAX_DELAY);
            Aircraft &ac = aircraft[hit];
            if (ac.paint_valid)
                draw_blip_shape(ac.paint_x, ac.paint_y, ac.heading, C_SEL);
            xSemaphoreGive(ac_mutex);
            draw_detail_box();
        } else if (detail_visible) {
            selected_idx = -1;
            erase_detail_box();
        }
    }
    was_touching = touching;

    // Sweep — smooth single-arm rotation
    static unsigned long last_sweep_ms = 0;
    if (now - last_sweep_ms >= SWEEP_INTERVAL_MS) {
        float new_angle = fmodf(sweep_angle + SWEEP_STEP_DEG, 360.0f);
        if (prev_sweep >= 0) erase_sweep(prev_sweep);
        sweep_paint_aircraft(sweep_angle, new_angle);
        draw_sweep(new_angle);
        if (detail_visible) draw_detail_box();
        prev_sweep  = sweep_angle;
        sweep_angle = new_angle;
        last_sweep_ms = now;
    }

    // Trigger background fetch every FETCH_INTERVAL_MS
    static unsigned long last_fetch_ms = 0;
    if (now - last_fetch_ms >= FETCH_INTERVAL_MS && !fetch_busy) {
        fetch_requested = true;
        last_fetch_ms = now;
    }

    delay(5);
}
