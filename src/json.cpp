#include "json.h"
#include "settings_things.h"

DynamicJsonDocument main_json_doc(capacity);
DynamicJsonDocument mode_json_doc(capacity);

void closeAllOpenFiles(File &root) {
    File file = root.openNextFile();
    while (file) {
        file.close();
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] File closed");
        #endif    /* defined(DEBUG) */
        file = root.openNextFile();
    }
}

void delete_all_files() {
    // Open the root directory
    File root = LITTLEFS.open("/");
    #if defined(DEBUG)
        Serial.println("[" + String(__func__) + "] File opening");
    #endif    /* defined(DEBUG) */
    
    // Check if root directory opened successfully
    if (!root) {
        Serial.println("Failed to open root directory");
        return;
    }

    // Check if it's a directory
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    // Ensure all files are closed
    closeAllOpenFiles(root);

    // Rewind the directory
    root.rewindDirectory();

    // Open the directory again for deleting files
    File file = root.openNextFile();
    while (file) {
        const char* filename = file.path();
        
        // Ensure the filename is valid
        if (filename == nullptr || strlen(filename) == 0) {
            Serial.println("Invalid file name, skipping");
        } else {
            // Print the file name
            Serial.print("Deleting file: ");
            Serial.println(filename);

            // Check if file exists before attempting to delete
            if (LITTLEFS.exists(filename)) {
                // Delete the file
                if (!LITTLEFS.remove(filename)) {
                    Serial.print("Failed to delete file: ");
                    Serial.println(filename);
                } else {
                    Serial.print("Successfully deleted file: ");
                    Serial.println(filename);
                }
            } else {
                Serial.print("File does not exist: ");
                Serial.println(filename);
            }
        }

        // Move to the next file
        file = root.openNextFile();
    }

    // Close the root directory
    root.close();
    //handle_restart();
}

bool json_save_prefs_file(const char* prefs_file) {
    File file = LITTLEFS.open(prefs_file, "w");
    #if defined(DEBUG)
        Serial.println("[" + String(__func__) + "] File opening " + String(prefs_file));
    #endif    /* defined(DEBUG) */
    if (!file) {
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] Failed to open preferences file for writing");
        #endif    /* defined(DEBUG) */
        return false;
    }
    if (strcmp(main_prefs_file, prefs_file) == 0) {
        serializeJson(main_json_doc, file);
    } else {
        serializeJson(mode_json_doc, file);
    }
    
    file.close();
    #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] File closed " + String(prefs_file));
        #endif    /* defined(DEBUG) */
    return true;
}

void json_load_prefs_file(const char* prefs_file) {
    #if defined(DEBUG)
        Serial.println("[" + String(__func__) + "] trying to open " + String(prefs_file));
    #endif    /* defined(DEBUG) */
    File file = LITTLEFS.open(prefs_file, "r");
    #if defined(DEBUG)
        Serial.println("[" + String(__func__) + "] File opening");
    #endif    /* defined(DEBUG) */
    if (!file) {
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] Failed to open preferences file for reading");
        #endif    /* defined(DEBUG) */

        save_default_settings(prefs_file);
        file.close();
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] File closed ERROR");
        #endif    /* defined(DEBUG) */
        return;
    } else {
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] preferences file opened for reading");
        #endif    /* defined(DEBUG) */
    }

    if (strcmp(main_prefs_file, prefs_file) == 0)  {
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] main prefs deserialize");
        #endif    /* defined(DEBUG) */
        deserializeJson(main_json_doc, file);
        serializeJsonPretty(main_json_doc, Serial);
    } else {
        #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] mode prefs deserialize");
        #endif    /* defined(DEBUG) */
        deserializeJson(mode_json_doc, file);
        serializeJsonPretty(mode_json_doc, Serial);
    }

    file.close();
    #if defined(DEBUG)
        Serial.println("[" + String(__func__) + "] File closed END");
    #endif    /* defined(DEBUG) */
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