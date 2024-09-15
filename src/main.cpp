#include "utils.h"

Panel panel;

void setup()
{
  // Immediately pulls display enable pin low to keep panels from flickering on boot
  pinMode(OE_PIN, OUTPUT);
  digitalWrite(OE_PIN, LOW);
  panel.init();
}

// Just do nothing, eveything is done in tasks
void loop()
{
  vTaskDelete(NULL); // Delete Loop task, we don't need it
}
