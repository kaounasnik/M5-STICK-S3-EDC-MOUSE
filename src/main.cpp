#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>
#include "embedded_assets.h"

// ==========================================
// Choose screen type: "1" for .png, "2" for .gif, "3" for .svg
// ==========================================
#define SCREEN_TYPE 3

#if SCREEN_TYPE != 1 && SCREEN_TYPE != 2 && SCREEN_TYPE != 3
#error "SCREEN_TYPE must be 1 (.png), 2 (.gif), or 3 (.svg)"
#endif

// Only compile the decoder needed for the selected & present asset
#if SCREEN_TYPE == 2 && HAS_SCREEN_GIF
#include <AnimatedGIF.h>
#endif

#if SCREEN_TYPE == 3 && HAS_SCREEN_SVG
#include <stdio.h>
#include <string.h>
#include <cmath>
using std::isnan;
#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#endif

// ==========================================
// DISPLAY & AUTO-DIM SETTINGS
// ==========================================
#define SCREEN_BRIGHTNESS     128    // Active brightness (0-255; lower saves more battery)
#define IDLE_DIM_MS           30000  // Turn backlight off after 30s of inactivity

// ==========================================
// CURSOR TUNING (UNCHANGED)
// ==========================================
#define SENSITIVITY      45.0f   // Higher = faster cursor
#define DEADZONE         0.035f  // Acceleration (in g) ignored as sensor noise
#define FRICTION         0.92f   // Velocity decay per tick
#define ZUPT_HOLD_MS     50      // Milliseconds of stillness before hard-zeroing velocity
#define MAX_VELOCITY     1.5f    // Clamps extreme bumps/taps

#define SWAP_XY          false
#define INVERT_X         false
#define INVERT_Y         true

// ==========================================
// DIAL TWIST SCROLL TUNING (UNCHANGED)
// ==========================================
#define SCROLL_DEADZONE_DPS   25.0f  // Minimum twist speed (deg/s) to ignore hand jitter
#define SCROLL_DEG_PER_NOTCH  12.0f  // Degrees of twist per scroll wheel click
#define INVERT_SCROLL         false  // Flip to true to reverse scroll direction

// ==========================================
// BATTERY OVERLAY GEOMETRY & POLL RATE
// ==========================================
#define BATTERY_UPDATE_MS     5000
#define BAT_PILL_X            60
#define BAT_PILL_Y            4
#define BAT_PILL_W            70
#define BAT_PILL_H            18

// ==========================================
// GLOBALS
// ==========================================
BleMouse bleMouse("M5StickS3 Mouse", "M5Stack", 100);

#if SCREEN_TYPE == 2 && HAS_SCREEN_GIF
AnimatedGIF gif;
static uint16_t gifLineBuf[240];
bool gifOpen = false;
unsigned long nextGifFrameMs = 0;
#endif

float biasX = 0.0f;
float biasY = 0.0f;
float biasGyroZ = 0.0f;

float vx = 0.0f;
float vy = 0.0f;
float remX = 0.0f;
float remY = 0.0f;
float scrollAccum = 0.0f;

unsigned long lastMicros = 0;
unsigned long stillStartMs = 0;
unsigned long bothHeldStartMs = 0;
unsigned long lastBatCheckMs = 0;
unsigned long lastActivityMs = 0;

int lastBatLevel = -1;
bool lastCharging = false;
bool wasConnected = false;
bool isScreenDimmed = false;

void drawBatteryOverlay(bool forceRedraw = false);

// ==========================================
// AUTO-DIM / WAKE HELPERS
// ==========================================
void wakeScreen() {
    lastActivityMs = millis();
    if (isScreenDimmed) {
        isScreenDimmed = false;
        M5.Display.setBrightness(SCREEN_BRIGHTNESS);
        drawBatteryOverlay(true);
#if SCREEN_TYPE == 2 && HAS_SCREEN_GIF
        nextGifFrameMs = millis();
#endif
    }
}

void dimScreen() {
    if (!isScreenDimmed) {
        isScreenDimmed = true;
        M5.Display.setBrightness(0);
    }
}

