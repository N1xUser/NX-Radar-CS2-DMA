// ==========================================================
//  NXRadar – ESP32-S3 Native Arduino (LovyanGFX)
// ==========================================================
//
//  REQUIRED LIBRARIES (install via Arduino Library Manager):
//    - LovyanGFX
//
//  BOARD: ESP32S3 Dev Module
//  USB CDC On Boot: Enabled
//  USB Mode: Hardware CDC and JTAG
// ==========================================================

#include <LovyanGFX.hpp>
#include <math.h>

#define FW_VERSION "v1.0.0"

// ──────────────────────────────────────────────
//  LovyanGFX Configuration
// ──────────────────────────────────────────────
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789      _panel_instance;
  lgfx::Bus_SPI           _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000; // 80 MHz SPI for max FPS
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 8;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 10;
      cfg.pin_rst          = 9;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 240;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 2;     // 180 degrees (upside down)
      cfg.readable         = false;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
LGFX_Sprite spr(&tft);

// ──────────────────────────────────────────────
//  LAYOUT CONSTANTS
// ──────────────────────────────────────────────
#define SCREEN_WIDTH   240
#define SCREEN_HEIGHT  240

#define RADAR_Y_START    5
#define RADAR_HEIGHT   150
#define RADAR_Y_END    (RADAR_Y_START + RADAR_HEIGHT)
#define RADAR_CX       (SCREEN_WIDTH / 2)
#define RADAR_CY       (RADAR_Y_START + (RADAR_HEIGHT / 2) + 50)

#define LIST_Y_START   160
#define ROW_HEIGHT      15

// ──────────────────────────────────────────────
//  COLOURS
// ──────────────────────────────────────────────
#define C_BLACK   TFT_BLACK
#define C_WHITE   TFT_WHITE
#define C_GREEN   TFT_GREEN
#define C_RED     TFT_RED
#define C_YELLOW  TFT_YELLOW
#define C_GREY    tft.color565(0x33, 0x33, 0x33)

static const uint16_t SLOT_COLORS[] = {
  TFT_RED,
  TFT_YELLOW,
  TFT_CYAN,
  0xD341, // Chocolate
  TFT_WHITE
};

// ──────────────────────────────────────────────
//  GLOBALS
// ──────────────────────────────────────────────
static const float scale = (SCREEN_WIDTH / 2.0f) / 2100.0f;
bool is_connected   = false;
bool needs_refresh  = true;

// ──────────────────────────────────────────────
//  ENEMY SLOT
// ──────────────────────────────────────────────
#define MAX_SLOTS 5
#define NAME_MAX_LEN 11

struct EnemySlot {
  int     index;
  uint16_t color;
  char    assigned_name[NAME_MAX_LEN];
  bool    has_name;
  int     dot_x, dot_y;
  int     last_hp;
  char    last_name[NAME_MAX_LEN];
  bool    hidden;
  bool    dot_hidden;
  bool    is_dead;
};

EnemySlot slots[MAX_SLOTS];

void initSlot(int i) {
  slots[i].index      = i;
  slots[i].color      = SLOT_COLORS[i];
  slots[i].has_name   = false;
  slots[i].assigned_name[0] = '\0';
  slots[i].dot_x      = -1;
  slots[i].dot_y      = -1;
  slots[i].last_hp    = -1;
  slots[i].last_name[0] = '\0';
  slots[i].hidden     = true;
  slots[i].dot_hidden = true;
  slots[i].is_dead    = false;
}

// ──────────────────────────────────────────────
//  BOMB SITE
// ──────────────────────────────────────────────
bool   bomb_visible = false;
char   bomb_site_char = '?';

