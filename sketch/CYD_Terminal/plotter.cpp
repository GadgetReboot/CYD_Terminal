/*
 * plotter.cpp - Serial data plotter for CYD Terminal
 *
 * Incoming bytes are accumulated into a text buffer.  Each time a complete
 * "token" (terminated by whitespace / CR / LF / comma) is received it is
 * parsed as a floating-point number with atof().  Tokens that produce 0.0
 * only because they were pure whitespace/CR/LF are discarded so that line
 * endings never inject a spurious zero data-point.
 *
 * Display layout (status bar occupies y=0..19):
 *   y=20..39  : 20-px control bar
 *   y=40..239 : 200-px plot area, 320 px wide → 320 samples visible
 */

#include "plotter.h"
#include "display.h"
#include "config.h"

// ── Compile-time constants ───────────────────────────────────────────────────

#define PLOT_TOP        40          // Top of plot area (px)
#define PLOT_BOTTOM     239         // Bottom of plot area (px)
#define PLOT_HEIGHT     (PLOT_BOTTOM - PLOT_TOP + 1)   // 200 px
#define PLOT_WIDTH      SCREEN_WIDTH                   // 320 px
#define MAX_SAMPLES     PLOT_WIDTH  // one sample per pixel column

#define CTRL_BAR_Y      20          // y of control bar
#define CTRL_BAR_H      20          // height of control bar
#define CTRL_BAR_COLOR  0x2104      // dark grey  (RGB565)

// Control bar button geometry
// [MODE]  [▲]  [▼]   all on the right half; left half shows current value
#define BTN_MODE_X      160
#define BTN_MODE_Y      (CTRL_BAR_Y + 2)
#define BTN_MODE_W      70
#define BTN_MODE_H      16

#define BTN_UP_X        238
#define BTN_UP_Y        (CTRL_BAR_Y + 2)
#define BTN_UP_W        36
#define BTN_UP_H        16

#define BTN_DN_X        280
#define BTN_DN_Y        (CTRL_BAR_Y + 2)
#define BTN_DN_W        36
#define BTN_DN_H        16

// Manual scale step (fraction of current range)
#define SCALE_STEP      0.25f       // ±25 % per tap

// Colours
#define COL_BG          TFT_BLACK
#define COL_GRID        0x2104      // very dark grey
#define COL_LINE        TFT_GREEN
#define COL_DOT         TFT_WHITE
#define COL_LABEL       TFT_YELLOW
#define COL_BTN_AUTO    TFT_GREEN
#define COL_BTN_MANUAL  TFT_ORANGE
#define COL_BTN_ARROW   TFT_CYAN
#define COL_BTN_TXT     TFT_BLACK

// ── Module state ─────────────────────────────────────────────────────────────

static HardwareSerial* plotSerial = nullptr;

// Circular sample buffer
static float samples[MAX_SAMPLES];
static int   sampleCount  = 0;   // total samples ever received (unbounded)
static int   writeHead    = 0;   // next write index in circular buffer

// Serial parser state
static char   tokenBuf[32];
static int    tokenLen = 0;

// Scale
static bool   autoScale   = true;
static float  manualMin   = 0.0f;
static float  manualMax   = 100.0f;

// Last received value (shown in control bar)
static float  lastValue            = 0.0f;
static bool   hasData              = false;
static float  lastDisplayedValue   = 0.0f;   // what is currently painted on screen
static bool   lastDisplayedHasData = false;  // false forces first control bar draw

// Debounce for touch
static unsigned long lastTouchMs = 0;
#define TOUCH_DEBOUNCE_MS 250

// Redraw throttle – render at most once per REDRAW_INTERVAL_MS.
// Serial parsing always runs to completion first so no bytes are missed.
#define REDRAW_INTERVAL_MS 30
static unsigned long lastRedrawMs = 0;
static bool          pendingRedraw = false;

// ── Forward declarations ──────────────────────────────────────────────────────

static void drawControlBar();
static void drawPlot();
static void drawGrid();
static void pushSample(float v);
static void getDisplayRange(float& outMin, float& outMax);
static bool hitTest(uint16_t tx, uint16_t ty, int bx, int by, int bw, int bh);
static void drawButton(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg);
static float clampRange(float v, float lo, float hi);

// ── Public API ────────────────────────────────────────────────────────────────