// ==========================================
// GIF SCANLINE RENDERER (PURE-BLACK TRANSPARENCY + BATTERY CLIP)
// ==========================================
#if SCREEN_TYPE == 2 && HAS_SCREEN_GIF
void pushScanlineClipped(int x, int y, int w, const uint16_t* pixels) {
    if (w <= 0) return;

    if (y < BAT_PILL_Y || y >= BAT_PILL_Y + BAT_PILL_H) {
        M5.Display.pushImage(x, y, w, 1, (const lgfx::rgb565_t*)pixels);
        return;
    }

    if (x < BAT_PILL_X) {
        int leftW = min(w, BAT_PILL_X - x);
        M5.Display.pushImage(x, y, leftW, 1, (const lgfx::rgb565_t*)pixels);
    }

    int pillRight = BAT_PILL_X + BAT_PILL_W;
    int rightStart = max(x, pillRight);
    int rightEnd = x + w;
    if (rightStart < rightEnd) {
        M5.Display.pushImage(rightStart, y, rightEnd - rightStart, 1,
                             (const lgfx::rgb565_t*)(pixels + (rightStart - x)));
    }
}

void GIFDraw(GIFDRAW *pDraw) {
    int y = pDraw->iY + pDraw->y;
    if (y < 0 || y >= M5.Display.height()) return;

    int x0 = pDraw->iX;
    int iWidth = pDraw->iWidth;
    if (x0 >= M5.Display.width()) return;
    if (x0 + iWidth > M5.Display.width()) {
        iWidth = M5.Display.width() - x0;
    }
    if (iWidth <= 0) return;
    if (iWidth > 240) iWidth = 240;

    uint8_t *s = pDraw->pPixels;
    uint16_t *usPalette = pDraw->pPalette;

    M5.Display.startWrite();
    if (pDraw->ucDisposalMethod == 2 && pDraw->ucHasTransparency) {
        uint8_t ucTransparent = pDraw->ucTransparent;
        for (int x = 0; x < iWidth; x++) {
            gifLineBuf[x] = (s[x] == ucTransparent) ? 0x0000 : usPalette[s[x]];
        }
        pushScanlineClipped(x0, y, iWidth, gifLineBuf);
    } else if (pDraw->ucHasTransparency) {
        uint8_t ucTransparent = pDraw->ucTransparent;
        int x = 0;
        while (x < iWidth) {
            while (x < iWidth && s[x] == ucTransparent) {
                x++;
            }
            if (x >= iWidth) break;

            int runStart = x;
            int runLen = 0;
            while (x < iWidth && s[x] != ucTransparent) {
                gifLineBuf[runLen++] = usPalette[s[x++]];
            }
            pushScanlineClipped(x0 + runStart, y, runLen, gifLineBuf);
        }
    } else {
        for (int x = 0; x < iWidth; x++) {
            gifLineBuf[x] = usPalette[s[x]];
        }
        pushScanlineClipped(x0, y, iWidth, gifLineBuf);
    }
    M5.Display.endWrite();
}

void stopGif() {
    if (gifOpen) {
        gif.close();
        gifOpen = false;
    }
}
#endif

// ==========================================
// SVG BANDED RENDERER (AUTO-CROPS TO ARTWORK BOUNDS)
// ==========================================
#if SCREEN_TYPE == 3 && HAS_SCREEN_SVG
#define SVG_SCALE_FILL 0.92f // 0.92 = fills 92% of screen; raise above 1.0 to zoom in further

