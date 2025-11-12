#include "led-matrix.h"
#include "graphics.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <time.h>
#include <sstream>
#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include "lodepng.h"
// g++ -o clock clock.cc -I../include -L../lib -lrgbmatrix -lcurl

using rgb_matrix::Canvas;
using rgb_matrix::RGBMatrix;
using rgb_matrix::Font;
using rgb_matrix::Color;

enum class Units { Metric, Imperial };

const int LEFT_PANEL_WIDTH = 64;
const int RIGHT_PANEL_X = 64;
const int TOTAL_WIDTH = 128;
const int ICON_SIZE = 32;

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

// --- Weather Fetch Helpers ---
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

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

struct WeatherData {
    std::string description;
    std::string temp;
};

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



bool LoadIconFromPNG(const std::string& filename, Pixel icon[ICON_SIZE][ICON_SIZE]) {
    std::vector<unsigned char> image; // raw RGBA pixels
    unsigned width, height;

    unsigned error = lodepng::decode(image, width, height, filename);
    if (error) {
        std::cerr << "PNG decode error in " << filename << ": "
                  << lodepng_error_text(error) << std::endl;
        return false;
    }

    // Clear icon buffer
    for (int y = 0; y < ICON_SIZE; ++y)
        for (int x = 0; x < ICON_SIZE; ++x)
            icon[y][x] = {0, 0, 0};

    // Copy pixels into icon buffer (clipped to ICON_SIZE)
    for (unsigned y = 0; y < std::min(height, (unsigned)ICON_SIZE); ++y) {
        for (unsigned x = 0; x < std::min(width, (unsigned)ICON_SIZE); ++x) {
            int idx = 4 * (y * width + x); // RGBA
            uint8_t r = image[idx];
            uint8_t g = image[idx + 1];
            uint8_t b = image[idx + 2];
            uint8_t a = image[idx + 3];

            if (a > 128) icon[y][x] = {r, g, b}; // skip transparent pixels
        }
    }

    return true;
}


// --- Helpers to clear and prerender icons ---
void ClearIcon(Pixel icon[ICON_SIZE][ICON_SIZE]) {
    for (int y = 0; y < ICON_SIZE; y++)
        for (int x = 0; x < ICON_SIZE; x++)
            icon[y][x] = {0, 0, 0};
}

void PreRenderSun() {
    ClearIcon(sunIcon);
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - ICON_SIZE / 2;
            int dy = y - ICON_SIZE / 2;
            if (dx * dx + dy * dy <= 64) sunIcon[y][x] = {0, 255, 255};
        }
    }
}

void PreRenderMoon() {
    ClearIcon(moonIcon);
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - ICON_SIZE / 2;
            int dy = y - ICON_SIZE / 2;
            if (dx * dx + dy * dy <= 64) moonIcon[y][x] = {255, 200, 200};
            int dx2 = x - ICON_SIZE / 2 - 4;
            if (dx2 * dx2 + dy * dy <= 64) moonIcon[y][x] = {0, 0, 0};
        }
    }
}

void PreRenderCloud() {
    ClearIcon(cloudIcon);
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - 16, dy = y - 16;
            if (dx * dx + dy * dy <= 36) cloudIcon[y][x] = {200, 200, 200};
            dx = x - 12; dy = y - 18;
            if (dx * dx + dy * dy <= 36) cloudIcon[y][x] = {200, 200, 200};
            dx = x - 20; dy = y - 18;
            if (dx * dx + dy * dy <= 36) cloudIcon[y][x] = {200, 200, 200};
        }
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

    // Sun (top-left corner)
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx = x - 10, dy = y - 10;
            if (dx * dx + dy * dy <= 36) {
                partlyCloudyIcon[y][x] = {255, 255, 0};  // yellow sun
            }
        }
    }

    // Cloud (center-right)
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int dx1 = x - 18, dy1 = y - 18;
            int dx2 = x - 22, dy2 = y - 16;
            if (dx1 * dx1 + dy1 * dy1 <= 36 || dx2 * dx2 + dy2 * dy2 <= 36) {
                partlyCloudyIcon[y][x] = {200, 200, 200};  // light gray cloud
            }
        }
    }
}

void PreRenderFog() {
    ClearIcon(fogIcon);
    for (int y = 12; y <= 20; y += 4) {
        for (int x = 8; x < 24; x++) {
            fogIcon[y][x] = {180, 180, 180};
        }
    }
}

// --- Blit function ---
void DrawIcon(rgb_matrix::FrameCanvas* canvas, int x, int y, Pixel icon[ICON_SIZE][ICON_SIZE]) {
    for (int row = 0; row < ICON_SIZE; row++) {
        for (int col = 0; col < ICON_SIZE; col++) {
            Pixel p = icon[row][col];
            if (p.r || p.g || p.b) {
                canvas->SetPixel(x + col, y + row, p.r, p.g, p.b);
            }
        }
    }
}

void DrawBorder(rgb_matrix::FrameCanvas* canvas, Color color) {
    int width = canvas->width();
    int height = canvas->height();

    for (int x = 0; x < width; ++x) {
        canvas->SetPixel(x, 0, color.r, color.g, color.b);             // top
        canvas->SetPixel(x, height - 1, color.r, color.g, color.b);    // bottom
    }

    for (int y = 0; y < height; ++y) {
        canvas->SetPixel(0, y, color.r, color.g, color.b);             // left
        canvas->SetPixel(width - 1, y, color.r, color.g, color.b);     // right
    }
}


