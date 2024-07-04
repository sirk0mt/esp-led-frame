#include "json.h"

bool json_save_prefs_file(const char* prefs_file) {
    File file = LITTLEFS.open(prefs_file, "w");
    if (!file) {
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] Failed to open preferences file for writing");
        #endif    /* defined(DEBUG) */
        return false;
    }
    if (memcmp(main_prefs_file, prefs_file, sizeof(main_prefs_file))) {
        serializeJson(main_json_doc, file);
    } else {
        serializeJson(mode_json_doc, file);
    }
    
    file.close();
    return true;
}

void json_load_prefs_file(const char* prefs_file) {
    File file = LITTLEFS.open(prefs_file, "r");
    if (!file) {
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] Failed to open preferences file for reading");
        #endif    /* defined(DEBUG) */

        save_default_settings(prefs_file);
        file.close();
        return;
    }

    if (memcmp(main_prefs_file, prefs_file, sizeof(main_prefs_file))) {
        deserializeJson(main_json_doc, file);
    } else {
        deserializeJson(mode_json_doc, file);
    }
    file.close();
}

String json_load_string(DynamicJsonDocument json_doc, const char* key) {
    if (json_doc.containsKey(key)) {
        return json_doc[key];
    }

    #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Unable to load [" +
                                              key + "] from json");
    #endif    /* defined(DEBUG) */

    return ""; 
}

uint16_t json_load_uint16(DynamicJsonDocument json_doc, const char* key) {
    if (json_doc.containsKey(key)) {
        return json_doc[key];
    }

    #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Unable to load [" +
                                              key + "] from json");
    #endif    /* defined(DEBUG) */


    return 0; // Or handle default value or error
}