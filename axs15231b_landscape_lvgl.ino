/*
 * AXS15231B 3.5" QSPI panel in LANDSCAPE (480x320) with LVGL v8
 * ------------------------------------------------------------------
 * The AXS15231B panel (e.g. Guition JC3248W535) is natively PORTRAIT
 * (320x480) over a QSPI bus. The common Arduino_GFX / driver builds do
 * NOT expose a MADCTL landscape mode for it, so hardware rotation is
 * not available. This sketch runs the UI in landscape 480x320 by doing
 * the rotation entirely in SOFTWARE inside the LVGL flush callback.
 *
 * Libraries:
 *   - Arduino_GFX (moononournation)  -> Arduino_ESP32QSPI + Arduino_AXS15231B + Arduino_Canvas
 *   - LVGL v8.x
 *
 * Board: ESP32-S3 (with PSRAM). FQBN example:
 *   esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app
 *
 * Tested on real hardware (ESP32-S3 + AXS15231B 3.5" 320x480 QSPI panel).
 */

#include <Arduino_GFX_Library.h>
#include <lvgl.h>

/* ====== Display wiring (QSPI) ======
 * Arduino_ESP32QSPI(cs, sck, d0, d1, d2, d3)
 * Adjust these pins to match your board.
 */
#define GFX_BL   1     // backlight pin (PWM capable)
#define QSPI_CS  45
#define QSPI_SCK 47
#define QSPI_D0  21
#define QSPI_D1  48
#define QSPI_D2  40
#define QSPI_D3  39

/* Native panel geometry (portrait) */
#define PANEL_W  320
#define PANEL_H  480

/* Landscape (LVGL) geometry */
#define LV_HOR   480
#define LV_VER   320

/* ====== Optional touch (AXS15231B capacitive controller over I2C) ======
 * Set HAS_TOUCH to 0 if you only want to test the display output.
 */
#define HAS_TOUCH 1
#define TOUCH_SDA  4
#define TOUCH_SCL  8
#define TOUCH_RST  12
#define TOUCH_ADDR 0x3B

#if HAS_TOUCH
#include <Wire.h>
#endif

/* ====== Arduino_GFX objects ======
 * We render into an off-screen Canvas (portrait 320x480), then the panel
 * driver pushes it out. LVGL never talks to the panel directly; it only
 * writes rotated pixels into the Canvas via drawPixel() in the flush cb.
 */
Arduino_DataBus  *bus = new Arduino_ESP32QSPI(QSPI_CS, QSPI_SCK, QSPI_D0, QSPI_D1, QSPI_D2, QSPI_D3);
Arduino_AXS15231B *g  = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, PANEL_W, PANEL_H);
Arduino_Canvas   *gfx = new Arduino_Canvas(PANEL_W, PANEL_H, g, 0, 0, 0);

/* ====== LVGL display plumbing ====== */
static lv_disp_draw_buf_t draw_buf;
static lv_color_t        *buf1;
static lv_disp_t         *disp;

/*
 * SOFTWARE ROTATION happens here.
 *
 * LVGL thinks the screen is 480x320 (landscape). The physical panel is
 * 320x480 (portrait). We map each landscape pixel (lx, ly) to the panel:
 *
 *     panel_x = ly
 *     panel_y = (PANEL_H - 1) - lx
 *
 * i.e. a 90-degree rotation. Change the mapping if you need the other
 * orientation (see README for the alternative formula).
 */
void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  uint16_t *px = (uint16_t *)color_p;

  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      int32_t lx = area->x1 + x;   // landscape X (0..479)
      int32_t ly = area->y1 + y;   // landscape Y (0..319)
      gfx->drawPixel(ly, (PANEL_H - 1) - lx, px[y * w + x]);
    }
  }
  gfx->flush();
  lv_disp_flush_ready(drv);
}

/* ====== Touch ====== */
#if HAS_TOUCH
static int16_t last_tx = 0, last_ty = 0;

void touch_reset() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(200);
  digitalWrite(TOUCH_RST, HIGH); delay(200);
}

