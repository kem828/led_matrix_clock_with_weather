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
#include <cmath>
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
Pixel hazeIcon[ICON_SIZE][ICON_SIZE];
Pixel ashIcon[ICON_SIZE][ICON_SIZE];
Pixel smokeIcon[ICON_SIZE][ICON_SIZE];
Pixel moonCloudIcon[ICON_SIZE][ICON_SIZE];
Pixel moonPartlyCloudIcon[ICON_SIZE][ICON_SIZE];


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
    if (!canvas) {
        std::cerr << "Canvas is null\n";
        return;
    }

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
    for (int y = 0; y < height; ++y) {
        canvas->SetPixel(0, y, color.r, color.g, color.b);             // left
        canvas->SetPixel(width - 1, y, color.r, color.g, color.b);     // right
    }
}
}
void DrawTextOutline(rgb_matrix::FrameCanvas* canvas, const rgb_matrix::Font& font, int x, int y,
                     const rgb_matrix::Color& outline_color, const rgb_matrix::Color& text_color,
                     const char* text) {
    // Draw the outline by shifting the text in all 8 directions
    rgb_matrix::DrawText(canvas, font, x - 1, y, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x + 1, y, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x, y - 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x, y + 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x - 1, y - 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x + 1, y + 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x - 1, y + 1, outline_color, nullptr, text);
    rgb_matrix::DrawText(canvas, font, x + 1, y - 1, outline_color, nullptr, text);

    // Draw the main text on top
    rgb_matrix::DrawText(canvas, font, x, y, text_color, nullptr, text);
}

int MeasureTextWidth(const rgb_matrix::Font& font, const std::string& text) {
    int width = 0;
    for (char c : text) {
        width += font.CharacterWidth(c);
    }
    return width;
}


void DrawFilledRoundedBox(rgb_matrix::FrameCanvas* canvas,
                          int x, int y, int w, int h,
                          const rgb_matrix::Color& fill,
                          const rgb_matrix::Color& border,
                          bool rounded = true) {
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            int px = x + dx;
            int py = y + dy;

            if (rounded) {
                bool top = dy == 0;
                bool bottom = dy == h - 1;
                bool left = dx == 0;
                bool right = dx == w - 1;
                if ((top && left) || (top && right) ||
                    (bottom && left) || (bottom && right)) continue;
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
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // handle redirects

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
    }

    return response;
}



rgb_matrix::Color TempToColor(float tempF) {
    float minT = 32.0f;   // freezing
    float maxT = 100.0f;  // hot
    float clamped = std::max(minT, std::min(maxT, tempF));

    // Normalize 0..1
    float t = (clamped - minT) / (maxT - minT);

    uint8_t r, g, b;

    if (t < 0.33f) {
        // Blue → Cyan
        float u = t / 0.33f;
        r = 0;
        g = static_cast<uint8_t>(255 * u);
        b = 255;
    } else if (t < 0.66f) {
        // Cyan → Orange
        float u = (t - 0.33f) / 0.33f;
        r = static_cast<uint8_t>(255 * u);
        g = static_cast<uint8_t>(255 - (90 * u));   // 255 → 165
        b = static_cast<uint8_t>(255 * (1.0f - u)); // 255 → 0
    } else {
        // Orange → Red
        float u = (t - 0.66f) / 0.34f;
        r = 255;
        g = static_cast<uint8_t>(165 * (1.0f - u));
        b = 0;
    }

    return rgb_matrix::Color(r, g, b);
}