void drawEmbeddedSvg() {
    if (screen_svg_size == 0) return;

    // nsvgParse modifies its input string in place, so copy from flash to RAM
    char* svgCopy = (char*)malloc(screen_svg_size + 1);
    if (!svgCopy) return;
    memcpy(svgCopy, screen_svg_data, screen_svg_size);
    svgCopy[screen_svg_size] = '\0';

    NSVGimage* img = nsvgParse(svgCopy, "px", 96.0f);
    free(svgCopy);
    if (!img) return;

    // Compute tight bounding box of all visible shapes (ignores empty page margins)
    float minX = 1e30f, minY = 1e30f;
    float maxX = -1e30f, maxY = -1e30f;
    bool hasBounds = false;

    for (NSVGshape* shape = img->shapes; shape != nullptr; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE)) continue;
        float pad = shape->strokeWidth * 0.5f;
        if (shape->bounds[2] > shape->bounds[0] || shape->bounds[3] > shape->bounds[1]) {
            if (shape->bounds[0] - pad < minX) minX = shape->bounds[0] - pad;
            if (shape->bounds[1] - pad < minY) minY = shape->bounds[1] - pad;
            if (shape->bounds[2] + pad > maxX) maxX = shape->bounds[2] + pad;
            if (shape->bounds[3] + pad > maxY) maxY = shape->bounds[3] + pad;
            hasBounds = true;
        }
    }

    // Fallback to document dimensions if no shape bounds were found
    if (!hasBounds || (maxX - minX) <= 0.0f || (maxY - minY) <= 0.0f) {
        minX = 0.0f;
        minY = 0.0f;
        maxX = img->width;
        maxY = img->height;
    }

    float artW = maxX - minX;
    float artH = maxY - minY;
    if (artW <= 0.0f || artH <= 0.0f) {
        nsvgDelete(img);
        return;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(img);
        return;
    }

    const int dispW = M5.Display.width();
    const int dispH = M5.Display.height();
    const int topMargin = BAT_PILL_Y + BAT_PILL_H + 4; // Keeps artwork clear of battery pill
    const int availH = dispH - topMargin;
    const int BAND_H = 24;

    float scaleX = ((float)dispW * SVG_SCALE_FILL) / artW;
    float scaleY = ((float)availH * SVG_SCALE_FILL) / artH;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;

    float artCenterX = (minX + maxX) * 0.5f;
    float artCenterY = (minY + maxY) * 0.5f;
    float offsetX = ((float)dispW * 0.5f) - (artCenterX * scale);
    float offsetY = ((float)topMargin + (float)availH * 0.5f) - (artCenterY * scale);

    uint8_t* rgba = (uint8_t*)malloc((size_t)dispW * BAND_H * 4);
    uint16_t* rgb565 = (uint16_t*)malloc((size_t)dispW * BAND_H * sizeof(uint16_t));

    if (rgba && rgb565) {
        M5.Display.startWrite();
        for (int y = 0; y < dispH; y += BAND_H) {
            int h = min(BAND_H, dispH - y);
            nsvgRasterize(rast, img, offsetX, offsetY - (float)y, scale,
                          rgba, dispW, h, dispW * 4);

            int count = dispW * h;
            for (int i = 0; i < count; i++) {
                uint16_t a = rgba[i * 4 + 3];
                uint8_t r = (uint8_t)((rgba[i * 4 + 0] * a) / 255);
                uint8_t g = (uint8_t)((rgba[i * 4 + 1] * a) / 255);
                uint8_t b = (uint8_t)((rgba[i * 4 + 2] * a) / 255);
                rgb565[i] = M5.Display.color565(r, g, b);
            }
            M5.Display.pushImage(0, y, dispW, h, (const lgfx::rgb565_t*)rgb565);
        }
        M5.Display.endWrite();
    }

    if (rgb565) free(rgb565);
    if (rgba) free(rgba);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
}
#endif

// ==========================================
// DISPLAY & BATTERY OVERLAY
// ==========================================
void drawBatteryOverlay(bool forceRedraw) {
    int rawLevel = M5.Power.getBatteryLevel();
    int level = (rawLevel < 0) ? 0 : constrain(rawLevel, 0, 100);
    bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);

    // Keep PC Bluetooth battery readout synced even while screen is dimmed
    if (bleMouse.isConnected() && level != lastBatLevel) {
        bleMouse.setBatteryLevel((uint8_t)level);
    }

    if (isScreenDimmed) {
        lastBatLevel = level;
        lastCharging = charging;
        return;
    }

    if (!forceRedraw && level == lastBatLevel && charging == lastCharging) {
        return;
    }

    lastBatLevel = level;
    lastCharging = charging;

    uint16_t fillColor = TFT_GREEN;
    if (charging) {
        fillColor = TFT_CYAN;
    } else if (level <= 20) {
        fillColor = TFT_RED;
    } else if (level <= 50) {
        fillColor = TFT_YELLOW;
    }
    uint16_t outlineColor = charging ? TFT_CYAN : TFT_WHITE;

    M5.Display.startWrite();
    M5.Display.fillRect(BAT_PILL_X, BAT_PILL_Y, BAT_PILL_W, BAT_PILL_H, M5.Display.color565(18, 18, 18));
    M5.Display.drawRect(BAT_PILL_X, BAT_PILL_Y, BAT_PILL_W, BAT_PILL_H, M5.Display.color565(65, 65, 65));

    char buf[6];
    snprintf(buf, sizeof(buf), "%3d%%", level);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(18, 18, 18));
    M5.Display.setCursor(BAT_PILL_X + 5, BAT_PILL_Y + 5);
    M5.Display.print(buf);

    const int batX = BAT_PILL_X + 34;
    const int batY = BAT_PILL_Y + 3;
    const int batW = 28;
    const int batH = 12;

    M5.Display.drawRoundRect(batX, batY, batW, batH, 2, outlineColor);
    M5.Display.fillRect(batX + batW, batY + 3, 3, 6, outlineColor);

    int fillW = (24 * level) / 100;
    if (fillW > 0) {
        M5.Display.fillRect(batX + 2, batY + 2, fillW, batH - 4, fillColor);
    }
    M5.Display.endWrite();
}

