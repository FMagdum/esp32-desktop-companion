# Third-party code

The ESP32 core and Arduino libraries installed through Boards Manager or Library Manager are listed in the [setup guide](SETUP.md). These additional copies ship with the project so the build configuration can be reproduced.

| Component | Source and notice |
| --- | --- |
| ES8311 codec driver | Waveshare Arduino adaptation of Espressif's Apache-2.0 driver. Source headers and the [Apache license](../arduino-libraries/es8311/LICENSE.txt) are included. |
| ES7210 codec driver | Waveshare Arduino copy of Espressif's Apache-2.0 driver with a Wire transport. See the [ES7210 notice](../Desktop_Companion/src/drivers/es7210/NOTICE.txt). |
| LVGL 9.6.0 configuration | Based on LVGL's config template. See the [LVGL license](../arduino-libraries/LICENSE-LVGL.txt). |
| FreeSans bitmap fonts | Adafruit_GFX font headers with a local include adjustment. See the [font notice](../Desktop_Companion/src/ui/fonts/NOTICE.md). |

If you add a project-wide license, retain the included notices for their respective third-party components.
