#include <Arduino.h>
#include "settings_things.h"
#include "pixels.h"

pixel_color* pixels = new pixel_color[1];

void strip_initialize() {
  strip.updateLength(num_of_pixels);
  strip.begin();
  strip.clear();
  strip.show();

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Initialized LED strip with " + String(num_of_pixels) + " pixels");
  #endif    /* defined(DEBUG) */
}

void resize_pixels_struct(uint16_t new_size) {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Changing pixel table from " + String(sizeof(pixels)) + " to " + String(new_size));
  #endif    /* defined(DEBUG) */

  pixel_color* new_array = new pixel_color[new_size];
  delete[] pixels;
  pixels = new_array;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Success changed to new size: "+ String(sizeof(pixels)));
  #endif    /* defined(DEBUG) */
}

uint16_t* resize_array(uint16_t* old_array, uint16_t old_size, uint16_t new_size) {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Resizing array from " + String(old_size) + " to " + String(new_size));
  #endif    /* defined(DEBUG) */

  uint16_t* new_array = new uint16_t[new_size];
  uint16_t  min_size  = (old_size < new_size) ? old_size : new_size;         /* size of smaller array */
  
  for (int i = 0; i < min_size; ++i) {
    new_array[i] = old_array[i];
  }
  delete[] old_array;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Success changed to new size");
  #endif    /* defined(DEBUG) */

  return new_array;
}

void send_pixel(uint16_t led_to_send) {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Sending " + String(led_to_send) + " pixel to LED strip");
  #endif    /* defined(DEBUG) */

  strip.setPixelColor(led_to_send, strip.Color(pixels[led_to_send].green, pixels[led_to_send].red, pixels[led_to_send].blue));
  strip.show();

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] pixel sended");
  #endif    /* defined(DEBUG) */
}

void set_pixel_color(uint16_t led, uint8_t r, uint8_t g, uint8_t b) {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Set " + String(led) + " pixel color R: " + String(r) + " G: " + String(g) + " B: " + String(b));
  #endif    /* defined(DEBUG) */

  pixels[led].red   = r;
  pixels[led].green = g;
  pixels[led].blue  = b;
  strip.setPixelColor(led, strip.Color(pixels[led].green, pixels[led].red, pixels[led].blue));

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] color is set");
  #endif    /* defined(DEBUG) */
}

void show_triangle() {
  for (int c = 0; c <= pixels_in_row; ++c) {
    for (int r = 0; r <= pixels_rows - c - 1; ++r){
      strip.setPixelColor(r * pixels_in_row + c, strip.Color(0, 255, 0));
    }
  }
  strip.show();
  
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] RED triangle was shown");
  #endif    /* defined(DEBUG) */
}