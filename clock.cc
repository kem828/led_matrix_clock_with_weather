#include "led-matrix.h"
#include "graphics.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <time.h>
#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include "lodepng.h"

using rgb_matrix::Canvas;
using rgb_matrix::RGBMatrix;
using rgb_matrix::Font;
using rgb_matrix::Color;

// --- Enums ---
enum class Units { Metric, Imperial };
enum class DisplayMode { Standard, WideHorizontal, WideVertical };

enum class ModuleType {
    WeatherIcon,    // 32x32
    Temperature,    // variable width x 12h
    DayDate,        // variable width x 24h
    WeatherDesc,    // variable width x 12h
    MoonPhase,      // 16x16
    Forecast,       // 32x32
    SunriseSunset   // 16x32
};

struct ModulePlacement {
    ModuleType type;
    int x, y;
};

// --- Clock Font Styles (forward-declared here so ClockConfig can use ClockFont) ---
enum class ClockFont { Classic, Bold, Slim, Tall, Retro };

struct SegParams { int w, h, t, gap; };

ClockFont ParseClockFont(const std::string& s) {
    if (s == "bold")   return ClockFont::Bold;
    if (s == "slim")   return ClockFont::Slim;
    if (s == "tall")   return ClockFont::Tall;
    if (s == "retro")  return ClockFont::Retro;
    return ClockFont::Classic;
}

SegParams GetSegParams(ClockFont font, bool wideOrClock) {
    switch (font) {
    case ClockFont::Classic:
        return wideOrClock ? SegParams{13, 27, 3, 2} : SegParams{10, 18, 2, 1};
    case ClockFont::Bold:
        return wideOrClock ? SegParams{16, 36, 4, 2} : SegParams{11, 19, 3, 1};
    case ClockFont::Slim:
        return wideOrClock ? SegParams{11, 37, 1, 2} : SegParams{ 8, 17, 1, 1};
    case ClockFont::Tall:
        return wideOrClock ? SegParams{ 9, 38, 2, 2} : SegParams{ 7, 20, 2, 1};
    case ClockFont::Retro:
        return wideOrClock ? SegParams{18, 28, 4, 2} : SegParams{14, 19, 3, 1};
    }
    return {10, 18, 2, 1};
}

// --- Constants ---
const int ICON_SIZE = 32;
const int PANEL_WIDTH = 128;
const int PANEL_HEIGHT = 64;
const int WEATHER_RETRY_INTERVAL = 60;   // seconds to retry after failed weather fetch
const int WEATHER_UPDATE_INTERVAL = 900; // seconds between successful weather updates

// 7-Segment digit masks
// Segments: bit0=top, bit1=top-right, bit2=bottom-right, bit3=bottom,
//           bit4=bottom-left, bit5=top-left, bit6=middle
const uint8_t SEG_DIGITS[10] = {
    0x3F, // 0: top, top-right, bottom-right, bottom, bottom-left, top-left
    0x06, // 1: top-right, bottom-right
    0x5B, // 2: top, top-right, middle, bottom-left, bottom
    0x4F, // 3: top, top-right, middle, bottom-right, bottom
    0x66, // 4: top-left, top-right, middle, bottom-right
    0x6D, // 5: top, top-left, middle, bottom-right, bottom
    0x7D, // 6: top, top-left, middle, bottom-left, bottom-right, bottom
    0x07, // 7: top, top-right, bottom-right
    0x7F, // 8: all segments
    0x6F, // 9: top, top-right, top-left, middle, bottom-right, bottom
};

struct Pixel {
    uint8_t r, g, b;
};

// Icon buffers
Pixel sunIcon[ICON_SIZE][ICON_SIZE];
Pixel moonIcon[ICON_SIZE][ICON_SIZE];
Pixel cloudIcon[ICON_SIZE][ICON_SIZE];
Pixel rainIcon[ICON_SIZE][ICON_SIZE];
Pixel snowIcon[ICON_SIZE][ICON_SIZE];
Pixel fogIcon[ICON_SIZE][ICON_SIZE];
Pixel partlyCloudyIcon[ICON_SIZE][ICON_SIZE];
Pixel drizzleIcon[ICON_SIZE][ICON_SIZE];
Pixel thunderIcon[ICON_SIZE][ICON_SIZE];
Pixel friendIcon[ICON_SIZE][ICON_SIZE];
Pixel hazeIcon[ICON_SIZE][ICON_SIZE];
Pixel ashIcon[ICON_SIZE][ICON_SIZE];
Pixel smokeIcon[ICON_SIZE][ICON_SIZE];
Pixel moonCloudIcon[ICON_SIZE][ICON_SIZE];
Pixel moonPartlyCloudIcon[ICON_SIZE][ICON_SIZE];

// --- App Configuration ---
struct ClockConfig {
    std::string api_key;
    std::string lat = "28.5";
    std::string lon = "-81.4";
    Units units = Units::Imperial;
    int dayBrightness = 100;
    int nightBrightness = 30;
    int nightStart = 18;
    int nightEnd = 6;
    bool clockOnly = false;
    ClockFont clockFont = ClockFont::Classic;
    std::vector<ModulePlacement> standardLayout;
    std::vector<ModulePlacement> wideHorizontalLayout;
    std::vector<ModulePlacement> wideVerticalLayout;
};

ModuleType ParseModuleType(const std::string& s) {
    if (s == "weather_icon")    return ModuleType::WeatherIcon;
    if (s == "temperature")     return ModuleType::Temperature;
    if (s == "day_date")        return ModuleType::DayDate;
    if (s == "weather_desc")    return ModuleType::WeatherDesc;
    if (s == "moon_phase")      return ModuleType::MoonPhase;
    if (s == "forecast")        return ModuleType::Forecast;
    if (s == "sunrise_sunset")  return ModuleType::SunriseSunset;
    return ModuleType::WeatherIcon; // fallback
}

std::vector<ModulePlacement> ParseLayout(const nlohmann::json& arr) {
    std::vector<ModulePlacement> layout;
    for (const auto& item : arr) {
        ModulePlacement mp;
        mp.type = ParseModuleType(item.value("type", ""));
        mp.x = item.value("x", 0);
        mp.y = item.value("y", 0);
        layout.push_back(mp);
    }
    return layout;
}

void SetDefaultLayouts(ClockConfig& config) {
    config.standardLayout = {
        {ModuleType::WeatherIcon, 16, 23},
        {ModuleType::Temperature, -1, 62},
        {ModuleType::DayDate, 70, 50},
    };
    config.wideHorizontalLayout = {
        {ModuleType::WeatherIcon, 132, 2},
        {ModuleType::Temperature, 180, 22},
        {ModuleType::WeatherDesc, 132, 42},
        {ModuleType::DayDate, 132, 52},
        {ModuleType::MoonPhase, 220, 2},
        {ModuleType::Forecast, 200, 32},
        {ModuleType::SunriseSunset, 240, 2},
    };
    config.wideVerticalLayout = {
        {ModuleType::WeatherIcon, 16, 66},
        {ModuleType::Temperature, 52, 86},
        {ModuleType::WeatherDesc, 4, 106},
        {ModuleType::DayDate, 70, 114},
        {ModuleType::MoonPhase, 100, 68},
        {ModuleType::Forecast, 0, 96},
        {ModuleType::SunriseSunset, 110, 90},
    };
}