void updateScreen(const char* status, uint16_t color) {
#if SCREEN_TYPE == 2 && HAS_SCREEN_GIF
    stopGif();
#endif
    wakeScreen();
    M5.Display.fillScreen(TFT_BLACK);
    drawBatteryOverlay(true);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setCursor(8, 30);
    M5.Display.setTextSize(2);
    M5.Display.println("M5 MOUSE");
    M5.Display.setTextSize(1);
    M5.Display.setCursor(8, 60);
    M5.Display.println(status);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor(8, 95);
    M5.Display.println("BtnA: L-Click");
    M5.Display.setCursor(8, 110);
    M5.Display.println("BtnB: R-Click");
    M5.Display.setCursor(8, 125);
    M5.Display.println("Twist: Scroll");
    M5.Display.setCursor(8, 145);
    M5.Display.println("Hold A+B: Zero");
}

void showActiveMedia() {
    wakeScreen();
    M5.Display.fillScreen(TFT_BLACK);

#if SCREEN_TYPE == 1 && HAS_SCREEN_PNG
    M5.Display.drawPng(screen_png_data, screen_png_size, 0, 0);
    drawBatteryOverlay(true);
#elif SCREEN_TYPE == 2 && HAS_SCREEN_GIF
    stopGif();
    gifOpen = (gif.open((uint8_t*)screen_gif_data, (int)screen_gif_size, GIFDraw) == 1);
    drawBatteryOverlay(true);
    if (gifOpen) {
        nextGifFrameMs = millis();
    }
#elif SCREEN_TYPE == 3 && HAS_SCREEN_SVG
    drawEmbeddedSvg();
    drawBatteryOverlay(true);
#else
    // Selected asset is not present in src/ -> clean black screen
    drawBatteryOverlay(true);
#endif
}

// ==========================================
// IMU CALIBRATION
// ==========================================
void calibrateIMU() {
    updateScreen("KEEP FLAT!\nCalibrating...", TFT_YELLOW);
    delay(400);

    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumGz = 0.0f;
    const int samples = 200;

    for (int i = 0; i < samples; i++) {
        float ax, ay, az;
        float gx, gy, gz;
        M5.Imu.getAccel(&ax, &ay, &az);
        M5.Imu.getGyro(&gx, &gy, &gz);
        sumX += ax;
        sumY += ay;
        sumGz += gz;
        delay(5);
    }

    biasX = sumX / samples;
    biasY = sumY / samples;
    biasGyroZ = sumGz / samples;

    vx = 0.0f;
    vy = 0.0f;
    remX = 0.0f;
    remY = 0.0f;
    scrollAccum = 0.0f;

    if (bleMouse.isConnected()) {
        showActiveMedia();
    } else {
        updateScreen("BLE: WAITING...", TFT_CYAN);
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(2); // 180-degree rotated portrait (135x240)
    M5.Display.setBrightness(SCREEN_BRIGHTNESS);

#if SCREEN_TYPE == 2 && HAS_SCREEN_GIF
    gif.begin(LITTLE_ENDIAN_PIXELS);
#endif

    if (!M5.Imu.isEnabled()) {
        updateScreen("IMU ERROR!", TFT_RED);
        while (true) {
            delay(1000);
        }
    }

    calibrateIMU();
    bleMouse.begin();
    lastMicros = micros();
    lastBatCheckMs = millis();
    lastActivityMs = millis();
}

void loop() {
    M5.update();

    // Auto-dim screen after IDLE_DIM_MS of inactivity
    if (!isScreenDimmed && (millis() - lastActivityMs >= IDLE_DIM_MS)) {
        dimScreen();
    }

    // Refresh top battery pill every 5s (skips LCD draw while dimmed)
    if (millis() - lastBatCheckMs >= BATTERY_UPDATE_MS) {
        lastBatCheckMs = millis();
        drawBatteryOverlay(false);
    }

    // Update display only when BLE connection state changes
    bool connected = bleMouse.isConnected();
    if (connected != wasConnected) {
        wasConnected = connected;
        if (connected) {
            showActiveMedia();
        } else {
            updateScreen("BLE: WAITING...", TFT_CYAN);
        }
    }

    // Hold both buttons for 1s to re-zero drift
    if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
        wakeScreen();
        if (bothHeldStartMs == 0) {
            bothHeldStartMs = millis();
        } else if (millis() - bothHeldStartMs > 1000) {
            bleMouse.release(MOUSE_LEFT);
            bleMouse.release(MOUSE_RIGHT);
            calibrateIMU();
            bothHeldStartMs = 0;
            return;
        }
    } else {
        bothHeldStartMs = 0;
    }

    if (!connected) {
        if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
            wakeScreen();
        }
        delay(20);
        return;
    }

    // Non-blocking GIF frame advance (automatically paused while dimmed)
