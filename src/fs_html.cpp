//
// Espanthella Lamp Matter Controller
// (c) 2026 Karl Pitrich, MIT License
//

#include "fs_html.h"
#include <LittleFS.h>

bool initFileSystem() {
  if (!LittleFS.begin(true)) {
    Serial.println("Filesystem mount failed");
    return false;
  }
  Serial.println("Filesystem mounted");
  return true;
}

String readHtmlFile(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.printf("File not found: %s\n", path);
    return String("<html><body>File not found: ") + path + "</body></html>";
  }
  String content = f.readString();
  f.close();
  return content;
}