ClockConfig LoadConfig(const std::string& path) {
    ClockConfig config;
    SetDefaultLayouts(config);

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "No config.json found, using defaults\n";
        return config;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(file);

        config.api_key = j.value("api_key", "");
        config.lat = j.value("lat", config.lat);
        config.lon = j.value("lon", config.lon);
        std::string u = j.value("units", "imperial");
        config.units = (u == "metric") ? Units::Metric : Units::Imperial;
        config.dayBrightness = j.value("day_brightness", config.dayBrightness);
        config.nightBrightness = j.value("night_brightness", config.nightBrightness);
        config.nightStart = j.value("night_start", config.nightStart);
        config.nightEnd = j.value("night_end", config.nightEnd);
        config.clockOnly = j.value("clock_only", config.clockOnly);
        config.clockFont = ParseClockFont(j.value("clock_font", "classic"));

        if (j.contains("layouts")) {
            auto& layouts = j["layouts"];
            if (layouts.contains("standard"))
                config.standardLayout = ParseLayout(layouts["standard"]);
            if (layouts.contains("wide_horizontal"))
                config.wideHorizontalLayout = ParseLayout(layouts["wide_horizontal"]);
            if (layouts.contains("wide_vertical"))
                config.wideVerticalLayout = ParseLayout(layouts["wide_vertical"]);
        }

        std::cerr << "Loaded config from " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config.json: " << e.what() << ", using defaults\n";
    }
    return config;
}

struct WeatherData {
    std::string description;
    std::string temp;
};

// --- cURL Helpers ---
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string GetWeather(const std::string& lat, const std::string& lon,
                       const std::string& api_key, Units units) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        std::ostringstream url;
        url << "http://api.openweathermap.org/data/2.5/weather?lat="
            << lat << "&lon=" << lon
            << "&appid=" << api_key
            << "&units=" << (units == Units::Metric ? "metric" : "imperial");

        curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

WeatherData ParseWeather(const std::string& jsonStr, Units units) {
    WeatherData data;
    if (jsonStr.empty()) return {"No data", ""};

    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        int cod_val = j["cod"].is_string() ? std::stoi(j["cod"].get<std::string>())
                                           : j["cod"].get<int>();
        if (cod_val != 200) return {j.value("message", "API error"), ""};

        data.description = j["weather"][0].value("description", "Unknown");
        double temp_val = j["main"].value("temp", 0.0);
        const char* unit_label = (units == Units::Metric) ? "°C" : "°F";

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << temp_val << unit_label;
        data.temp = oss.str();
    } catch (...) {
        data = {"Parse error", ""};
    }
    return data;
}

// --- Icon Loading ---
bool LoadIconFromPNG(const std::string& filename, Pixel icon[ICON_SIZE][ICON_SIZE]) {
    std::vector<unsigned char> image;
    unsigned width, height;

    unsigned error = lodepng::decode(image, width, height, filename);
    if (error) {
        std::cerr << "PNG decode error in " << filename << ": "
                  << lodepng_error_text(error) << std::endl;
        return false;
    }

    for (int y = 0; y < ICON_SIZE; ++y)
        for (int x = 0; x < ICON_SIZE; ++x)
            icon[y][x] = {0, 0, 0};

    for (unsigned y = 0; y < std::min(height, (unsigned)ICON_SIZE); ++y) {
        for (unsigned x = 0; x < std::min(width, (unsigned)ICON_SIZE); ++x) {
            int idx = 4 * (y * width + x);
            uint8_t r = image[idx];
            uint8_t g = image[idx + 1];
            uint8_t b = image[idx + 2];
            uint8_t a = image[idx + 3];
            if (a > 128) icon[y][x] = {r, g, b};
        }
    }
    return true;
}

void ClearIcon(Pixel icon[ICON_SIZE][ICON_SIZE]) {
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++)
            icon[y][x] = {0, 0, 0};
}

// --- Pre-render fallback icons ---
void PreRenderSun() {
    ClearIcon(sunIcon);
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - ICON_SIZE / 2, dy = y - ICON_SIZE / 2;
            if (dx * dx + dy * dy <= 64) sunIcon[y][x] = {0, 255, 255};
        }
}

void PreRenderMoon() {
    ClearIcon(moonIcon);
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - ICON_SIZE / 2, dy = y - ICON_SIZE / 2;
            if (dx * dx + dy * dy <= 64) moonIcon[y][x] = {255, 200, 200};
            int dx2 = x - ICON_SIZE / 2 - 4;
            if (dx2 * dx2 + dy * dy <= 64) moonIcon[y][x] = {0, 0, 0};
        }
}

void PreRenderCloud() {
    ClearIcon(cloudIcon);
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - 16, dy = y - 16;
            if (dx * dx + dy * dy <= 36) cloudIcon[y][x] = {200, 200, 200};
            dx = x - 12; dy = y - 18;
            if (dx * dx + dy * dy <= 36) cloudIcon[y][x] = {200, 200, 200};
            dx = x - 20; dy = y - 18;
            if (dx * dx + dy * dy <= 36) cloudIcon[y][x] = {200, 200, 200};
        }
}

void PreRenderRain() {
    PreRenderCloud();
    for (int i = 0; i < 3; i++) {
        int px = 12 + i * 4;
        rainIcon[22][px] = {255, 128, 0};
        rainIcon[24][px] = {255, 128, 0};
    }
}

void PreRenderSnow() {
    PreRenderCloud();
    for (int i = 0; i < 3; i++) {
        int px = 12 + i * 4;
        snowIcon[22][px] = {255, 255, 255};
        snowIcon[24][px] = {255, 255, 255};
    }
}

void PreRenderPartlyCloudy() {
    ClearIcon(partlyCloudyIcon);
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - 10, dy = y - 10;
            if (dx * dx + dy * dy <= 36) partlyCloudyIcon[y][x] = {255, 255, 0};
        }
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx1 = x - 18, dy1 = y - 18;
            int dx2 = x - 22, dy2 = y - 16;
            if (dx1 * dx1 + dy1 * dy1 <= 36 || dx2 * dx2 + dy2 * dy2 <= 36)
                partlyCloudyIcon[y][x] = {200, 200, 200};
        }
}

void PreRenderFog() {
    ClearIcon(fogIcon);
    for (int y = 12; y <= 20; y += 4)
        for (int x = 8; x < 24; x++)
            fogIcon[y][x] = {180, 180, 180};
}

// --- Drawing Helpers ---
void DrawIcon(rgb_matrix::FrameCanvas* canvas, int x, int y, Pixel icon[ICON_SIZE][ICON_SIZE]) {
    if (!canvas) return;
    for (int row = 0; row < ICON_SIZE; row++)
        for (int col = 0; col < ICON_SIZE; col++) {
            Pixel p = icon[row][col];
            if (p.r || p.g || p.b)
                canvas->SetPixel(x + col, y + row, p.r, p.g, p.b);
        }
}

void DrawBorder(rgb_matrix::FrameCanvas* canvas, Color color) {
    int width = canvas->width();
    int height = canvas->height();

    for (int x = 0; x < width; ++x) {
        canvas->SetPixel(x, 0, color.r, color.g, color.b);
        canvas->SetPixel(x, height - 1, color.r, color.g, color.b);
    }
    for (int y = 0; y < height; ++y) {
        canvas->SetPixel(0, y, color.r, color.g, color.b);
        canvas->SetPixel(width - 1, y, color.r, color.g, color.b);
    }
}

