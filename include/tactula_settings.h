#ifndef TACTULA_SETTINGS_H
#define TACTULA_SETTINGS_H

#include <Arduino.h>
#include "config.h"

// Night brightness schedule. Versioned NVS blob "backlight"; `brightness` is
// the day level and lives in its own key.
struct BacklightSettings {
  uint8_t  version;
  uint8_t  nightEnabled;
  uint8_t  nightBrightness;
  uint8_t  reserved0;            // StatNode's offline-off flag, unused
  uint16_t nightStartMinute;     // minute of day, 0..1439
  uint16_t nightEndMinute;       // minute of day, 0..1439
  uint16_t reserved1;            // StatNode's offline sleep delay, unused
};

// Filament / status LED on a PWM pin. Versioned NVS blob "led"; see led.cpp.
struct LedSettings {
  uint8_t version;
  uint8_t enabled;
  uint8_t pin;
  uint8_t brightness;       // 0..255, the normal daytime level
  uint8_t nightEnabled;     // use nightBrightness inside the LED's own night interval
  uint8_t nightBrightness;  // 0..255, 0 switches the LED fully off at night
  uint8_t followDisplay;    // dark whenever the panel itself is dark
  uint16_t nightStartMinute;  // minute of day, 0..1439; v2, v1 shared the backlight's
  uint16_t nightEndMinute;    // minute of day, 0..1439
};

// Panel and PC metric tile settings, each its own NVS scalar.
constexpr uint8_t DAILY_MAX_COUNT = 32;

// Unlisted wallpaper feed token; configured in the web portal, never a full URL.
inline constexpr char DEFAULT_DAILY_FEED_BASE[] = "https://firmware.mrsleeps.uk/api/v1/feeds";
inline constexpr size_t DAILY_FEED_BASE_MAX = 192;
inline constexpr uint8_t WALL_SOURCE_FEED = 3;
extern String dailyFeedId;
extern String dailyFeedBase;
bool normalizeDailyFeedBase(String& value);
bool validDailyFeedId(const String& value);

struct DisplaySettings {
  uint8_t  rotation;          // 0..3 (x90 degrees)
  uint16_t tempScaleMax;      // C, default full scale of temperature tiles
  uint16_t powerScaleW;       // W, default full scale of power tiles
  uint8_t  warnThresholdPct;  // metric tiles turn amber at >= this % of scale; 0 = off
  uint8_t  theme;             // THEME_* display preset
  uint8_t  tactulaRadius = 22; // tile corner radius in pixels
  uint8_t  tactulaPadding = 9; // preferred inner padding in pixels
  uint8_t  glassTint;         // Glass: tile tint opacity %, 0..100
  uint8_t  glassBlur;         // Glass: blur radius px, 0..24 (0 = clear glass)
  uint8_t  glassDim;          // Glass: wallpaper black overlay %, 0..100 (sharp + blurred)
  uint8_t  glassOn;           // Glass: accent fill of an "on" tile, opacity %, 20..100
  uint8_t  wallEvery;         // Wallpaper interval in minutes, 0 = off
  uint8_t  wallOrder;         // 0 = in order, 1 = shuffle
  bool     wallDaily;         // Daily downloads enabled
  uint8_t  dailyMirror;       // 0 = Microsoft direct, 1 = Tactula mirror
  uint8_t  wallSource;        // 0 = own, 1 = daily, 2 = both, 3 = Tactula feed
  bool     sdCard;
  uint16_t dailyMax;          // SD archive per orientation: 50, 100, 200 or 500
  uint8_t  dailyHour;         // Daily fetch hour, local time 0..23
  uint8_t  dailyOrient;       // 0 = landscape, 1 = portrait, 2 = both
  uint8_t  dailyCount;        // Daily pictures per orientation, 1..DAILY_MAX_COUNT
  bool     glassAutoText;     // Glass: text colour and tint follow the picture per tile
  bool     smoothCharts;      // PC metric charts glide between packets
  uint8_t  tileFx;            // Tile animations: 0 classic, 1 calm, 2 water, 3 glass, 4 heavy, 5 custom
  // Custom (tileFx 5): one effect per moment, numbered as in the web selects.
  uint8_t  fxTouch, fxToggle, fxPage, fxCard;
};

// Screen sleep (portal Screen page); NVS scalars. Present = an entity
// "on"/"home" or any paired PC online.
constexpr size_t MAX_PRESENCE_ENTITIES = 4;
struct PresenceSettings {
  uint16_t sleepMin = 0;  // 0 = never sleeps
  uint8_t dimPct = 0;     // asleep: 0 = backlight off, else at most this % of full
  bool frame = false;
  uint8_t frameEvery = 5;
  bool frameClock = true;
  bool frameClockLarge = false;
  bool frameWeatherIcon = true;
  String frameWeatherEntity; // empty = dashboard clock source
  uint8_t frameWeatherTemperature = 0; // 0 inherit (actual for explicit source), 1 actual, 2 feels-like
  bool frameClockBackground = true;
  bool frameAnim = true;
  bool mediaKeepAwake = false;
  bool nightWake = false;        // presence lifts the night schedule to nightWakeLevel
  uint8_t nightWakeLevel = 128;  // 0..255
  String entities[MAX_PRESENCE_ENTITIES];  // "" = unused slot
};

// Vibration motor module on a GPIO (portal Hardware page); NVS scalars.
struct HapticSettings {
  bool enabled = false;
  uint8_t pin = 0;        // 0 = not chosen yet
  uint8_t strength = 100; // PWM duty %, HAPTIC_MIN_STRENGTH..100
  uint8_t pulseMs = 40;   // HAPTIC_MIN_MS..HAPTIC_MAX_MS
};
constexpr uint8_t HAPTIC_MIN_STRENGTH = 20;
constexpr uint8_t HAPTIC_MIN_MS = 10;
constexpr uint8_t HAPTIC_MAX_MS = 200;