/*
 * Reads the raw touch point from the AXS15231B touch controller and
 * returns it already mapped into LVGL landscape coordinates.
 *
 * NOTE: The affine constants below convert this specific panel's raw
 * readings into 480x320 landscape space. They were derived by a 9-point
 * calibration on real hardware and WILL differ for your unit -- treat
 * them as a starting point and re-calibrate if the touch is offset.
 */
bool read_touch(int16_t &lx, int16_t &ly) {
  uint8_t cmd[8] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 0x08};
  Wire.beginTransmission(TOUCH_ADDR);
  for (int i = 0; i < 8; i++) Wire.write(cmd[i]);
  Wire.endTransmission(false);

  uint8_t d[8] = {0};
  Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)8);
  uint8_t i = 0;
  while (Wire.available() && i < 8) d[i++] = Wire.read();
  if (i < 8 || d[1] != 0x01) return false;

  int16_t rx = d[3];
  int16_t ry = ((d[4] & 0x0F) << 8) | d[5];
  if (ry > (PANEL_H - 1)) return false;

  // 9-point affine calibration -> LVGL landscape coordinates
  float X = 0.1234f * rx - 0.9284f * ry + 463.2f;
  float Y = 1.0831f * rx + 0.0047f * ry - 8.9f;
  lx = (int16_t)constrain((int)(X + 0.5f), 0, LV_HOR - 1);
  ly = (int16_t)constrain((int)(Y + 0.5f), 0, LV_VER - 1);
  return true;
}

void my_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  int16_t lx, ly;
  if (read_touch(lx, ly)) {
    last_tx = lx;
    last_ty = ly;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  data->point.x = last_tx;
  data->point.y = last_ty;
}
#endif  // HAS_TOUCH

/* ====== Demo UI: a label + a button that counts clicks ====== */
static lv_obj_t *count_label;
static int       click_count = 0;

static void btn_event_cb(lv_event_t *e) {
  click_count++;
  lv_label_set_text_fmt(count_label, "Clicks: %d", click_count);
}

static void build_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "AXS15231B Landscape 480x320");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 200, 70);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_lbl = lv_label_create(btn);
  lv_label_set_text(btn_lbl, "Tap me");
  lv_obj_center(btn_lbl);

  count_label = lv_label_create(scr);
  lv_label_set_text(count_label, "Clicks: 0");
  lv_obj_set_style_text_color(count_label, lv_color_hex(0x4CC2FF), 0);
  lv_obj_align(count_label, LV_ALIGN_BOTTOM_MID, 0, -20);
}

/* ====== Setup / Loop ====== */
void setup() {
  Serial.begin(115200);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);   // backlight ON

  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();

#if HAS_TOUCH
  touch_reset();
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);
#endif

  // Full-frame draw buffer in PSRAM (falls back to a partial buffer if
  // PSRAM allocation fails). Requires a board configured with PSRAM.
  buf1 = (lv_color_t *)ps_malloc((size_t)LV_HOR * LV_VER * sizeof(lv_color_t));
  size_t buf_px = (size_t)LV_HOR * LV_VER;
  if (!buf1) {
    buf1   = (lv_color_t *)ps_malloc((size_t)LV_HOR * 40 * sizeof(lv_color_t));
    buf_px = (size_t)LV_HOR * 40;
  }

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, buf_px);

  static lv_disp_drv_t dd;
  lv_disp_drv_init(&dd);
  dd.hor_res  = LV_HOR;   // 480 -- landscape
  dd.ver_res  = LV_VER;   // 320
  dd.flush_cb = my_disp_flush;
  dd.draw_buf = &draw_buf;
  disp = lv_disp_drv_register(&dd);

#if HAS_TOUCH
  static lv_indev_drv_t id;
  lv_indev_drv_init(&id);
  id.type    = LV_INDEV_TYPE_POINTER;
  id.read_cb = my_touch_read;
  id.disp    = disp;
  lv_indev_drv_register(&id);
#endif

  build_ui();
}

void loop() {
  lv_timer_handler();
  delay(5);
}