void DrawTextOutline(rgb_matrix::FrameCanvas* canvas, const rgb_matrix::Font& font, int x, int y,
                     const rgb_matrix::Color& outline_color, const rgb_matrix::Color& text_color,
                     const char* text) {
    rgb_matrix::DrawText(canvas, font, x - 1, y, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x + 1, y, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x, y - 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x, y + 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x - 1, y - 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x + 1, y + 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x - 1, y + 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x + 1, y - 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x, y, text_color, nullptr, text);
}

int MeasureTextWidth(const rgb_matrix::Font& font, const std::string& text) {
    int width = 0;
    for (char c : text) width += font.CharacterWidth(c);
    return width;
}

// Clear a rectangular region to black without touching the rest of the canvas.
void ClearRegion(rgb_matrix::FrameCanvas* canvas, int x, int y, int w, int h) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            canvas->SetPixel(x + dx, y + dy, 0, 0, 0);
}

void DrawFilledRoundedBox(rgb_matrix::FrameCanvas* canvas,
                          int x, int y, int w, int h,
                          const rgb_matrix::Color& fill,
                          const rgb_matrix::Color& border,
                          bool rounded = true) {
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            int px = x + dx, py = y + dy;
            if (rounded) {
                bool top = dy == 0, bottom = dy == h - 1;
                bool left = dx == 0, right = dx == w - 1;
                if ((top && left) || (top && right) || (bottom && left) || (bottom && right))
                    continue;
            }
            bool isEdge = (dy == 0 || dy == h - 1 || dx == 0 || dx == w - 1);
            const rgb_matrix::Color& c = isEdge ? border : fill;
            canvas->SetPixel(px, py, c.r, c.g, c.b);
        }
    }
}

std::string FetchURL(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        curl_easy_cleanup(curl);
    }
    return response;
}

rgb_matrix::Color TempToColor(float tempF) {
    float minT = 32.0f, maxT = 100.0f;
    float clamped = std::max(minT, std::min(maxT, tempF));
    float t = (clamped - minT) / (maxT - minT);

    uint8_t r, g, b;
    if (t < 0.33f) {
        float u = t / 0.33f;
        r = 0; g = static_cast<uint8_t>(255 * u); b = 255;
    } else if (t < 0.66f) {
        float u = (t - 0.33f) / 0.33f;
        r = static_cast<uint8_t>(255 * u);
        g = static_cast<uint8_t>(255 - (90 * u));
        b = static_cast<uint8_t>(255 * (1.0f - u));
    } else {
        float u = (t - 0.66f) / 0.34f;
        r = 255; g = static_cast<uint8_t>(165 * (1.0f - u)); b = 0;
    }
    return rgb_matrix::Color(r, g, b);
}

// Maps WMO weather codes (returned by Open-Meteo) to description strings
// that match our icon selection logic, so icons work without an OWM key.
std::string WmoCodeToDescription(int code) {
    switch (code) {
        case 0:           return "clear sky";
        case 1:           return "mainly clear";
        case 2:           return "partly cloudy";
        case 3:           return "overcast clouds";
        case 45: case 48: return "fog";
        case 51: case 53: case 55:
        case 56: case 57: return "drizzle";
        case 61: case 63: case 65:
        case 66: case 67:
        case 80: case 81: case 82: return "rain";
        case 71: case 73: case 75:
        case 77:
        case 85: case 86: return "snow";
        case 95: case 96: case 99: return "thunderstorm";
        default:           return "unknown";
    }
}

struct DailyForecast {
    std::string date;       // "Mar 14"
    char dayLetter = '?';   // M, T, W, T, F, S, S
    int weatherCode = 0;
    float tempMax = 0, tempMin = 0;
    std::string sunrise;    // "7:04 AM"
    std::string sunset;     // "7:00 PM"
};

struct OpenMeteoData {
    float temp = 62.0f;
    std::string description;
    DailyForecast daily[3];
};

// Convert "2026-03-13T07:05" to "7:05 AM"
std::string FormatMeteoTime(const std::string& isoTime) {
    // Find the T separator
    auto tpos = isoTime.find('T');
    if (tpos == std::string::npos || tpos + 1 >= isoTime.size()) return isoTime;

    std::string timeStr = isoTime.substr(tpos + 1);
    int hour = 0, min = 0;
    if (sscanf(timeStr.c_str(), "%d:%d", &hour, &min) != 2) return timeStr;

    bool pm = hour >= 12;
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, min, pm ? "PM" : "AM");
    return buf;
}

