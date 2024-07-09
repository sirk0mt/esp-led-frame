#ifndef JSON_H
/* If first use this header, define all things */
#define JSON_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LITTLEFS.h>

const size_t capacity = JSON_OBJECT_SIZE(32);

extern DynamicJsonDocument main_json_doc;

extern DynamicJsonDocument mode_json_doc;

void delete_all_files();

bool json_save_prefs_file(const char* prefs_file);

void json_load_prefs_file(const char* prefs_file);

String json_load_string(DynamicJsonDocument json_doc, const char* key);

uint16_t json_load_uint16(DynamicJsonDocument json_doc, const char* key);




#endif      /* ifndef JSON_H */