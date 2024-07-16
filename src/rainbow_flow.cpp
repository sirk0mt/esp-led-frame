
#include "settings_things.h"
#include "network_things.h"
#include "json.h"
#include "all_modes.h"
#include "website.h"

const char* rainbow_flow_prefs_file = "/rainbow_flow.json"; 

uint16_t    rainbow_flow_change_rate;

uint16_t    rainbow_flow_change_degree;

uint16_t    rainbow_flow_gradient_density;

uint16_t    rainbow_flow_master_delay;

String rainbow_flow_html() {
  return
    "<div class='row justify-content-center'>"
      "<div class='col-auto text-center'>"
        + get_num_row_with_save_button("Speed:", "masterSpeed", 0, rainbow_flow_change_rate, "bt-masterSpeed", "setVal(this)", "Set") +
        get_num_row_with_save_button("Degree of animation:", "degree", 0, rainbow_flow_change_degree, "bt-degree", "setVal(this)", "Set") +
        get_num_row_with_save_button("Gradient density:", "grad", 0, rainbow_flow_gradient_density, "bt-grad", "setVal(this)", "Set") +
        get_num_row_with_save_button("Master delay:", "del", 0, rainbow_flow_master_delay, "bt-del", "setVal(this)", "Set") +
      "</div>"
    "</div>"
    "<script>"
      "function setVal(clickedElement) {"
        "switch(clickedElement.id) {"
          "case 'bt-masterSpeed':"
            "var newval = parseInt(document.getElementById('masterSpeed').value);"
            "fetch('/rainbowFlowSet?speed=' + newval, { method: 'GET' })"
              ".then(response => response.text());"
            "break;"
          "case 'bt-degree':"
            "var newval = parseInt(document.getElementById('degree').value);"
            "fetch('/rainbowFlowSet?degree=' + newval, { method: 'GET' })"
              ".then(response => response.text());"
            "break;"
          "case 'bt-grad':"
            "var newval = parseInt(document.getElementById('grad').value);"
            "fetch('/rainbowFlowSet?grad=' + newval, { method: 'GET' })"
              ".then(response => response.text());"
            "break;"
          "case 'bt-del':"
            "var newval = parseInt(document.getElementById('del').value);"
            "fetch('/rainbowFlowSet?del=' + newval, { method: 'GET' })"
              ".then(response => response.text());"
            "break;"
          "default:"
            "break;"
        "}"
      "}"
    "</script>";
}

void rainbow_flow_init_defaults() {
  rainbow_flow_change_rate      = 1;
  mode_json_doc["change_rate"]  = rainbow_flow_change_rate;

  rainbow_flow_change_degree    = 0;
  mode_json_doc["degree"]       = rainbow_flow_change_degree;

  rainbow_flow_gradient_density = 10;
  mode_json_doc["density"]      = rainbow_flow_gradient_density;

  rainbow_flow_master_delay     = 0;
  mode_json_doc["master_del"]   = rainbow_flow_master_delay;
}

void rainbow_flow_init_endpoints() {
  server.on("/rainbowFlowSet", HTTP_GET, []() {
    if (current_mode == MODE_RAINBOW_FLOW) {
      String paramName = server.argName(0); // Get the name of the parameter
      String paramValue = server.arg(0); // Get the value of the parameter
      Serial.println("Change: " + paramName + " to: " + paramValue);
      if (paramValue != "") {
        if (paramName == "speed") {
          rainbow_flow_change_rate = paramValue.toInt();
          mode_json_doc["change_rate"]  = rainbow_flow_change_rate;
          Serial.println("Change rate changed to: " + String(rainbow_flow_change_rate));
        }
        else if (paramName == "degree") {
          rainbow_flow_change_degree = paramValue.toInt();
          mode_json_doc["degree"]       = rainbow_flow_change_degree;
          Serial.println("Change degree changed to: " + String(rainbow_flow_change_degree));
        }
        else if (paramName == "grad") {
          rainbow_flow_gradient_density = paramValue.toInt();
          mode_json_doc["density"]      = rainbow_flow_gradient_density;
          Serial.println("Gradient density changed to: " + String(rainbow_flow_gradient_density));
        }
        else if (paramName == "del") {
          rainbow_flow_master_delay = paramValue.toInt();
          mode_json_doc["master_del"]   = rainbow_flow_master_delay;
          Serial.println("Delay changed to: " + String(rainbow_flow_master_delay));
        }
        server.send(200, "text/plain", "OK");
      } else {
        server.send(404, "text/plain", "Parameter not found");
      }
    }
    else {
      server.send(200, "text/plain", "You're not in rainbow_flow mode!");
    }
  });
  server.on("/rainbowFlowGet", HTTP_GET, [](){
    if (current_mode == MODE_RAINBOW_FLOW) {
      String paramName = server.arg("v");
      if(paramName == "speed") {
        server.send(200, "text/plain", String(rainbow_flow_change_rate));
      }
    }
    else {
      server.send(200, "text/plain", "You're not in rainbow_flow mode!");
    }
  });
}

void rainbow_flow_start() {

}

void rainbow_flow_stop() {

}

/**
 * \brief             Input a value 0 to 255 to get a color value. The colors are a transition r - g - b - back to r.
 * \param[wheel_pos]  wheel_pos: Position of color wheel to get color.
*/
uint32_t wheel(byte wheel_pos) {
  wheel_pos = 255 - wheel_pos;
  if (wheel_pos < 85) {
    return strip.Color(255 - wheel_pos * 3, 0, wheel_pos * 3);
  }
  if (wheel_pos < 170) {
    wheel_pos -= 85;
    return strip.Color(0, wheel_pos * 3, 255 - wheel_pos * 3);
  }
  wheel_pos -= 170;
  return strip.Color(wheel_pos * 3, 255 - wheel_pos * 3, 0);
}

int xy_to_index(int x, int y) {
  return y * pixels_in_row + x;
}

void rainbow_flow() {
  static int curr_del = 0;
  if (curr_del == 0) {
    static int offset = 0;
    float radianDirection = rainbow_flow_change_degree * PI / 180.0;
    float dx = cos(radianDirection);
    float dy = sin(radianDirection);

    for (int x = 0; x < pixels_in_row; x++) {
      for (int y = 0; y < pixels_rows; y++) {
        int pixelIndex = xy_to_index(x, y);
        int colorIndex = (int)((x * dx + y * dy) * rainbow_flow_gradient_density + offset) % 256;
        strip.setPixelColor(pixelIndex, wheel(colorIndex));
      }
    }
    strip.show();
    offset = (offset + rainbow_flow_change_rate) % 256;
    curr_del = rainbow_flow_master_delay;
  } else {
    --curr_del;
  }
  
}