// Get day-of-week letter from ISO date string "YYYY-MM-DD"
char DayOfWeekLetter(const std::string& isoDate) {
    int year, month, day;
    if (sscanf(isoDate.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return '?';
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    mktime(&t);
    const char letters[] = "SMTWTFS";
    return letters[t.tm_wday];
}

// Convert "2026-03-14" to "Mar 14"
std::string FormatMeteoDate(const std::string& isoDate) {
    int year, month, day;
    if (sscanf(isoDate.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return isoDate;
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
    if (month < 1 || month > 12) return isoDate;
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d", months[month - 1], day);
    return buf;
}

OpenMeteoData GetFromOpenMeteo(const std::string& lat, const std::string& lon, Units units = Units::Imperial) {
    std::string tempUnit = (units == Units::Metric) ? "celsius" : "fahrenheit";
    std::string url = "https://api.open-meteo.com/v1/forecast"
                      "?latitude=" + lat +
                      "&longitude=" + lon +
                      "&current_weather=true"
                      "&temperature_unit=" + tempUnit +
                      "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
                      "&forecast_days=3"
                      "&timezone=auto";

    std::string response = FetchURL(url);
    auto data = nlohmann::json::parse(response);

    OpenMeteoData result;
    result.temp = data["current_weather"]["temperature"].get<float>();
    int weathercode = data["current_weather"]["weathercode"].get<int>();
    result.description = WmoCodeToDescription(weathercode);

    // Parse daily forecast data
    if (data.contains("daily")) {
        auto& daily = data["daily"];
        int days = std::min(3, (int)daily["time"].size());
        for (int i = 0; i < days; i++) {
            std::string dateStr = daily["time"][i].get<std::string>();
            result.daily[i].date = FormatMeteoDate(dateStr);
            result.daily[i].dayLetter = DayOfWeekLetter(dateStr);
            result.daily[i].weatherCode = daily["weather_code"][i].get<int>();
            result.daily[i].tempMax = daily["temperature_2m_max"][i].get<float>();
            result.daily[i].tempMin = daily["temperature_2m_min"][i].get<float>();
            result.daily[i].sunrise = FormatMeteoTime(daily["sunrise"][i].get<std::string>());
            result.daily[i].sunset = FormatMeteoTime(daily["sunset"][i].get<std::string>());
        }
    }

    return result;
}

// --- 7-Segment Rendering ---
// Symmetry: mid = t + (h-3t)/2 so both vertical halves have equal height.
// Bottom segments start at mid+t (after middle bar), not mid.
void Draw7Segment(rgb_matrix::FrameCanvas* canvas, int ox, int oy,
                  int w, int h, int t, uint8_t segs,
                  const rgb_matrix::Color& color) {
    auto fill = [&](int rx, int ry, int rw, int rh) {
        for (int dy = 0; dy < rh; dy++)
            for (int dx = 0; dx < rw; dx++)
                canvas->SetPixel(ox + rx + dx, oy + ry + dy, color.r, color.g, color.b);
    };

    int mid = t + (h - 3 * t) / 2;  // symmetric center: equal space above and below
    int hw = w - 2 * t;             // horizontal bar inner width
    int vhTop = mid - t;            // top vertical height
    int vhBot = h - mid - 2 * t;   // bottom vertical height (symmetric to vhTop)

    if (segs & 0x01) fill(t,     0,       hw, t);     // a: top
    if (segs & 0x02) fill(w - t, t,       t,  vhTop); // b: top-right
    if (segs & 0x04) fill(w - t, mid + t, t,  vhBot); // c: bottom-right
    if (segs & 0x08) fill(t,     h - t,   hw, t);     // d: bottom
    if (segs & 0x10) fill(0,     mid + t, t,  vhBot); // e: bottom-left
    if (segs & 0x20) fill(0,     t,       t,  vhTop); // f: top-left
    if (segs & 0x40) fill(t,     mid,     hw, t);     // g: middle
}

void Draw7SegColon(rgb_matrix::FrameCanvas* canvas, int x, int y,
                   int h, int t, const rgb_matrix::Color& color) {
    int dot1_y = y + h / 3 - t / 2;
    int dot2_y = y + 2 * h / 3 - t / 2;
    for (int dy = 0; dy < t; dy++)
        for (int dx = 0; dx < t; dx++) {
            canvas->SetPixel(x + dx, dot1_y + dy, color.r, color.g, color.b);
            canvas->SetPixel(x + dx, dot2_y + dy, color.r, color.g, color.b);
        }
}

// Draws HH:MM:SS in 7-segment style. Returns total width drawn.
int Draw7SegTime(rgb_matrix::FrameCanvas* canvas, int x, int y,
                 int hour12, int min, int sec,
                 int segW, int segH, int segT, int gap,
                 const rgb_matrix::Color& color) {
    int cx = x;

    // Hour (no leading zero)
    if (hour12 >= 10) {
        Draw7Segment(canvas, cx, y, segW, segH, segT, SEG_DIGITS[hour12 / 10], color);
        cx += segW + gap;
    }
    Draw7Segment(canvas, cx, y, segW, segH, segT, SEG_DIGITS[hour12 % 10], color);
    cx += segW + gap;

    // Colon
    Draw7SegColon(canvas, cx, y, segH, segT, color);
    cx += segT + gap;

    // Minutes (always 2 digits)
    Draw7Segment(canvas, cx, y, segW, segH, segT, SEG_DIGITS[min / 10], color);
    cx += segW + gap;
    Draw7Segment(canvas, cx, y, segW, segH, segT, SEG_DIGITS[min % 10], color);
    cx += segW + gap;

    // Colon
    Draw7SegColon(canvas, cx, y, segH, segT, color);
    cx += segT + gap;

    // Seconds (always 2 digits)
    Draw7Segment(canvas, cx, y, segW, segH, segT, SEG_DIGITS[sec / 10], color);
    cx += segW + gap;
    Draw7Segment(canvas, cx, y, segW, segH, segT, SEG_DIGITS[sec % 10], color);
    cx += segW;

    return cx - x;
}

// Measure the width of HH:MM:SS in 7-segment style
int Measure7SegTime(int hour12, int segW, int segT, int gap) {
    int w = 0;
    if (hour12 >= 10) w += segW + gap;  // hour tens
    w += segW + gap;                     // hour ones
    w += segT + gap;                     // colon
    w += 2 * (segW + gap);              // minutes
    w += segT + gap;                     // colon
    w += segW + gap + segW;             // seconds
    return w;
}

// --- Display Modules ---
void DrawWeatherIconModule(rgb_matrix::FrameCanvas* canvas, int x, int y,
                           const std::string& desc, bool isNight) {
    std::string d = desc;
    std::transform(d.begin(), d.end(), d.begin(), ::tolower);

    try {
        if (d.find("clear") != std::string::npos) {
            DrawIcon(canvas, x, y, isNight ? moonIcon : sunIcon);
        } else if (d.find("partly") != std::string::npos ||
                   d.find("few cloud") != std::string::npos ||
                   d.find("light cloud") != std::string::npos ||
                   d.find("scattered cloud") != std::string::npos) {
            DrawIcon(canvas, x, y, isNight ? moonPartlyCloudIcon : partlyCloudyIcon);
        } else if (d.find("cloud") != std::string::npos) {
            DrawIcon(canvas, x, y, isNight ? moonCloudIcon : cloudIcon);
        } else if (d.find("thunder") != std::string::npos) {
            DrawIcon(canvas, x, y, thunderIcon);
        } else if (d.find("drizzle") != std::string::npos) {
            DrawIcon(canvas, x, y, drizzleIcon);
        } else if (d.find("rain") != std::string::npos) {
            DrawIcon(canvas, x, y, rainIcon);
        } else if (d.find("haze") != std::string::npos) {
            DrawIcon(canvas, x, y, hazeIcon);
        } else if (d.find("ash") != std::string::npos) {
            DrawIcon(canvas, x, y, ashIcon);
        } else if (d.find("smoke") != std::string::npos) {
            DrawIcon(canvas, x, y, smokeIcon);
        } else if (d.find("snow") != std::string::npos) {
            DrawIcon(canvas, x, y, snowIcon);
        } else if (d.find("fog") != std::string::npos || d.find("mist") != std::string::npos) {
            DrawIcon(canvas, x, y, fogIcon);
        } else {
            DrawIcon(canvas, x, y, friendIcon);
        }
    } catch (...) {
        std::cerr << "Failed to draw weather icon\n";
    }
}

void DrawTemperatureModule(rgb_matrix::FrameCanvas* canvas, const rgb_matrix::Font& font,
                           int x, int y, const std::string& temp) {
    if (temp.empty() || temp == "Parse error" || temp == "API error") return;

    float tempF = 62.0f;
    try {
        std::string tempClean = temp;
        tempClean.erase(std::remove_if(tempClean.begin(), tempClean.end(),
                        [](char c) { return !std::isdigit(c) && c != '.'; }),
                        tempClean.end());
        if (!tempClean.empty()) tempF = std::stof(tempClean);
    } catch (...) {
        std::cerr << "Failed to parse temperature: " << temp << std::endl;
    }

    rgb_matrix::Color fill = TempToColor(tempF);
    DrawTextOutline(canvas, font, x, y, fill, rgb_matrix::Color(0, 0, 0), temp.c_str());
}

void DrawDayDateModule(rgb_matrix::FrameCanvas* canvas, const rgb_matrix::Font& font,
                       int x, int dayY, int dateY,
                       const char* day, const char* date,
                       const rgb_matrix::Color& color) {
    if (day) rgb_matrix::DrawText(canvas, font, x, dayY, color, nullptr, day);
    if (date) rgb_matrix::DrawText(canvas, font, x, dateY, color, nullptr, date);
}

void DrawWeatherDescModule(rgb_matrix::FrameCanvas* canvas, const rgb_matrix::Font& font,
                           int x, int y, const std::string& desc,
                           const rgb_matrix::Color& color) {
    if (!desc.empty() && desc != "No data" && desc != "Parse error" && desc != "API error") {
        std::string d = desc;
        if (!d.empty()) d[0] = std::toupper(d[0]);
        rgb_matrix::DrawText(canvas, font, x, y, color, nullptr, d.c_str());
    }
}

// --- Tiny 3x5 Digit Renderer ---
// Each digit is 3 pixels wide, 5 pixels tall. Bitmask per row (3 bits, MSB=left).
const uint8_t TINY_DIGITS[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, // 0
    {0x2, 0x6, 0x2, 0x2, 0x7}, // 1
    {0x7, 0x1, 0x7, 0x4, 0x7}, // 2
    {0x7, 0x1, 0x7, 0x1, 0x7}, // 3
    {0x5, 0x5, 0x7, 0x1, 0x1}, // 4
    {0x7, 0x4, 0x7, 0x1, 0x7}, // 5
    {0x7, 0x4, 0x7, 0x5, 0x7}, // 6
    {0x7, 0x1, 0x1, 0x1, 0x1}, // 7
    {0x7, 0x5, 0x7, 0x5, 0x7}, // 8
    {0x7, 0x5, 0x7, 0x1, 0x7}, // 9
};

void DrawTiny3x5Digit(rgb_matrix::FrameCanvas* canvas, int x, int y, int digit,
                      const rgb_matrix::Color& color) {
    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < 5; row++) {
        uint8_t bits = TINY_DIGITS[digit][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (0x4 >> col))
                canvas->SetPixel(x + col, y + row, color.r, color.g, color.b);
        }
    }
}

// Draw a number at (x,y) using tiny 3x5 digits. Returns width drawn.
int DrawTiny3x5Number(rgb_matrix::FrameCanvas* canvas, int x, int y, int number,
                      const rgb_matrix::Color& color) {
    std::string s = std::to_string(number);
    int cx = x;
    for (char c : s) {
        if (c == '-') {
            // minus sign: horizontal line at row 2
            for (int dx = 0; dx < 3; dx++)
                canvas->SetPixel(cx + dx, y + 2, color.r, color.g, color.b);
            cx += 4;
        } else {
            DrawTiny3x5Digit(canvas, cx, y, c - '0', color);
            cx += 4; // 3px digit + 1px gap
        }
    }
    return cx - x;
}

// Tiny 3x5 letter bitmaps for day-of-week labels
void DrawTiny3x5Letter(rgb_matrix::FrameCanvas* canvas, int x, int y, char c,
                       const rgb_matrix::Color& color) {
    // 3x5 bitmaps: each row is 3 bits (MSB=left), same format as digits
    static const uint8_t LETTER_S[5] = {0x7, 0x4, 0x7, 0x1, 0x7};
    static const uint8_t LETTER_M[5] = {0x5, 0x7, 0x7, 0x5, 0x5};
    static const uint8_t LETTER_T[5] = {0x7, 0x2, 0x2, 0x2, 0x2};
    static const uint8_t LETTER_W[5] = {0x5, 0x5, 0x7, 0x7, 0x5};
    static const uint8_t LETTER_F[5] = {0x7, 0x4, 0x7, 0x4, 0x4};

    const uint8_t* bmp = nullptr;
    switch (c) {
        case 'S': bmp = LETTER_S; break;
        case 'M': bmp = LETTER_M; break;
        case 'T': bmp = LETTER_T; break;
        case 'W': bmp = LETTER_W; break;
        case 'F': bmp = LETTER_F; break;
        default: return;
    }
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (bmp[row] & (0x4 >> col))
                canvas->SetPixel(x + col, y + row, color.r, color.g, color.b);
}

// Draw tiny text string (digits, colon, space, A, M, P only)
void DrawTinyChar(rgb_matrix::FrameCanvas* canvas, int x, int y, char c,
                  const rgb_matrix::Color& color) {
    if (c >= '0' && c <= '9') {
        DrawTiny3x5Digit(canvas, x, y, c - '0', color);
    } else if (c == ':') {
        canvas->SetPixel(x, y + 1, color.r, color.g, color.b);
        canvas->SetPixel(x, y + 3, color.r, color.g, color.b);
    }
}

// --- Moon Phase Module (16x16) ---
// Returns moon age in days (0 = new moon, ~14.76 = full moon)
double MoonPhaseAge() {
    // Reference new moon: January 6, 2000 at 18:14 UTC
    struct tm ref = {};
    ref.tm_year = 100; // 2000
    ref.tm_mon = 0;    // January
    ref.tm_mday = 6;
    ref.tm_hour = 18;
    ref.tm_min = 14;
    ref.tm_sec = 0;

    time_t refTime = mktime(&ref); // local time, close enough
    time_t now = time(NULL);
    double diffDays = difftime(now, refTime) / 86400.0;
    double synodic = 29.53059;
    double age = fmod(diffDays, synodic);
    if (age < 0) age += synodic;
    return age;
}

void DrawMoonPhaseModule(rgb_matrix::FrameCanvas* canvas, int x, int y) {
    double age = MoonPhaseAge();
    double synodic = 29.53059;
    double phase = age / synodic; // 0..1

    int cx = x + 7, cy = y + 7; // center of 16x16
    int radius = 6;

    rgb_matrix::Color lit(220, 220, 180);    // warm white
    rgb_matrix::Color dark(40, 40, 40);       // dark gray

    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) continue;

            // Determine if this pixel is illuminated
            // Terminator position based on phase
            double termX = cos(phase * 2.0 * M_PI) * radius;
            bool isLit;

            if (phase < 0.5) {
                // Waxing: right side lights up first
                isLit = (dx > termX);
            } else {
                // Waning: left side still lit
                isLit = (dx < -termX);
            }

            const auto& c = isLit ? lit : dark;
            canvas->SetPixel(cx + dx, cy + dy, c.r, c.g, c.b);
        }
    }
}

