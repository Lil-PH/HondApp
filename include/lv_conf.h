#ifndef LV_CONF_H
#define LV_CONF_H

/* Local configuration for LVGL 8.3.11 on the 320x240 RGB565 display. */
#define LV_COLOR_DEPTH 16
/* LovyanGFX and the ILI9341 canvas use native little-endian RGB565. */
#define LV_COLOR_16_SWAP 0
#define LV_MEM_SIZE (32U * 1024U)
#define LV_USE_LOG 0

/* Native LVGL GIF widget. It decodes files through the filesystem driver
 * registered in vehicle_media.cpp; media remains on the microSD card. */
#define LV_USE_GIF 1

/* Fonts used by src/main.cpp. */
#define LV_FONT_MONTSERRAT_8 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_32 1

#endif /* LV_CONF_H */