void plotterInit(HardwareSerial* serial) {
  plotSerial   = serial;
  sampleCount  = 0;
  writeHead    = 0;
  tokenLen     = 0;
  autoScale    = true;
  manualMin    = 0.0f;
  manualMax    = 100.0f;
  lastValue            = 0.0f;
  hasData              = false;
  lastDisplayedValue   = 0.0f;
  lastDisplayedHasData = false;

  lastRedrawMs  = 0;
  pendingRedraw = false;

  // Clear the plot area + control bar
  tft.fillRect(0, CTRL_BAR_Y, SCREEN_WIDTH, SCREEN_HEIGHT - CTRL_BAR_Y, COL_BG);

  drawControlBar();
  drawGrid();
}

void plotterUpdate() {
  if (!plotSerial) return;

  // ── Phase 1: drain ALL available bytes before touching the display ──────
  // The ESP32 UART FIFO is only 128 bytes deep.  If we stop mid-stream to
  // paint the screen we risk an overrun and dropped bytes.  Parsing is cheap
  // (no floats allocated, just character accumulation), so we empty the FIFO
  // completely every call and only set a flag when new data is ready.

  while (plotSerial->available()) {
    char c = (char)plotSerial->read();

    if (c == '\r' || c == '\n' || c == ' ' || c == '\t' || c == ',') {
      if (tokenLen > 0) {
        tokenBuf[tokenLen] = '\0';

        // Validate: every character must be a legal float character
        // (digit, leading minus, one decimal point, 'e'/'E' for sci notation).
        // Any other character means this token is a corrupted fragment from a
        // buffer overrun – discard it entirely rather than feed garbage to atof().
        bool valid    = true;
        bool hasDigit = false;
        bool hasDot   = false;
        for (int i = 0; i < tokenLen && valid; i++) {
          char ch = tokenBuf[i];
          if      (ch >= '0' && ch <= '9') { hasDigit = true; }
          else if (ch == '-' && i == 0)    { /* leading minus OK */ }
          else if (ch == '+' && i == 0)    { /* leading plus OK  */ }
          else if (ch == '.' && !hasDot)   { hasDot = true; }
          else if (ch == 'e' || ch == 'E') { /* sci notation     */ }
          else                             { valid = false; }  // corrupted token
        }

        if (valid && hasDigit) {
          float v = atof(tokenBuf);

          // Outlier clamp: if we already have data and this value is more than
          // 10× the current displayed range away from the centre, treat it as a
          // corruption artefact and discard it.
          bool accept = true;
          if (hasData && sampleCount >= 2) {
            float lo, hi;
            getDisplayRange(lo, hi);
            float span   = hi - lo;
            float centre = (lo + hi) * 0.5f;
            if (span > 0.0f && fabsf(v - centre) > span * 10.0f) {
              accept = false;
            }
          }

          if (accept) {
            pushSample(v);
            lastValue     = v;
            hasData       = true;
            pendingRedraw = true;
          }
        }
        tokenLen = 0;
      }
      // Pure whitespace / CR / LF with no preceding digits → no zero sample
    } else {
      // If the incoming character is not a valid float character, it signals
      // the start of a corrupted sequence.  Reset the token accumulator so
      // we resync cleanly.
      char ch = c;
      bool legalFloatChar = (ch >= '0' && ch <= '9') ||
                             ch == '-' || ch == '+' ||
                             ch == '.' || ch == 'e' || ch == 'E';
      if (legalFloatChar) {
        if (tokenLen < (int)(sizeof(tokenBuf) - 1)) {
          tokenBuf[tokenLen++] = c;
        }
      } else {
        // Unexpected character mid-token → discard partial token and resync
        tokenLen = 0;
      }
    }
  }

  // ── Phase 2: redraw at most once per REDRAW_INTERVAL_MS ─────────────────
  // This keeps the display responsive while ensuring parsing is never blocked
  // by a slow SPI repaint.

  if (pendingRedraw) {
    unsigned long now = millis();
    if (now - lastRedrawMs >= REDRAW_INTERVAL_MS) {
      lastRedrawMs  = now;
      pendingRedraw = false;

      // Only repaint the control bar when the displayed value has actually
      // changed – redrawing it every frame wastes ~2 ms of SPI time and
      // adds a visible flicker band at the top of the plot area.
      bool valueChanged = (hasData != lastDisplayedHasData) ||
                          (hasData && lastValue != lastDisplayedValue);
      if (valueChanged) {
        drawControlBar();
        lastDisplayedValue   = lastValue;
        lastDisplayedHasData = hasData;
      }

      // Hold SPI CS asserted across the entire plot repaint so each
      // primitive (fillRect, drawLine, drawPixel …) does not pay the
      // per-transaction CS toggle overhead.  On a 55 MHz SPI bus this
      // cuts the blank-screen window noticeably.
      tft.startWrite();
      drawPlot();
      tft.endWrite();
    }
  }
}