// --- Mini Weather Icons (8x8, procedural) ---
void DrawMiniWeatherIcon(rgb_matrix::FrameCanvas* canvas, int x, int y, int wmoCode) {
    rgb_matrix::Color yellow(255, 200, 0);
    rgb_matrix::Color gray(150, 150, 150);
    rgb_matrix::Color blue(50, 100, 255);
    rgb_matrix::Color white(255, 255, 255);

    if (wmoCode <= 1) {
        // Clear/sunny: circle with rays
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx*dx + dy*dy <= 4)
                    canvas->SetPixel(x + 3 + dx, y + 3 + dy, yellow.r, yellow.g, yellow.b);
        // rays
        canvas->SetPixel(x + 3, y, yellow.r, yellow.g, yellow.b);
        canvas->SetPixel(x + 3, y + 6, yellow.r, yellow.g, yellow.b);
        canvas->SetPixel(x, y + 3, yellow.r, yellow.g, yellow.b);
        canvas->SetPixel(x + 6, y + 3, yellow.r, yellow.g, yellow.b);
    } else if (wmoCode <= 3) {
        // Cloudy: rounded blob
        for (int dx = 1; dx <= 5; dx++) {
            canvas->SetPixel(x + dx, y + 3, gray.r, gray.g, gray.b);
            canvas->SetPixel(x + dx, y + 4, gray.r, gray.g, gray.b);
        }
        for (int dx = 2; dx <= 4; dx++)
            canvas->SetPixel(x + dx, y + 2, gray.r, gray.g, gray.b);
    } else if (wmoCode <= 57) {
        // Drizzle/fog: cloud + dots
        for (int dx = 1; dx <= 5; dx++)
            canvas->SetPixel(x + dx, y + 2, gray.r, gray.g, gray.b);
        for (int dx = 2; dx <= 4; dx++)
            canvas->SetPixel(x + dx, y + 1, gray.r, gray.g, gray.b);
        canvas->SetPixel(x + 2, y + 4, blue.r, blue.g, blue.b);
        canvas->SetPixel(x + 4, y + 5, blue.r, blue.g, blue.b);
    } else if (wmoCode <= 82) {
        // Rain: cloud + rain drops
        for (int dx = 1; dx <= 5; dx++)
            canvas->SetPixel(x + dx, y + 2, gray.r, gray.g, gray.b);
        for (int dx = 2; dx <= 4; dx++)
            canvas->SetPixel(x + dx, y + 1, gray.r, gray.g, gray.b);
        canvas->SetPixel(x + 2, y + 4, blue.r, blue.g, blue.b);
        canvas->SetPixel(x + 3, y + 5, blue.r, blue.g, blue.b);
        canvas->SetPixel(x + 4, y + 4, blue.r, blue.g, blue.b);
        canvas->SetPixel(x + 2, y + 6, blue.r, blue.g, blue.b);
    } else if (wmoCode <= 86) {
        // Snow: cloud + snowflakes
        for (int dx = 1; dx <= 5; dx++)
            canvas->SetPixel(x + dx, y + 2, gray.r, gray.g, gray.b);
        for (int dx = 2; dx <= 4; dx++)
            canvas->SetPixel(x + dx, y + 1, gray.r, gray.g, gray.b);
        canvas->SetPixel(x + 2, y + 4, white.r, white.g, white.b);
        canvas->SetPixel(x + 4, y + 5, white.r, white.g, white.b);
        canvas->SetPixel(x + 3, y + 6, white.r, white.g, white.b);
    } else {
        // Thunderstorm: cloud + lightning bolt
        for (int dx = 1; dx <= 5; dx++)
            canvas->SetPixel(x + dx, y + 1, gray.r, gray.g, gray.b);
        for (int dx = 2; dx <= 4; dx++)
            canvas->SetPixel(x + dx, y + 0, gray.r, gray.g, gray.b);
        // zigzag
        canvas->SetPixel(x + 4, y + 3, yellow.r, yellow.g, yellow.b);
        canvas->SetPixel(x + 3, y + 4, yellow.r, yellow.g, yellow.b);
        canvas->SetPixel(x + 4, y + 5, yellow.r, yellow.g, yellow.b);
        canvas->SetPixel(x + 3, y + 6, yellow.r, yellow.g, yellow.b);
    }
}

