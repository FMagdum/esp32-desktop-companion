#include "ConversationStatus.h"
#include <lvgl.h>

namespace {
constexpr int width = 76, height = 28, left = 162, top = 2;
alignas(4) uint16_t buffer[width * height];
Arduino_GFX *output = nullptr;
lv_display_t *display = nullptr;
lv_obj_t *label = nullptr;
bool dirty = true;
const char *previous = nullptr;
uint16_t oldBackground = 0, oldForeground = 0;

void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  output->draw16bitRGBBitmap(left + area->x1, top + area->y1,
                            reinterpret_cast<uint16_t *>(pixels),
                            lv_area_get_width(area), lv_area_get_height(area));
  lv_display_flush_ready(display);
}

lv_color_t color(uint16_t rgb565) {
  return lv_color_make((rgb565 >> 11) * 255 / 31, ((rgb565 >> 5) & 63) * 255 / 63,
                       (rgb565 & 31) * 255 / 31);
}
}

namespace ConversationStatus {
void invalidate() { dirty = true; }

void draw(Arduino_GFX *graphics, uint16_t background, uint16_t foreground, const char *state) {
  if (!display) {
    output = graphics;
    lv_init();
    display = lv_display_create(width, height);
    if (!display) return;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    label = lv_label_create(lv_screen_active());
    lv_obj_center(label);
  }
  if (!dirty && previous && !strcmp(previous, state) && background == oldBackground && foreground == oldForeground) return;
  lv_obj_t *screen = lv_display_get_screen_active(display);
  lv_obj_set_style_bg_color(screen, color(background), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(label, color(foreground), 0);
  lv_label_set_text_static(label, state);
  lv_obj_invalidate(screen);
  lv_refr_now(display);
  previous = state;
  oldBackground = background;
  oldForeground = foreground;
  dirty = false;
}
}