void plotterHandleTouch() {
  uint16_t tx, ty;
  static bool wasPressed = false;

  if (!getTouch(&tx, &ty)) {
    wasPressed = false;
    return;
  }

  if (wasPressed) return;            // hold – wait for release
  wasPressed = true;

  unsigned long now = millis();
  if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
  lastTouchMs = now;

  // Only respond to control bar touches
  if (ty < CTRL_BAR_Y || ty > CTRL_BAR_Y + CTRL_BAR_H) return;

  // MODE button
  if (hitTest(tx, ty, BTN_MODE_X, BTN_MODE_Y, BTN_MODE_W, BTN_MODE_H)) {
    if (autoScale && hasData) {
      // Capture the current auto range BEFORE flipping the flag so that
      // getDisplayRange() still returns the live auto-computed values.
      float lo, hi;
      getDisplayRange(lo, hi);
      manualMin = lo;
      manualMax = hi;
    }
    autoScale = !autoScale;
    drawControlBar();
    drawPlot();
    return;
  }

  // Scale-up button: shrinks the Y range → plot appears larger/zoomed in
  if (!autoScale && hitTest(tx, ty, BTN_UP_X, BTN_UP_Y, BTN_UP_W, BTN_UP_H)) {
    float span    = manualMax - manualMin;
    float centre  = (manualMin + manualMax) * 0.5f;
    float newSpan = span * (1.0f - SCALE_STEP);   // zoom in → narrower range
    if (newSpan < 1e-4f) newSpan = 1e-4f;
    manualMin = centre - newSpan * 0.5f;
    manualMax = centre + newSpan * 0.5f;
    drawControlBar();
    drawPlot();
    return;
  }

  // Scale-down button: grows the Y range → plot appears smaller/zoomed out
  if (!autoScale && hitTest(tx, ty, BTN_DN_X, BTN_DN_Y, BTN_DN_W, BTN_DN_H)) {
    float span    = manualMax - manualMin;
    float centre  = (manualMin + manualMax) * 0.5f;
    float newSpan = span * (1.0f + SCALE_STEP);   // zoom out → wider range
    manualMin = centre - newSpan * 0.5f;
    manualMax = centre + newSpan * 0.5f;
    drawControlBar();
    drawPlot();
    return;
  }
}

void plotterExit() {
  plotSerial = nullptr;
  // Caller (main .ino) will repaint the screen
}

// ── Private helpers ───────────────────────────────────────────────────────────

static void pushSample(float v) {
  samples[writeHead] = v;
  writeHead = (writeHead + 1) % MAX_SAMPLES;
  if (sampleCount < MAX_SAMPLES) sampleCount++;
}

static void getDisplayRange(float& outMin, float& outMax) {
  if (!autoScale) {
    outMin = manualMin;
    outMax = manualMax;
    return;
  }

  if (sampleCount == 0) {
    outMin = 0.0f; outMax = 1.0f;
    return;
  }

  float lo =  1e30f;
  float hi = -1e30f;

  int n = sampleCount;
  if (n > MAX_SAMPLES) n = MAX_SAMPLES;

  int start = (writeHead - n + MAX_SAMPLES) % MAX_SAMPLES;
  for (int i = 0; i < n; i++) {
    float v = samples[(start + i) % MAX_SAMPLES];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }

  if (lo == hi) { lo -= 1.0f; hi += 1.0f; }

  // 5 % headroom
  float pad = (hi - lo) * 0.05f;
  outMin = lo - pad;
  outMax = hi + pad;
}

static void drawGrid() {
  // Horizontal grid lines (4 divisions)
  for (int i = 1; i < 4; i++) {
    int y = PLOT_TOP + (PLOT_HEIGHT * i) / 4;
    tft.drawFastHLine(0, y, PLOT_WIDTH, COL_GRID);
  }
  // Vertical grid lines (every 80 px → 4 divisions)
  for (int x = 80; x < PLOT_WIDTH; x += 80) {
    tft.drawFastVLine(x, PLOT_TOP, PLOT_HEIGHT, COL_GRID);
  }
}