// --- 3-Day Forecast Module (32x32) ---
void DrawForecastModule(rgb_matrix::FrameCanvas* canvas, const rgb_matrix::Font& font,
                        int x, int y, const OpenMeteoData& meteo) {
    rgb_matrix::Color white(255, 255, 255);
    rgb_matrix::Color hiColor(255, 100, 50);   // warm red for high
    rgb_matrix::Color loColor(80, 150, 255);    // cool blue for low

    for (int i = 0; i < 3; i++) {
        int rowY = y + i * 10;
        const auto& day = meteo.daily[i];

        // Day-of-week letter
        DrawTiny3x5Letter(canvas, x, rowY + 1, day.dayLetter, white);

        // Mini weather icon (8x8) right of letter
        DrawMiniWeatherIcon(canvas, x + 5, rowY, day.weatherCode);

        // High temp
        int hi = (int)roundf(day.tempMax);
        int cx = x + 15;
        cx += DrawTiny3x5Number(canvas, cx, rowY + 1, hi, hiColor);

        // Separator slash
        canvas->SetPixel(cx, rowY + 3, white.r, white.g, white.b);
        canvas->SetPixel(cx + 1, rowY + 2, white.r, white.g, white.b);
        cx += 3;

        // Low temp
        DrawTiny3x5Number(canvas, cx, rowY + 1, (int)roundf(day.tempMin), loColor);
    }
}

// --- Sunrise/Sunset Module (16x32) ---
void DrawMiniSunIcon(rgb_matrix::FrameCanvas* canvas, int x, int y, bool isRise) {
    rgb_matrix::Color sunColor = isRise ? rgb_matrix::Color(255, 180, 0) : rgb_matrix::Color(255, 100, 30);

    // Half-sun: semicircle above horizon line
    for (int dy = -2; dy <= 0; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx*dx + dy*dy <= 5)
                canvas->SetPixel(x + 3 + dx, y + 3 + dy, sunColor.r, sunColor.g, sunColor.b);

    // Horizon line
    for (int dx = 0; dx < 7; dx++)
        canvas->SetPixel(x + dx, y + 4, sunColor.r, sunColor.g, sunColor.b);

    // Arrow (up for sunrise, down for sunset)
    if (isRise) {
        canvas->SetPixel(x + 3, y + 6, sunColor.r, sunColor.g, sunColor.b);
        canvas->SetPixel(x + 2, y + 7, sunColor.r, sunColor.g, sunColor.b);
        canvas->SetPixel(x + 4, y + 7, sunColor.r, sunColor.g, sunColor.b);
    } else {
        canvas->SetPixel(x + 3, y + 7, sunColor.r, sunColor.g, sunColor.b);
        canvas->SetPixel(x + 2, y + 6, sunColor.r, sunColor.g, sunColor.b);
        canvas->SetPixel(x + 4, y + 6, sunColor.r, sunColor.g, sunColor.b);
    }
}

void DrawTinyTimeString(rgb_matrix::FrameCanvas* canvas, int x, int y,
                        const std::string& timeStr, const rgb_matrix::Color& color) {
    int cx = x;
    for (char c : timeStr) {
        if (c == ' ') break; // stop at AM/PM part, just show time
        if (c == ':') {
            DrawTinyChar(canvas, cx, y, ':', color);
            cx += 2;
        } else if (c >= '0' && c <= '9') {
            DrawTiny3x5Digit(canvas, cx, y, c - '0', color);
            cx += 4;
        }
    }
}

void DrawSunriseSunsetModule(rgb_matrix::FrameCanvas* canvas, int x, int y,
                             const OpenMeteoData& meteo) {
    rgb_matrix::Color riseColor(255, 180, 0);
    rgb_matrix::Color setColor(255, 100, 30);

    // Top half: sunrise
    DrawMiniSunIcon(canvas, x, y, true);
    DrawTinyTimeString(canvas, x, y + 10, meteo.daily[0].sunrise, riseColor);

    // Bottom half: sunset
    DrawMiniSunIcon(canvas, x, y + 16, false);
    DrawTinyTimeString(canvas, x, y + 26, meteo.daily[0].sunset, setColor);
}

