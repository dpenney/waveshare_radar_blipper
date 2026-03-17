#include "ClockView.h"
#include <time.h>
#include <math.h>

void ClockView::draw(Arduino_GFX *gfx, int cx, int cy, int radius) {
    // Clear the screen/canvas
    gfx->fillScreen(0x0000); // Black

    // Draw the outer ring (altimeter style)
    uint16_t white = 0xFFFF;
    gfx->drawCircle(cx, cy, radius - 2, white);
    gfx->drawCircle(cx, cy, radius - 5, white);

    // Draw scale marks (0-9 for altimeter look)
    for (int i = 0; i < 60; i++) {
        float angle = i * 6.0f * M_PI / 180.0f;
        int r1 = (i % 5 == 0) ? radius - 25 : radius - 15;
        int r2 = radius - 7;
        
        int x1 = cx + r1 * sin(angle);
        int y1 = cy - r1 * cos(angle);
        int x2 = cx + r2 * sin(angle);
        int y2 = cy - r2 * cos(angle);
        
        gfx->drawLine(x1, y1, x2, y2, white);
        
        // Draw numbers (0, 1, 2... 9) every 5 ticks (60 / 12 = 5)
        if (i % 5 == 0) {
            int num = i / 6; // 0, 10, 20...
            if (num == 0) num = 0; // Just for clarity
            // Use 0-9 for altimeter look
            int alt_num = i / 6; 
            // In a clock we want 1-12 usually, but user asked for altimeter style.
            // Altimeters have 0-9.
            char buf[4];
            sprintf(buf, "%d", i / 6);
            if (i == 0) sprintf(buf, "0");
            
            int tr = radius - 45;
            int tx = cx + tr * sin(angle) - 10;
            int ty = cy - tr * cos(angle) - 8;
            
            gfx->setTextSize(3);
            gfx->setCursor(tx, ty);
            gfx->print(buf);
        }
    }

    // Get current time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        gfx->setCursor(cx - 60, cy);
        gfx->print("TIME ERROR");
        return;
    }

    // Hands
    float s_angle = timeinfo.tm_sec * 6.0f * M_PI / 180.0f;
    float m_angle = (timeinfo.tm_min * 6.0f + timeinfo.tm_sec * 0.1f) * M_PI / 180.0f;
    float h_angle = (timeinfo.tm_hour % 12 * 30.0f + timeinfo.tm_min * 0.5f) * M_PI / 180.0f;

    // Hour hand (short, fat)
    int h_len = radius * 0.5;
    int hx = cx + h_len * sin(h_angle);
    int hy = cy - h_len * cos(h_angle);
    gfx->drawLine(cx, cy, hx, hy, white);
    // Simulating breadth with offset lines
    gfx->drawLine(cx+1, cy, hx+1, hy, white); 
    gfx->drawLine(cx-1, cy, hx-1, hy, white);

    // Minute hand (long, medium)
    int m_len = radius * 0.75;
    int mx = cx + m_len * sin(m_angle);
    int my = cy - m_len * cos(m_angle);
    gfx->drawLine(cx, cy, mx, my, white);
    gfx->drawLine(cx+1, cy, mx+1, my, white);

    // Second hand (long, thin, maybe a small circle at the end)
    int s_len = radius * 0.85;
    int sx = cx + s_len * sin(s_angle);
    int sy = cy - s_len * cos(s_angle);
    gfx->drawLine(cx, cy, sx, sy, white);

    // Center hub
    gfx->fillCircle(cx, cy, 6, white);
    gfx->fillCircle(cx, cy, 3, 0x0000);
}