// --- Update static frame ---
void UpdateStaticFrame(rgb_matrix::FrameCanvas* staticFrame,
                       const WeatherData &weatherData,
                       const char *day_str,
                       const char *date_str,
                       Font &tempFont,
                       Color &weatherColor,
                       Color &clockColor,
                       bool isNight) {
    staticFrame->Clear();

    std::string desc = weatherData.description;
    std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);
	//DrawBorder(staticFrame, Color(128, 128, 128));
    // Weather icon shifted upward to y = 24
	if (desc.find("clear") != std::string::npos) {
		if (isNight) DrawIcon(staticFrame, 16, 24, moonIcon);
		else         DrawIcon(staticFrame, 16, 24, sunIcon);
	} else if (desc.find("partly") != std::string::npos ||
			   desc.find("few cloud") != std::string::npos ||
			   desc.find("light cloud") != std::string::npos ||
			   desc.find("scattered cloud") != std::string::npos) {
		DrawIcon(staticFrame, 16, 24, partlyCloudyIcon);
	} else if (desc.find("cloud") != std::string::npos) {
		DrawIcon(staticFrame, 16, 24, cloudIcon);
    } else if (desc.find("thunder") != std::string::npos) {
        DrawIcon(staticFrame, 16, 24, thunderIcon);
    } else if (desc.find("drizzle") != std::string::npos) {
        DrawIcon(staticFrame, 16, 24, drizzleIcon);
    } else if (desc.find("rain") != std::string::npos) {
        DrawIcon(staticFrame, 16, 24, rainIcon);
    } else if (desc.find("snow") != std::string::npos) {
        DrawIcon(staticFrame, 16, 24, snowIcon);
    } else if (desc.find("fog") != std::string::npos || desc.find("mist") != std::string::npos) {
        DrawIcon(staticFrame, 16, 24, fogIcon);
    } else {
		DrawIcon(staticFrame, 16, 24, friendIcon);
		}

    // Temperature baseline aligned with date (y = 62)
    int temp_width = rgb_matrix::DrawText(staticFrame, tempFont, 0, 0,
                                          weatherColor, nullptr,
                                          weatherData.temp.c_str());
    int temp_x = (LEFT_PANEL_WIDTH - temp_width) / 2;
    rgb_matrix::DrawText(staticFrame, tempFont, temp_x, 62,
                         weatherColor, nullptr, weatherData.temp.c_str());
    // Day + Date also at y = 62
    rgb_matrix::DrawText(staticFrame, tempFont, RIGHT_PANEL_X + 6, 50,
                         clockColor, nullptr, day_str);
    rgb_matrix::DrawText(staticFrame, tempFont, RIGHT_PANEL_X + 6, 62,
                         clockColor, nullptr, date_str);
}

// --- Main Program ---
int main(int argc, char* argv[]) {
    RGBMatrix::Options defaults;
    defaults.rows = 64;
    defaults.cols = 64;
    defaults.chain_length = 2;
    defaults.parallel = 1;

    rgb_matrix::RuntimeOptions runtime_opt;
    RGBMatrix* matrix = rgb_matrix::CreateMatrixFromFlags(&argc, &argv,
                                                          &defaults, &runtime_opt);
    if (matrix == nullptr) return 1;

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

    std::string api_key = "API_KEY_HERE";
    std::string city = "Los%20Angeles,US";
	std::string lat = "34.078";
	std::string lon = "-118.260";
    Units units = Units::Imperial;

    WeatherData weatherData;
    time_t lastWeatherUpdate = 0;
	
    // Pre-render icons once
    LoadIconFromPNG("icons/sun.png", sunIcon);
    LoadIconFromPNG("icons/moon.png", moonIcon);
    LoadIconFromPNG("icons/cloud.png", cloudIcon);
	LoadIconFromPNG("icons/drizzle.png", drizzleIcon);
    LoadIconFromPNG("icons/rain.png", rainIcon);
    PreRenderSnow();
    LoadIconFromPNG("icons/fog.png", fogIcon);
	LoadIconFromPNG("icons/light_cloud.png", partlyCloudyIcon);
	LoadIconFromPNG("icons/friend.png", friendIcon);
	LoadIconFromPNG("icons/thunder.png", thunderIcon);

    // Buffers
    rgb_matrix::FrameCanvas* offscreen = matrix->CreateFrameCanvas();
    rgb_matrix::FrameCanvas* staticFrame = matrix->CreateFrameCanvas();

    while (true) {
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);

        char time_str[64];
        strftime(time_str, sizeof(time_str), "%-I:%M:%S", tm_now);

        char date_str[64];
        strftime(date_str, sizeof(date_str), "%m/%d/%y", tm_now);

        char day_str[64];
        strftime(day_str, sizeof(day_str), "%A", tm_now);

        // Update weather every 15 minutes
        if (difftime(now, lastWeatherUpdate) > 900 || weatherData.temp.empty()) {
            std::string weatherJson = GetWeather(lat, lon, api_key, units);
            weatherData = ParseWeather(weatherJson, units);
            lastWeatherUpdate = now;

            int hour = tm_now->tm_hour;
            bool isNight = (hour < 6 || hour >= 18);

            UpdateStaticFrame(staticFrame, weatherData, day_str, date_str,
                              tempFont, weatherColor, clockColor, isNight);
        }

        // Compose frame: copy static + overlay time
        offscreen->Clear();
        offscreen->CopyFrom(*staticFrame);

        int time_width = rgb_matrix::DrawText(offscreen, clockFont, 0, 0,
                                              clockColor, nullptr, time_str);
        int time_x = (TOTAL_WIDTH - time_width) / 2;
        rgb_matrix::DrawText(offscreen, clockFont, time_x, 20,
                             clockColor, nullptr, time_str);
		
		
        // Swap completed frame
        offscreen = matrix->SwapOnVSync(offscreen);

        usleep(1000 * 1000); // refresh once per second
    }

    return 0;
}
   