// --- Update Static Frame ---
void UpdateStaticFrame(rgb_matrix::FrameCanvas* staticFrame,
                       const ClockConfig& config,
                       const WeatherData& weatherData,
                       const OpenMeteoData& meteoData,
                       const char* day_str, const char* date_str,
                       Font& tempFont, Color& weatherColor, Color& clockColor,
                       bool isNight, DisplayMode mode) {
    if (!staticFrame) {
        std::cerr << "staticFrame is null!\n";
        return;
    }
    staticFrame->Clear();

    const auto& layout = (mode == DisplayMode::Standard)        ? config.standardLayout
                       : (mode == DisplayMode::WideHorizontal) ? config.wideHorizontalLayout
                       : config.wideVerticalLayout;

    for (const auto& mod : layout) {
        switch (mod.type) {
        case ModuleType::WeatherIcon:
            DrawWeatherIconModule(staticFrame, mod.x, mod.y, weatherData.description, isNight);
            break;
        case ModuleType::Temperature: {
            int tx = mod.x;
            if (tx == -1) {
                // Auto-center under weather icon (find it in layout)
                int iconX = 16; // default
                for (const auto& m : layout) {
                    if (m.type == ModuleType::WeatherIcon) { iconX = m.x; break; }
                }
                int iconCenterX = iconX + ICON_SIZE / 2;
                int tempWidth = MeasureTextWidth(tempFont, weatherData.temp);
                tx = iconCenterX - tempWidth / 2;
            }
            DrawTemperatureModule(staticFrame, tempFont, tx, mod.y, weatherData.temp);
            break;
        }
        case ModuleType::DayDate:
            DrawDayDateModule(staticFrame, tempFont, mod.x, mod.y, mod.y + 12,
                             day_str, date_str, clockColor);
            break;
        case ModuleType::WeatherDesc:
            DrawWeatherDescModule(staticFrame, tempFont, mod.x, mod.y,
                                 weatherData.description, weatherColor);
            break;
        case ModuleType::MoonPhase:
            DrawMoonPhaseModule(staticFrame, mod.x, mod.y);
            break;
        case ModuleType::Forecast:
            DrawForecastModule(staticFrame, tempFont, mod.x, mod.y, meteoData);
            break;
        case ModuleType::SunriseSunset:
            DrawSunriseSunsetModule(staticFrame, mod.x, mod.y, meteoData);
            break;
        }
    }
}

// --- Night Time Detection ---
bool IsNightTime(int hour, int nightStart, int nightEnd) {
    if (nightStart > nightEnd) {
        // Wraps around midnight (e.g., 18-6: night from 6PM to 6AM)
        return hour >= nightStart || hour < nightEnd;
    } else {
        return hour >= nightStart && hour < nightEnd;
    }
}

// --- CLI Argument Parsing ---
void ParseCustomArgs(int* argc, char** argv, ClockConfig& config) {
    int newArgc = 1; // keep argv[0]
    for (int i = 1; i < *argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--day-brightness=") == 0) {
            config.dayBrightness = std::max(1, std::min(100, std::stoi(arg.substr(17))));
        } else if (arg.find("--night-brightness=") == 0) {
            config.nightBrightness = std::max(1, std::min(100, std::stoi(arg.substr(19))));
        } else if (arg.find("--night-start=") == 0) {
            config.nightStart = std::max(0, std::min(23, std::stoi(arg.substr(14))));
        } else if (arg.find("--night-end=") == 0) {
            config.nightEnd = std::max(0, std::min(23, std::stoi(arg.substr(12))));
        } else if (arg == "--clock-only") {
            config.clockOnly = true;
        } else {
            argv[newArgc++] = argv[i];
        }
    }
    *argc = newArgc;
}