float GetCurrentTempFromOpenMeteo(const std::string& lat, const std::string& lon) {
    std::string url = "https://api.open-meteo.com/v1/forecast"
                      "?latitude=" + lat +
                      "&longitude=" + lon +
                      "&current_weather=true"
                      "&temperature_unit=fahrenheit";

    std::string response = FetchURL(url);  // your libcurl wrapper
    auto data = nlohmann::json::parse(response);

    return data["current_weather"]["temperature"];
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
    
	if (!staticFrame) {
		std::cerr << "staticFrame is null!\n";
    return;
	}
	//std::cerr << "staticFrame size: " << staticFrame->width() << "x" << staticFrame->height() << std::endl;

	staticFrame->Clear();
	//std::cerr << "sTransform\n";
	std::string desc = weatherData.description;
	std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);

	//DrawBorder(staticFrame, Color(128, 128, 128));
    // Weather icon shifted upward to y = 24
	//std::cerr << "Draw Icons!\n";
	try{
		if (desc.find("clear") != std::string::npos) {
			if (isNight) DrawIcon(staticFrame, 16, 23, moonIcon);
			else {
				DrawIcon(staticFrame, 16, 23, sunIcon);
			}
		} else if (desc.find("partly") != std::string::npos ||
				desc.find("few cloud") != std::string::npos ||
				desc.find("light cloud") != std::string::npos ||
				desc.find("scattered cloud") != std::string::npos){
			if (isNight) DrawIcon(staticFrame, 16, 23, moonPartlyCloudIcon);
			else {
				DrawIcon(staticFrame, 16, 23, partlyCloudyIcon);
			}
		} else if (desc.find("cloud") != std::string::npos) {
				if (isNight) DrawIcon(staticFrame, 16, 23, moonCloudIcon);
				else {
				DrawIcon(staticFrame, 16, 23, cloudIcon);
			}
		} else if (desc.find("thunder") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, thunderIcon);
		} else if (desc.find("drizzle") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, drizzleIcon);
		} else if (desc.find("rain") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, rainIcon);
		} else if (desc.find("haze") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, hazeIcon);
		} else if (desc.find("ash") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, ashIcon);
		} else if (desc.find("smoke") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, smokeIcon);			
		} else if (desc.find("snow") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, snowIcon);
		} else if (desc.find("fog") != std::string::npos || desc.find("mist") != std::string::npos) {
			DrawIcon(staticFrame, 16, 23, fogIcon);
		} else {
			DrawIcon(staticFrame, 16, 23, friendIcon);
			}
	} catch (...) {
		std::cerr << "Failed to Draw Icons: " << weatherData.temp << std::endl;
	}
	float tempF = 62.0f;  // default fallback
	try {
		std::string tempClean = weatherData.temp;
		tempClean.erase(std::remove_if(tempClean.begin(), tempClean.end(),
									   [](char c) { return !std::isdigit(c) && c != '.'; }),
						tempClean.end());
		if (!tempClean.empty()) {
			tempF = std::stof(tempClean);
		}
	} catch (...) {
		std::cerr << "Failed to parse temperature: " << weatherData.temp << std::endl;
	}
	//std::cerr << "Temp to colo!\n";
	
	//std::cerr << "Draw Text temperature!\n";
	if (!weatherData.temp.empty() &&
		weatherData.temp != "Parse error" &&
		weatherData.temp != "API error") {
		
	rgb_matrix::DrawText(staticFrame, tempFont, 0, 0,
										  weatherColor, nullptr,
										  weatherData.temp.c_str());

	}
	int temp_width = MeasureTextWidth(tempFont, weatherData.temp);

	//std::cerr << "Get color and fill box!\n";
	int icon_center_x = 16 + ICON_SIZE / 2;

	//const int TEMP_BOX_WIDTH = 48;
	const int TEMP_BOX_PADDING = 0;
	int box_w = temp_width + TEMP_BOX_PADDING * 2;
	int box_h = tempFont.height() + TEMP_BOX_PADDING * 2;
	int box_x = icon_center_x - box_w / 2;
	int box_y = 64 - box_h;  // aligns bottom edge 


	rgb_matrix::Color fill = TempToColor(tempF);
	rgb_matrix::Color border(255, 255, 255);

	try {
		//DrawFilledRoundedBox(staticFrame, box_x, box_y, box_w, box_h, fill, border);
		//rgb_matrix::DrawText(staticFrame, tempFont, box_x + 3, box_y + box_h - 4,
							 //rgb_matrix::Color(255, 255, 255), nullptr,
							 //weatherData.temp.c_str());
		DrawTextOutline(staticFrame, tempFont, box_x + 3, 62,fill, rgb_matrix::Color(0, 0, 0), weatherData.temp.c_str());
	} catch (...) {
		std::cerr << "Failed to draw temperature box or text: " << weatherData.temp << std::endl;
	}
	if (day_str && date_str) {
    rgb_matrix::DrawText(staticFrame, tempFont, RIGHT_PANEL_X + 6, 50,
                         clockColor, nullptr, day_str);
    rgb_matrix::DrawText(staticFrame, tempFont, RIGHT_PANEL_X + 6, 62,
                         clockColor, nullptr, date_str);
	}







}

