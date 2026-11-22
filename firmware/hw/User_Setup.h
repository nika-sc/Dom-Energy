// ============================================================================
//  User_Setup.h — настройка TFT_eSPI под плату ESP32-2432S028
//  ("Cheap Yellow Display": ESP32 + ILI9341 2.8" 240x320)
// ============================================================================

#define USER_SETUP_INFO "ESP32-2432S028 CYD"

// ---------------------------------------------------------------- драйвер
#define ILI9341_2_DRIVER

// ---------------------------------------------------------------- пины
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// ---------------------------------------------------------------- шрифты
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ---------------------------------------------------------------- SPI
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000