// --- Main Program ---
int main(int argc, char* argv[]) {
    std::cerr << "Entered main()\n";

    // Load config from file, then override with CLI args
    ClockConfig config = LoadConfig("config.json");
    ParseCustomArgs(&argc, argv, config);

    RGBMatrix::Options defaults;
    defaults.rows = 64;
    defaults.cols = 128;
    defaults.chain_length = 1;
    defaults.parallel = 1;

    std::string lastDayStr, lastDateStr;

    rgb_matrix::RuntimeOptions runtime_opt;
    RGBMatrix* matrix = rgb_matrix::CreateMatrixFromFlags(&argc, &argv,
                                                          &defaults, &runtime_opt);
    if (matrix == nullptr) return 1;

    // Detect display mode from actual matrix dimensions
    int totalW = matrix->width();
    int totalH = matrix->height();
    DisplayMode displayMode;

    if (totalW >= 256 && totalH >= 64) {
        displayMode = DisplayMode::WideHorizontal;
        std::cerr << "Display mode: Wide Horizontal (" << totalW << "x" << totalH << ")\n";
    } else if (totalW >= 128 && totalH >= 128) {
        displayMode = DisplayMode::WideVertical;
        std::cerr << "Display mode: Wide Vertical (" << totalW << "x" << totalH << ")\n";
    } else {
        displayMode = DisplayMode::Standard;
        std::cerr << "Display mode: Standard (" << totalW << "x" << totalH << ")\n";
    }

    Font clockFont, tempFont;
    if (!clockFont.LoadFont("fonts/12x24.bdf")) {
        std::cerr << "Couldn't load clock font\n";
        return 1;
    }
    if (!tempFont.LoadFont("fonts/6x12.bdf")) {
        std::cerr << "Couldn't load temp font\n";
        return 1;
    }

    Color clockColor(255, 255, 255);
    Color weatherColor(0, 255, 255);

    std::string api_key = config.api_key;
    std::string lat = config.lat;
    std::string lon = config.lon;
    Units units = config.units;

    WeatherData weatherData;
    OpenMeteoData meteoData;
    time_t lastWeatherUpdate = 0;
    bool weatherFetchFailed = false;

    // Load icons
    try {
        LoadIconFromPNG("icons/sun.png", sunIcon);
        LoadIconFromPNG("icons/moon.png", moonIcon);
        LoadIconFromPNG("icons/cloud.png", cloudIcon);
        LoadIconFromPNG("icons/drizzle.png", drizzleIcon);
        LoadIconFromPNG("icons/rain.png", rainIcon);
        LoadIconFromPNG("icons/snow.png", snowIcon);
        LoadIconFromPNG("icons/fog.png", fogIcon);
        LoadIconFromPNG("icons/light_cloud.png", partlyCloudyIcon);
        LoadIconFromPNG("icons/friend.png", friendIcon);
        LoadIconFromPNG("icons/thunder.png", thunderIcon);
        LoadIconFromPNG("icons/ash.png", ashIcon);
        LoadIconFromPNG("icons/haze.png", hazeIcon);
        LoadIconFromPNG("icons/smoke.png", smokeIcon);
        LoadIconFromPNG("icons/night_lightcloud.png", moonPartlyCloudIcon);
        LoadIconFromPNG("icons/night_cloud.png", moonCloudIcon);
    } catch (...) {
        std::cerr << "Failed to load png icons\n";
    }

    // Buffers
    rgb_matrix::FrameCanvas* offscreen = matrix->CreateFrameCanvas();
    rgb_matrix::FrameCanvas* staticFrame = matrix->CreateFrameCanvas();

    // 7-segment sizing — driven by chosen font style and display mode.
    // wideOrClock = true uses the larger "wide panel" preset for the chosen style.
    bool wideOrClock = config.clockOnly || (displayMode != DisplayMode::Standard);
    SegParams segP = GetSegParams(config.clockFont, wideOrClock);
    int segW = segP.w, segH = segP.h, segT = segP.t, segGap = segP.gap;
    std::cerr << "ClockFont segW=" << segW << " segH=" << segH
              << " segT=" << segT << " gap=" << segGap << "\n";

    // fullRedrawNeeded: how many frames need a full CopyFrom(staticFrame).
    // We keep 2 (one per rotation canvas) so both display buffers get updated
    // when static content changes, eliminating weather-panel flicker.
    int fullRedrawNeeded = 2;

    bool currentIsNight = false;
    bool firstFrame = true;

    while (true) {
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);

        int hour24 = tm_now->tm_hour;
        int hour12 = hour24 % 12;
        if (hour12 == 0) hour12 = 12;
        bool isPM = hour24 >= 12;
        int minute = tm_now->tm_min;
        int second = tm_now->tm_sec;

        char date_str[64];
        strftime(date_str, sizeof(date_str), "%m/%d/%y", tm_now);
        char day_str[64];
        strftime(day_str, sizeof(day_str), "%A", tm_now);

        std::string currentDayStr(day_str);
        std::string currentDateStr(date_str);
        bool dateChanged = (currentDayStr != lastDayStr || currentDateStr != lastDateStr);

        // Night mode detection and brightness control
        bool isNight = IsNightTime(hour24, config.nightStart, config.nightEnd);
        if (isNight != currentIsNight || firstFrame) {
            currentIsNight = isNight;
            matrix->SetBrightness(isNight ? config.nightBrightness : config.dayBrightness);
            std::cerr << "Brightness: " << (isNight ? config.nightBrightness : config.dayBrightness)
                      << " (" << (isNight ? "night" : "day") << ")\n";
        }

        // Update weather (skipped entirely in clock-only mode)
        if (!config.clockOnly) {
            int retryInterval = weatherFetchFailed ? WEATHER_RETRY_INTERVAL : WEATHER_UPDATE_INTERVAL;
            if (difftime(now, lastWeatherUpdate) > retryInterval || dateChanged || firstFrame) {
                weatherFetchFailed = false;

                // Only call OWM if the key has been configured — the placeholder "YOUR API KEY"
                // contains spaces which cause CURLE_URL_MALFORMAT and a failed description.
                bool owmConfigured = !api_key.empty() && api_key.find(' ') == std::string::npos;
                if (owmConfigured) {
                    std::string weatherJson = GetWeather(lat, lon, api_key, units);
                    weatherData = ParseWeather(weatherJson, units);
                }

                try {
                    meteoData = GetFromOpenMeteo(lat, lon, units);
                    std::ostringstream oss;
                    const char* unit_label = (units == Units::Metric) ? "°C" : "°F";
                    oss << std::fixed << std::setprecision(1) << meteoData.temp << unit_label;
                    weatherData.temp = oss.str();
                    // Use Open-Meteo WMO description when OWM isn't configured, failed
                    // (empty temp = any OWM error), or returned a non-weather description.
                    if (!owmConfigured || weatherData.temp.empty() ||
                        weatherData.description.empty() ||
                        weatherData.description == "No data" ||
                        weatherData.description == "Parse error") {
                        weatherData.description = meteoData.description;
                    }
                    std::cerr << "Weather: " << weatherData.description
                              << " | Temp: " << weatherData.temp << std::endl;
                } catch (...) {
                    std::cerr << "Failed to get data from Open-Meteo\n";
                    if (weatherData.temp.empty()) weatherFetchFailed = true;
                }

                // Always update timestamp to prevent retry-every-second
                lastWeatherUpdate = now;

                UpdateStaticFrame(staticFrame, config, weatherData, meteoData,
                                  day_str, date_str, tempFont, weatherColor, clockColor,
                                  isNight, displayMode);
                fullRedrawNeeded = 2; // refresh both rotation canvases
            }
        }
        lastDateStr = currentDateStr;
        lastDayStr = currentDayStr;
        firstFrame = false;

        // Compose frame.
        // Clock-only: full clear every second (no static content).
        // After a static update: CopyFrom staticFrame for 2 frames (one per rotation canvas)
        //   so both display buffers get the fresh weather content.
        // Normal operation: only clear the clock region — the returned offscreen canvas
        //   already has the correct static content from its previous display cycle,
        //   eliminating the per-second CopyFrom that caused the weather panel to flicker.
        if (config.clockOnly) {
            offscreen->Clear();
        } else if (fullRedrawNeeded > 0) {
            offscreen->CopyFrom(*staticFrame);
            fullRedrawNeeded--;
        } else {
            // Partial clear: only erase the clock region.
            // Standard: top strip across full width (clock row + AM/PM space).
            // Wide modes: entire left/top 128x64 panel (which is the clock panel).
            if (displayMode == DisplayMode::Standard) {
                ClearRegion(offscreen, 0, 0, totalW, segH + 16);
            } else {
                ClearRegion(offscreen, 0, 0, PANEL_WIDTH, PANEL_HEIGHT);
            }
        }

        // Draw 7-segment time + AM/PM
        const char* ampm = isPM ? "PM" : "AM";
        int ampmW = MeasureTextWidth(tempFont, ampm);

        // Center based on max possible width (2-digit hour) so position never shifts.
        // Single-digit hours draw offset by one digit-slot so colon/minutes stay fixed.
        int maxDigitsW = Measure7SegTime(10, segW, segT, segGap);
        int singleOffset = (hour12 < 10) ? segW + segGap : 0;

        if (config.clockOnly) {
            // Clock-only: large digits centered on full screen, AM/PM below
            int timeX = std::max(0, (totalW - maxDigitsW) / 2);
            int timeY = (totalH - segH - 14) / 2;

            Draw7SegTime(offscreen, timeX + singleOffset, timeY, hour12, minute, second,
                         segW, segH, segT, segGap, clockColor);

            int ampmX = (totalW - ampmW) / 2;
            rgb_matrix::DrawText(offscreen, tempFont, ampmX, timeY + segH + 12,
                                 clockColor, nullptr, ampm);

        } else if (displayMode == DisplayMode::Standard) {
            // Time centered across full width, AM/PM to the right
            int totalTimeW = maxDigitsW + 3 + ampmW;
            int timeX = std::max(0, (totalW - totalTimeW) / 2);
            int timeY = 1;

            Draw7SegTime(offscreen, timeX + singleOffset, timeY, hour12, minute, second,
                         segW, segH, segT, segGap, clockColor);

            // AM/PM aligned to bottom-right of digit block (fixed position regardless of hour width)
            rgb_matrix::DrawText(offscreen, tempFont, timeX + maxDigitsW + 3, timeY + segH,
                                 clockColor, nullptr, ampm);

        } else if (displayMode == DisplayMode::WideHorizontal) {
            // Center in left 128px panel, AM/PM below
            int timeX = std::max(0, (PANEL_WIDTH - maxDigitsW) / 2);
            int timeY = (PANEL_HEIGHT - segH - 14) / 2;

            Draw7SegTime(offscreen, timeX + singleOffset, timeY, hour12, minute, second,
                         segW, segH, segT, segGap, clockColor);

            int ampmX = (PANEL_WIDTH - ampmW) / 2;
            rgb_matrix::DrawText(offscreen, tempFont, ampmX, timeY + segH + 12,
                                 clockColor, nullptr, ampm);

        } else { // WideVertical
            // Center in top 128x64 panel, AM/PM below
            int timeX = std::max(0, (PANEL_WIDTH - maxDigitsW) / 2);
            int timeY = (PANEL_HEIGHT - segH - 14) / 2;

            Draw7SegTime(offscreen, timeX + singleOffset, timeY, hour12, minute, second,
                         segW, segH, segT, segGap, clockColor);

            int ampmX = (PANEL_WIDTH - ampmW) / 2;
            rgb_matrix::DrawText(offscreen, tempFont, ampmX, timeY + segH + 12,
                                 clockColor, nullptr, ampm);
        }

        // Swap completed frame
        offscreen = matrix->SwapOnVSync(offscreen);
        usleep(1000 * 1000);
    }

    return 0;
}