// NS4168 speaker (portal Hardware page); NVS scalars. A file name is a WAV in
// audioDir(), "" = the built-in sound.
constexpr size_t AUDIO_FILE_LEN = 40;
struct AudioSettings {
  bool enabled = false;
  uint8_t volume = 70;     // 0..100
  bool nightMute = true;   // silent while the screen's night schedule is active
  bool click = false;      // tile taps
};

constexpr uint8_t THEME_MIDNIGHT = 0;
constexpr uint8_t THEME_DAYLIGHT = 1;
constexpr uint8_t THEME_GLASS = 2;  // Midnight colours, glass tiles over a wallpaper
constexpr uint8_t THEME_TACTULA = 3;
// Retired display theme IDs 4 and 5 are reserved for older settings and backups.
// Modern Tactula: the persisted byte encodes palette and independent Glass mode.
constexpr uint8_t THEME_CINEMATIC = 6; // existing Ocean ID
constexpr uint8_t THEME_EMBER = 7;
constexpr uint8_t THEME_OCEAN_GLASS = 8;
constexpr uint8_t THEME_EMBER_GLASS = 9;
constexpr uint8_t THEME_AMETHYST = 10;
constexpr uint8_t THEME_FOREST = 11;
constexpr uint8_t THEME_GRAPHITE = 12;
constexpr uint8_t THEME_AMETHYST_GLASS = 13;
constexpr uint8_t THEME_FOREST_GLASS = 14;
constexpr uint8_t THEME_GRAPHITE_GLASS = 15;
#if defined(BOARD_IS_JC8012P4A1C)
constexpr bool CINEMATIC_THEME_AVAILABLE = true;
#else
constexpr bool CINEMATIC_THEME_AVAILABLE = false;
#endif

constexpr bool themeUsesModernLayout(int theme) {
  return theme >= THEME_CINEMATIC && theme <= THEME_GRAPHITE_GLASS;
}
constexpr bool themeUsesGlassMode(int theme) {
  return theme == THEME_OCEAN_GLASS || theme == THEME_EMBER_GLASS ||
         (theme >= THEME_AMETHYST_GLASS && theme <= THEME_GRAPHITE_GLASS);
}
constexpr bool themeUsesEmber(int theme) {
  return theme == THEME_EMBER || theme == THEME_EMBER_GLASS;
}
constexpr bool themeUsesAmethyst(int theme) {
  return theme == THEME_AMETHYST || theme == THEME_AMETHYST_GLASS;
}
constexpr bool themeUsesForest(int theme) {
  return theme == THEME_FOREST || theme == THEME_FOREST_GLASS;
}
constexpr bool themeUsesGraphite(int theme) {
  return theme == THEME_GRAPHITE || theme == THEME_GRAPHITE_GLASS;
}
constexpr uint8_t normalizeDisplayTheme(int theme) {
  // Both boards migrate retired themes; keep legacy IDs for portable backups.
  if (theme == THEME_MIDNIGHT || theme == THEME_DAYLIGHT || theme == THEME_TACTULA) return THEME_CINEMATIC;
  if (theme == THEME_GLASS) return THEME_OCEAN_GLASS;
  if (themeUsesModernLayout(theme)) return static_cast<uint8_t>(theme);
  return THEME_OCEAN_GLASS;
}

constexpr uint8_t TACTULA_RADIUS_MAX = 32;
constexpr uint8_t TACTULA_PADDING_MAX = 20;

constexpr bool themeUsesWallpaper(uint8_t theme) {
  return theme == THEME_GLASS || theme == THEME_TACTULA || themeUsesModernLayout(theme);
}

// Network settings. NVS blob "net", layout shared with StatNode.
struct NetworkSettings {
  bool useDHCP;           // true = DHCP, false = static
  char staticIP[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  bool showIPAtStartup;   // show the IP screen for a moment after WiFi connects
  uint8_t timezoneIndex;  // unused
  char timezoneStr[64];   // POSIX TZ string
  bool use24h;            // top bar clock: 24h (default) or 12h
  uint8_t dateFormat;     // unused
  bool mdnsEnabled;       // advertise <hostname>.local over mDNS
  char hostname[32];      // sanitized [a-z0-9-], default "tactula-<MAC4>"
};

extern char wifiSSID[33];
extern char wifiPass[65];
extern uint8_t brightness;
extern DisplaySettings dispSettings;
extern NetworkSettings netSettings;
extern BacklightSettings backlightSettings;
extern LedSettings ledSettings;
extern PresenceSettings presenceSettings;
extern HapticSettings hapticSettings;
extern AudioSettings audioSettings;

bool validDailyMax(int count);
// Migrate beta settings that stored Tactula feed as a Spotlight download source.
void normalizeWallpaperSource(DisplaySettings& display);
bool validWallEvery(int minutes);
bool validFrameEvery(int minutes);
void loadSettings();
void saveSettings();
bool factoryResetSettings();
void defaultDisplaySettings(DisplaySettings& ds);
void defaultNetworkSettings(NetworkSettings& ns);
void defaultBacklightSettings(BacklightSettings& bs);
void defaultLedSettings(LedSettings& ls);

// Normalize a string into a valid mDNS/DNS label: lowercase, keep only
// [a-z0-9-], strip leading/trailing hyphens; ensure the name starts with
// "tactula" (e.g. "office" -> "tactula-office").
void sanitizeHostname(const char* in, char* out, size_t outSize);

#endif // SETTINGS_H