// ──────────────────────────────────────────────
//  DRAWING HELPERS
// ──────────────────────────────────────────────
void drawWaitingScreen() {
  spr.fillSprite(C_BLACK);
  spr.setTextColor(C_GREEN, C_BLACK);
  spr.setTextSize(2);
  spr.setTextDatum(middle_center);
  spr.drawString("WAITING FOR", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 20);
  spr.drawString("PROGRAM...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 4);
  
  // Show firmware version at the bottom
  spr.setTextSize(1);
  spr.setTextColor(C_GREY, C_BLACK);
  spr.setTextDatum(bottom_center);
  spr.drawString("NXRadar " FW_VERSION, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 5);
  spr.pushSprite(0, 0);
}

void drawRadarFrame() {
  spr.fillSprite(C_BLACK);

  spr.drawFastHLine(0, RADAR_Y_START,     SCREEN_WIDTH, C_WHITE);
  spr.drawFastHLine(0, RADAR_Y_START + 1, SCREEN_WIDTH, C_WHITE);
  spr.drawFastHLine(0, RADAR_Y_END,       SCREEN_WIDTH, C_WHITE);
  spr.drawFastHLine(0, RADAR_Y_END + 1,   SCREEN_WIDTH, C_WHITE);
  spr.drawFastVLine(0, RADAR_Y_START,      RADAR_HEIGHT, C_WHITE);
  spr.drawFastVLine(1, RADAR_Y_START,      RADAR_HEIGHT, C_WHITE);
  spr.drawFastVLine(SCREEN_WIDTH - 1, RADAR_Y_START, RADAR_HEIGHT, C_WHITE);
  spr.drawFastVLine(SCREEN_WIDTH - 2, RADAR_Y_START, RADAR_HEIGHT, C_WHITE);

  spr.fillCircle(RADAR_CX, RADAR_CY, 3, C_GREEN);

  if (bomb_visible) {
    spr.setTextColor(C_YELLOW, C_BLACK);
    spr.setTextSize(1);
    spr.setTextDatum(top_center);
    char buf[2] = { bomb_site_char, '\0' };
    spr.drawString(buf, RADAR_CX, RADAR_Y_START + 5);
  }

  for (int i = 0; i < MAX_SLOTS; i++) {
    EnemySlot &s = slots[i];
    if (s.hidden) continue;

    if (!s.dot_hidden) {
      spr.fillCircle(s.dot_x, s.dot_y, 3, s.color);
    }

    int base_y = LIST_Y_START + (i * ROW_HEIGHT);

    if (s.is_dead) {
      spr.setTextColor(C_GREY, C_BLACK);
      spr.setTextSize(1);
      spr.setTextDatum(middle_left);
      spr.drawString(s.last_name, 5, base_y + 6);

      int txt_w = strlen(s.last_name) * 6;
      if (txt_w < 1) txt_w = 1;
      spr.drawFastHLine(3, base_y + 5, txt_w + 4, C_RED);
      spr.drawFastHLine(3, base_y + 6, txt_w + 4, C_RED);
    } else {
      spr.setTextColor(s.color, C_BLACK);
      spr.setTextSize(1);
      spr.setTextDatum(middle_left);
      spr.drawString(s.last_name, 5, base_y + 6);

      spr.fillRect(150, base_y + 3, 80, 6, C_GREY);

      if (s.last_hp > 0) {
        float pct = s.last_hp / 100.0f;
        int w = (int)(80.0f * pct);
        if (w < 1) w = 1;
        uint16_t bar_color = (s.last_hp > 30) ? C_GREEN : C_RED;
        spr.fillRect(150, base_y + 3, w, 6, bar_color);
      }
    }
  }

  spr.pushSprite(0, 0);
}

// ──────────────────────────────────────────────
//  SLOT LOGIC
// ──────────────────────────────────────────────
void markDead(int idx) {
  EnemySlot &s = slots[idx];
  s.hidden    = false;
  s.dot_hidden = true;
  s.is_dead   = true;
  s.last_hp   = 0;
}

void updateSlot(int idx, int sx, int sy, int hp, const char* name) {
  EnemySlot &s = slots[idx];
  if (s.hidden) s.hidden = false;
  s.is_dead = false;

  strncpy(s.assigned_name, name, NAME_MAX_LEN - 1);
  s.assigned_name[NAME_MAX_LEN - 1] = '\0';
  s.has_name = true;

  bool in_bounds = (sx > 2 && sx < SCREEN_WIDTH - 2 && sy > RADAR_Y_START && sy < RADAR_Y_END - 2);

  if (in_bounds) {
    s.dot_x = sx;
    s.dot_y = sy;
    s.dot_hidden = false;
  } else {
    s.dot_hidden = true;
  }

  char truncated[NAME_MAX_LEN];
  strncpy(truncated, name, 10);
  truncated[10] = '\0';
  strncpy(s.last_name, truncated, NAME_MAX_LEN);

  if (hp != s.last_hp) {
    if (hp <= 0 && s.last_hp > 0) markDead(idx);
    s.last_hp = hp;
  }
}

void resetAllSlots() {
  for (int i = 0; i < MAX_SLOTS; i++) initSlot(i);
}

// ──────────────────────────────────────────────
//  SERIAL PARSING
// ──────────────────────────────────────────────
char lineBuf[512];
int  linePos = 0;

int findSlot(const char* name) {
  for (int i = 0; i < MAX_SLOTS; i++) if (slots[i].has_name && strcmp(slots[i].assigned_name, name) == 0) return i;
  for (int i = 0; i < MAX_SLOTS; i++) if (!slots[i].has_name) return i;
  return -1;
}

void processLine(const char* text) {
  if (text[0] == '\0') return;

  if (strncmp(text, "RADAR_DISCONNECT", 16) == 0) {
    is_connected = false;
    resetAllSlots();
    bomb_visible = false;
    drawWaitingScreen();
    return;
  }

  if (text[0] != 'p') return;

  char work[512];
  strncpy(work, text, sizeof(work) - 1);
  work[sizeof(work) - 1] = '\0';

  char* parts[32];
  int   nparts = 0;
  char* p = strtok(work, ";");
  while (p && nparts < 32) {
    parts[nparts++] = p;
    p = strtok(NULL, ";");
  }

  if (nparts < 1) return;

  char player_buf[128];
  strncpy(player_buf, parts[0], sizeof(player_buf) - 1);
  player_buf[sizeof(player_buf) - 1] = '\0';

  char* tok = strtok(player_buf, ",");
  char* spx = strtok(NULL, ",");
  char* spy = strtok(NULL, ",");
  char* sang = strtok(NULL, ",");

  if (!spx || !spy || !sang) return;

  float px  = atof(spx);
  float py  = atof(spy);
  float ang = atof(sang);

  if (!is_connected) {
    is_connected = true;
    needs_refresh = true;
  }

  char seen_names[10][NAME_MAX_LEN];
  int  seen_count = 0;

  char work2[512];
  strncpy(work2, text, sizeof(work2) - 1);
  work2[sizeof(work2) - 1] = '\0';

  char* chunks[32];
  int   nchunks = 0;
  char* c = work2;
  chunks[nchunks++] = c;
  while (*c) {
    if (*c == ';') {
      *c = '\0';
      c++;
      if (*c) chunks[nchunks++] = c;
    } else {
      c++;
    }
    if (nchunks >= 32) break;
  }

  bomb_visible = false;

  for (int i = 1; i < nchunks; i++) {
    char chunkBuf[128];
    strncpy(chunkBuf, chunks[i], sizeof(chunkBuf) - 1);
    chunkBuf[sizeof(chunkBuf) - 1] = '\0';

    if (chunkBuf[0] == 'e') {
      char* fields[6];
      int nf = 0;
      char* f = chunkBuf;
      fields[nf++] = f;
      while (*f && nf < 6) {
        if (*f == ',') {
          *f = '\0';
          f++;
          fields[nf++] = f;
        } else {
          f++;
        }
      }

      if (nf >= 5) {
        float ex = atof(fields[1]);
        float ey = atof(fields[2]);
        int   hp = atoi(fields[3]);
        char* name = fields[4];

        if (seen_count < 10) {
          strncpy(seen_names[seen_count], name, NAME_MAX_LEN - 1);
          seen_names[seen_count][NAME_MAX_LEN - 1] = '\0';
          seen_count++;
        }

        float rad = ang * (M_PI / 180.0f);
        float si  = sinf(rad);
        float co  = cosf(rad);
        float dx  = ex - px;
        float dy  = ey - py;
        float fwd = dx * co + dy * si;
        float rgt = dx * si - dy * co;
        int dot_sx = (int)(RADAR_CX + (rgt * scale));
        int dot_sy = (int)(RADAR_CY - (fwd * scale));

        int slot = findSlot(name);
        if (slot >= 0) {
          updateSlot(slot, dot_sx, dot_sy, hp, name);
          needs_refresh = true;
        }
      }

    } else if (chunkBuf[0] == 'b') {
      char* fields[3];
      int nf = 0;
      char* f = chunkBuf;
      fields[nf++] = f;
      while (*f && nf < 3) {
        if (*f == ',') {
          *f = '\0';
          f++;
          fields[nf++] = f;
        } else {
          f++;
        }
      }

      if (nf >= 2) {
        int site_idx = atoi(fields[1]);
        bomb_site_char = (site_idx == 1) ? 'B' : 'A';
        bomb_visible = true;
        needs_refresh = true;
      }
    }
  }

  for (int s = 0; s < MAX_SLOTS; s++) {
    if (slots[s].has_name && !slots[s].is_dead) {
      bool found = false;
      for (int n = 0; n < seen_count; n++) {
        if (strcmp(slots[s].assigned_name, seen_names[n]) == 0) {
          found = true;
          break;
        }
      }
      if (!found) {
        markDead(s);
        needs_refresh = true;
      }
    }
  }
}

// ──────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
  spr.setColorDepth(16);

  for (int i = 0; i < MAX_SLOTS; i++) initSlot(i);

  drawWaitingScreen();
}