// --- Main Program ---
int main(int argc, char* argv[]) {
	std::cerr << "Entered main()\n";
    RGBMatrix::Options defaults;
    defaults.rows = 64;
    defaults.cols = 64;
    defaults.chain_length = 2;
    defaults.parallel = 1;
	
	std::string lastDayStr, lastDateStr;

	
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

    std::string api_key = "YOUR API KEY";
    std::string city = "Los%20Angeles,US";
	std::string lat = "34.078";
	std::string lon = "-118.260";
    Units units = Units::Imperial;

    WeatherData weatherData;
    time_t lastWeatherUpdate = 0;
	
    // Pre-render icons once
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
    std::cerr << "Failed to load png: " << std::endl;
	}

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
		
		std::string currentDayStr(day_str);
		std::string currentDateStr(date_str);
		//std::cerr << "Check Dates: " << time_str << std::endl;
		bool dateChanged = (currentDayStr != lastDayStr || currentDateStr != lastDateStr);

		
        // Update weather every 15 minutes
        if (difftime(now, lastWeatherUpdate) > 900 || weatherData.temp.empty() || dateChanged) {
			//std::cerr << "Update Weather: " << time_str << std::endl;
            std::string weatherJson = GetWeather(lat, lon, api_key, units);
            weatherData = ParseWeather(weatherJson, units);
			try {
				float tempF = GetCurrentTempFromOpenMeteo(lat, lon);  // Los Angeles
				std::ostringstream oss;
				oss << std::fixed << std::setprecision(1) << tempF << "°F";
				weatherData.temp = oss.str();

			} catch (...) {
				std::cerr << "Failed to get temp from open meteo, Using OWM " << std::endl;
			}
            lastWeatherUpdate = now;

            int hour = tm_now->tm_hour;
            bool isNight = (hour < 6 || hour >= 18);
			std::cerr << "Update static fram: " << time_str << std::endl;
            UpdateStaticFrame(staticFrame, weatherData, day_str, date_str,
                              tempFont, weatherColor, clockColor, isNight);
        }
		lastDateStr = currentDateStr;
		lastDayStr = currentDayStr;
		

        // Compose frame: copy static + overlay time
		//std::cerr << "Clear offscreen: " << time_str << std::endl;
        offscreen->Clear();
		//std::cerr << "write offscreen: " << time_str << std::endl;
        offscreen->CopyFrom(*staticFrame);
		
		
		if (strlen(time_str) == 0) {
			std::cerr << "Empty time string — skipping draw\n";
			continue;
		}
		//std::cerr << "Drawing time: " << time_str << std::endl;
		
        int time_width = rgb_matrix::DrawText(offscreen, clockFont, 0, 0,
                                              clockColor, nullptr, time_str);
        int time_x = (TOTAL_WIDTH - time_width) / 2;
		//std::cerr << "Draw Offscreen: " << time_str << std::endl;
        rgb_matrix::DrawText(offscreen, clockFont, time_x, 20,
                             clockColor, nullptr, time_str);
		
		//std::cerr << "Swap Frame: " << time_str << std::endl;
        // Swap completed frame
        offscreen = matrix->SwapOnVSync(offscreen);

        usleep(1000 * 1000); // refresh once per second
    }

    return 0;
}
   