static void drawButton(int x, int y, int w, int h,
                       const char* label, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(x, y, w, h, 3, bg);
  tft.drawRoundRect(x, y, w, h, 3, TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(1);
  tft.setTextColor(fg, bg);
  int len = strlen(label);
  int tx  = x + (w - len * 6) / 2;
  int ty  = y + (h - 8) / 2;
  tft.setCursor(tx, ty);
  tft.print(label);
}

static void drawControlBar() {
  tft.fillRect(0, CTRL_BAR_Y, SCREEN_WIDTH, CTRL_BAR_H, CTRL_BAR_COLOR);

  // Current value on the left
  tft.setTextSize(1);
  tft.setTextFont(1);
  tft.setTextColor(COL_LABEL, CTRL_BAR_COLOR);
  tft.setCursor(4, CTRL_BAR_Y + 6);
  if (hasData) {
    char buf[24];
    dtostrf(lastValue, 8, 3, buf);
    tft.print("Val:");
    tft.print(buf);
  } else {
    tft.print("Val: ---");
  }

  // MODE button
  uint16_t modeColor = autoScale ? COL_BTN_AUTO : COL_BTN_MANUAL;
  drawButton(BTN_MODE_X, BTN_MODE_Y, BTN_MODE_W, BTN_MODE_H,
             autoScale ? "AUTO" : "MANUAL", modeColor, COL_BTN_TXT);

  // Scale buttons (grey when auto, active when manual)
  uint16_t arrowColor = autoScale ? 0x4208 : COL_BTN_ARROW;
  drawButton(BTN_UP_X, BTN_UP_Y, BTN_UP_W, BTN_UP_H, "  +  ", arrowColor, COL_BTN_TXT);
  drawButton(BTN_DN_X, BTN_DN_Y, BTN_DN_W, BTN_DN_H, "  -  ", arrowColor, COL_BTN_TXT);
}

// Map a float value to a Y screen coordinate within the plot area
static inline int valueToY(float v, float lo, float hi) {
  if (hi == lo) return (PLOT_TOP + PLOT_BOTTOM) / 2;
  float norm = (v - lo) / (hi - lo);
  norm = clampRange(norm, 0.0f, 1.0f);
  return (int)(PLOT_BOTTOM - norm * (PLOT_HEIGHT - 1));
}

static void drawPlot() {
  // Clear plot area
  tft.fillRect(0, PLOT_TOP, PLOT_WIDTH, PLOT_HEIGHT, COL_BG);
  drawGrid();

  if (sampleCount == 0) return;

  float lo, hi;
  getDisplayRange(lo, hi);

  // Draw Y-axis labels (min / mid / max)
  tft.setTextSize(1);
  tft.setTextFont(1);
  tft.setTextColor(COL_LABEL, COL_BG);

  char buf[16];
  dtostrf(hi, 6, 2, buf);
  tft.setCursor(0, PLOT_TOP + 1);
  tft.print(buf);

  dtostrf((lo + hi) * 0.5f, 6, 2, buf);
  tft.setCursor(0, PLOT_TOP + PLOT_HEIGHT / 2 - 4);
  tft.print(buf);

  dtostrf(lo, 6, 2, buf);
  tft.setCursor(0, PLOT_BOTTOM - 8);
  tft.print(buf);

  // Determine how many samples to draw (up to PLOT_WIDTH)
  int n = sampleCount;
  if (n > MAX_SAMPLES) n = MAX_SAMPLES;

  // The oldest sample in the draw window
  int startIdx = (writeHead - n + MAX_SAMPLES) % MAX_SAMPLES;

  int prevX = -1, prevY = -1;

  for (int i = 0; i < n; i++) {
    float v  = samples[(startIdx + i) % MAX_SAMPLES];
    int   px = i;
    int   py = valueToY(v, lo, hi);

    if (prevX >= 0) {
      tft.drawLine(prevX, prevY, px, py, COL_LINE);
    }

    tft.drawPixel(px, py, COL_DOT);

    prevX = px;
    prevY = py;
  }
}

static bool hitTest(uint16_t tx, uint16_t ty,
                    int bx, int by, int bw, int bh) {
  return (tx >= (uint16_t)bx && tx <= (uint16_t)(bx + bw) &&
          ty >= (uint16_t)by && ty <= (uint16_t)(by + bh));
}

static float clampRange(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