#if SCREEN_TYPE == 2 && HAS_SCREEN_GIF
    if (!isScreenDimmed && gifOpen && (long)(millis() - nextGifFrameMs) >= 0) {
        int frameDelayMs = 0;
        if (gif.playFrame(false, &frameDelayMs) == 0) {
            gif.reset();
        }
        if (frameDelayMs < 20) frameDelayMs = 20;
        nextGifFrameMs = millis() + (unsigned long)frameDelayMs;
    }
#endif

    // Handle Mouse Buttons
    if (M5.BtnA.wasPressed())  { wakeScreen(); bleMouse.press(MOUSE_LEFT); }
    if (M5.BtnA.wasReleased()) { wakeScreen(); bleMouse.release(MOUSE_LEFT); }
    if (M5.BtnB.wasPressed())  { wakeScreen(); bleMouse.press(MOUSE_RIGHT); }
    if (M5.BtnB.wasReleased()) { wakeScreen(); bleMouse.release(MOUSE_RIGHT); }

    // Run IMU kinematics at ~100Hz (every 10,000 microseconds)
    unsigned long now = micros();
    if (now - lastMicros < 10000) {
        return;
    }
    float dt = (now - lastMicros) / 1000000.0f;
    lastMicros = now;

    // ================
    // CURSOR MOVEMENT 
    // ================
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float linX = ax - biasX;
    float linY = ay - biasY;

    if (SWAP_XY) {
        float tmp = linX;
        linX = linY;
        linY = tmp;
    }
    if (INVERT_X) linX = -linX;
    if (INVERT_Y) linY = -linY;

    bool isStill = (fabsf(linX) < DEADZONE) && (fabsf(linY) < DEADZONE);

    if (isStill) {
        if (stillStartMs == 0) {
            stillStartMs = millis();
        }
        vx *= 0.70f;
        vy *= 0.70f;

        if (millis() - stillStartMs >= ZUPT_HOLD_MS) {
            vx = 0.0f;
            vy = 0.0f;
            remX = 0.0f;
            remY = 0.0f;
        }
    } else {
        stillStartMs = 0;
        vx += linX * dt;
        vy += linY * dt;

        vx *= FRICTION;
        vy *= FRICTION;
    }

    vx = constrain(vx, -MAX_VELOCITY, MAX_VELOCITY);
    vy = constrain(vy, -MAX_VELOCITY, MAX_VELOCITY);

    float moveX = (vx * SENSITIVITY) + remX;
    float moveY = (vy * SENSITIVITY) + remY;

    int8_t dx = (int8_t)constrain((int)roundf(moveX), -127, 127);
    int8_t dy = (int8_t)constrain((int)roundf(moveY), -127, 127);

    remX = moveX - dx;
    remY = moveY - dy;

    // ==========================================
    // DIAL TWIST SCROLL (Z-AXIS GYROSCOPE)
    // ==========================================
    float gx, gy, gz;
    M5.Imu.getGyro(&gx, &gy, &gz);
    float yawRate = gz - biasGyroZ;
    int8_t scroll = 0;

    if (fabsf(yawRate) > SCROLL_DEADZONE_DPS) {
        float effectiveYaw = (yawRate > 0.0f)
            ? (yawRate - SCROLL_DEADZONE_DPS)
            : (yawRate + SCROLL_DEADZONE_DPS);

        if (INVERT_SCROLL) effectiveYaw = -effectiveYaw;

        scrollAccum += effectiveYaw * dt;

        while (scrollAccum >= SCROLL_DEG_PER_NOTCH) {
            scroll++;
            scrollAccum -= SCROLL_DEG_PER_NOTCH;
        }
        while (scrollAccum <= -SCROLL_DEG_PER_NOTCH) {
            scroll--;
            scrollAccum += SCROLL_DEG_PER_NOTCH;
        }
    } else {
        scrollAccum *= 0.8f;
    }

    // Send cursor movement + scroll and wake screen on any motion
    if (dx != 0 || dy != 0 || scroll != 0) {
        wakeScreen();
        bleMouse.move(dx, dy, scroll);
    }
}