// ──────────────────────────────────────────────
//  LOOP
// ──────────────────────────────────────────────
void loop() {
  // ── BEACON: broadcast RADAR_READY every 500ms when not connected ──
  static unsigned long lastBeacon = 0;
  if (!is_connected) {
    unsigned long now = millis();
    if (now - lastBeacon >= 500) {
      Serial.println("RADAR_READY");
      Serial.flush();
      lastBeacon = now;
    }
  }

  // ── SERIAL PROCESSING ──
  while (Serial.available()) {
    if (Serial.available() > 150) {
      while (Serial.available()) Serial.read();
      linePos = 0;
      continue;
    }

    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (linePos > 0) {
        lineBuf[linePos] = '\0';
        if (strcmp(lineBuf, "RADAR_INIT") == 0) {
          Serial.println("RADAR_ACK");
          Serial.flush();
        } else {
          processLine(lineBuf);
        }
        linePos = 0;
      }
    } else {
      if (linePos < (int)sizeof(lineBuf) - 1) {
        lineBuf[linePos++] = c;
      }
    }
  }

  // ── RENDER at 60fps when connected ──
  static unsigned long lastRefresh = 0;
  unsigned long now = millis();
  if (now - lastRefresh >= 16) {
    if (is_connected) {
      drawRadarFrame();
    }
    lastRefresh = now;
  }
}
