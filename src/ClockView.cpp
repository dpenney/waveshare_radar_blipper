#include "ClockView.h"
#include <time.h>
#include <math.h>

void ClockView::draw(Arduino_GFX *gfx, int cx, int cy, int radius) {
    uint16_t white = 0xFFFF;
    uint16_t dim   = 0x4208; // Gray

    // Outer Bezel
    gfx->drawCircle(cx, cy, radius - 2, white);
    gfx->drawCircle(cx, cy, radius - 4, white);

    // Get current time
    struct tm timeinfo;
    bool time_valid = getLocalTime(&timeinfo);
    if (!time_valid || timeinfo.tm_year < 100) { // tm_year is years since 1900
        gfx->setTextColor(white);
        gfx->setTextSize(2);
        gfx->setCursor(cx - 80, cy - 10);
        gfx->print("WAITING FOR NTP...");
        return;
    }

    // Scale Marks & Numbers
    for (int i = 0; i < 60; i++) {
        float angle = i * 6.0f * M_PI / 180.0f;
        int r1 = (i % 5 == 0) ? radius - 35 : radius - 20;
        int r2 = radius - 7;
        
        int x1 = cx + r1 * sin(angle);
        int y1 = cy - r1 * cos(angle);
        int x2 = cx + r2 * sin(angle);
        int y2 = cy - r2 * cos(angle);
        
        gfx->drawLine(x1, y1, x2, y2, white);
        
        if (i % 5 == 0) {
            int val = i / 5;
            if (val == 0) val = 12;
            char buf[4];
            sprintf(buf, "%d", val);
            int tr = radius - 60;
            // Center numbers better
            int tx = cx + tr * sin(angle) - (val >= 10 ? 20 : 10);
            int ty = cy - tr * cos(angle) - 15;
            gfx->setTextSize(4);
            gfx->setCursor(tx, ty);
            gfx->print(buf);
        }
    }

    // Date Window (Kollsman Window / Altimeter Setting)
    // Positioned at 3 o'clock
    int wx = cx + radius * 0.45;
    int wy = cy - 20;
    gfx->drawRect(wx - 5, wy - 5, 60, 40, white);
    gfx->setTextColor(white);
    gfx->setTextSize(3);
    gfx->setCursor(wx + 5, wy + 5);
    gfx->printf("%02d", timeinfo.tm_mday);

    // Gauge Center Text
    gfx->setTextSize(1);
    gfx->setCursor(cx - 30, cy + 40);
    gfx->print("ALT-TIME");
    gfx->setCursor(cx - 35, cy + 55);
    gfx->print("29.92 IN HG");

    // Hand Angles
    float s_angle = timeinfo.tm_sec * 6.0f * M_PI / 180.0f;
    float m_angle = (timeinfo.tm_min * 6.0f + timeinfo.tm_sec * 0.1f) * M_PI / 180.0f;
    float h_angle = (timeinfo.tm_hour % 12 * 30.0f + timeinfo.tm_min * 0.5f) * M_PI / 180.0f;

    // Drawing Hands (Mechanical Needle Style)
    // Hour (Short & Thick)
    int h_len = radius * 0.55;
    int hx = cx + h_len * sin(h_angle);
    int hy = cy - h_len * cos(h_angle);
    gfx->drawLine(cx, cy, hx, hy, white);
    gfx->drawLine(cx+1, cy, hx+1, hy, white);
    gfx->drawLine(cx-1, cy, hx-1, hy, white);

    // Minute (Longer & Tapered)
    int m_len = radius * 0.8;
    int mx = cx + m_len * sin(m_angle);
    int my = cy - m_len * cos(m_angle);
    gfx->drawLine(cx, cy, mx, my, white);
    gfx->drawLine(cx+1, cy, mx+1, my, white);

    // Second (Thin Needle with Counterweight)
    int s_len = radius * 0.9;
    int sx = cx + s_len * sin(s_angle);
    int sy = cy - s_len * cos(s_angle);
    gfx->drawLine(cx, cy, sx, sy, white);
    
    // Counterweight (Small line opposite the second hand)
    int cw_len = 20;
    int cwx = cx - cw_len * sin(s_angle);
    int cwy = cy + cw_len * cos(s_angle);
    gfx->drawLine(cx, cy, cwx, cwy, white);

    // Hub Cover
    gfx->fillCircle(cx, cy, 10, white);
    gfx->drawCircle(cx, cy, 10, 0x0000);
    gfx->fillCircle(cx, cy, 4, 0x0000);
}
