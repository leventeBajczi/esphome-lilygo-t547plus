#include "t547.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#ifdef USE_ESP32_FRAMEWORK_ARDUINO

#include <esp32-hal-gpio.h>

namespace esphome {
namespace t547 {

static const char *const TAG = "t574";

void T547::setup() {
  ESP_LOGV(TAG, "Initialize called");
  epd_init();
  uint32_t buffer_size = this->get_buffer_length_();

  if (this->buffer_ != nullptr) {
    free(this->buffer_);  // NOLINT
  }

  this->buffer_ = (uint8_t *) ps_malloc(buffer_size);
  this->last_buffer = (uint8_t *) ps_malloc(buffer_size);

  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate buffer for display!");
    this->mark_failed();
    return;
  }
  if (this->last_buffer == nullptr) {
    ESP_LOGE(TAG, "Could not allocate buffer for last display!");
    this->mark_failed();
    return;
  }


  memset(this->buffer_, 0xFF, buffer_size);
  memset(this->last_buffer, 0xFF, buffer_size);
  ESP_LOGV(TAG, "Initialize complete");
}

float T547::get_setup_priority() const { return setup_priority::PROCESSOR; }
size_t T547::get_buffer_length_() {
    return this->get_width_internal() * this->get_height_internal() / 2;
}

void T547::update() {
  this->do_update_();
  this->display();
}

void HOT T547::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= this->get_width_internal() || y >= this->get_height_internal() || x < 0 || y < 0)
    return;
  uint8_t gs = 255 - ((color.red * 2126 / 10000) + (color.green * 7152 / 10000) + (color.blue * 722 / 10000));
  epd_draw_pixel(x, y, gs, this->buffer_);
}

void T547::dump_config() {
  LOG_DISPLAY("", "T547", this);
  LOG_UPDATE_INTERVAL(this);
}

void T547::eink_off_() {
  ESP_LOGV(TAG, "Eink off called");
  if (panel_on_ == 0)
    return;
  epd_poweroff();
  panel_on_ = 0;
}

void T547::eink_on_() {
  ESP_LOGV(TAG, "Eink on called");
  if (panel_on_ == 1)
    return;
  epd_poweron();
  panel_on_ = 1;
}

int max(int i, int j) { return i < j ? j : i; }
int min(int i, int j) { return i < j ? i : j; }

Rect_t T547::get_dirty_area() {
  for(int x = 0; x < get_width_internal(); x++) {
      
      for(int y = 0; y < get_height_internal(); y++) {
          
          uint8_t *old_buf_ptr = &this->last_buffer[y * get_width_internal() / 2 + x / 2];
          uint8_t *new_buf_ptr = &this->buffer_[y * get_width_internal() / 2 + x / 2];
          
          bool matches = false;
          
          if( x % 2 ) {
              if((*old_buf_ptr & 0xF0) == (*new_buf_ptr & 0xF0)) matches = true;
          } else {
              if((*old_buf_ptr & 0x0F) == (*new_buf_ptr & 0x0F)) matches = true;
          }
          
          if(!matches) {
              if (x < this->dirty_x1) {
                  this->dirty_x1 = x;
              }
              if (x > this->dirty_x2) {
                  this->dirty_x2 = x;
              }
              if (y < this->dirty_y1) {
                  this->dirty_y1 = y;
              }
              if (y > this->dirty_y2) {
                  this->dirty_y2 = y;
              }
          }
          
      }
  }
  
  this->dirty_x1 = max(this->dirty_x1-10, 0);
  this->dirty_y1 = max(this->dirty_y1-10, 0);
  this->dirty_x2 = min(this->dirty_x2+10, get_width_internal());
  this->dirty_y2 = min(this->dirty_y2+10, get_height_internal());
    
  Rect_t area = {.x = this->dirty_x1, .y = this->dirty_y1, .width = this->dirty_x2 - this->dirty_x1 + 1, .height = this->dirty_y2 - this->dirty_y1 + 1};
  ESP_LOGD(TAG, "Dirty area found (%d, %d, %d, %d)", dirty_x1, dirty_y1, dirty_x2, dirty_y2);
  return area;
}

void T547::extract_dirty_fb(Rect_t area, uint8_t* target) {
    for(int x = 0; x < area.width; x++) {
      for(int y = 0; y < area.height; y++) {
          
          uint8_t *old_buf_ptr = &this->buffer_[(y + area.y) * get_width_internal() / 2 + (x + area.x) / 2];
          uint8_t *new_buf_ptr = &target[y * area.width / 2 + x / 2];
          
          uint8_t old_color = ( (x + area.x) % 2 ) ? (*old_buf_ptr & 0xF0) >> 4 : *old_buf_ptr & 0x0F;
          
          if( x % 2 ) {
              *new_buf_ptr = *new_buf_ptr & 0x0F | old_color << 4;
          } else {
              *new_buf_ptr = *new_buf_ptr & 0xF0 | old_color;
          }
          
      }
    }
}

void T547::display() {
  ESP_LOGV(TAG, "Display called");
  uint32_t start_time = millis();

  epd_poweron();
  if(this->reload_cnt % 10 == 0) {
    epd_clear_area_cycles(epd_full_screen(), 1, 10);
    epd_draw_grayscale_image(epd_full_screen(), this->buffer_);
    this->reload_cnt = 0;
  }
  else {
    Rect_t dirty_area = this->get_dirty_area();
    if(dirty_area.width > 0 && dirty_area.height > 0) {
        epd_clear_area_cycles(dirty_area, 1, 10);
        uint8_t* dirty_fb = (uint8_t *) ps_malloc((dirty_area.width + 1)/2 * dirty_area.height);
        extract_dirty_fb(dirty_area, dirty_fb);
        epd_draw_grayscale_image(dirty_area, dirty_fb);
        free(dirty_fb);
    }
  }
  memcpy(this->last_buffer, this->buffer_, this->get_buffer_length_());
  this->dirty_x1 = get_width_internal();
  this->dirty_x2 = 0;
  this->dirty_y1 = get_height_internal();
  this->dirty_y2 = 0;
  this->reload_cnt++;
  epd_poweroff();

  ESP_LOGV(TAG, "Display finished (full) (%ums)", millis() - start_time);
}

}  // namespace T547
}  // namespace esphome

#endif  // USE_ESP32_FRAMEWORK_ARDUINO
