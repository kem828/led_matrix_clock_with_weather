# led_matrix_clock_with_weather
Clock with weather display designed to be run on a raspberry pi (tested on 3B) to a 128x64 matrix array

Built on https://github.com/hzeller/rpi-rgb-led-matrix


Gets local weather based on lat\lon from OpenWeather (Need free API key)
![alt text](https://github.com/kem828/led_matrix_clock_with_weather/blob/main/led_clock.jpg?raw=true "Clock")

**Install Dependencies**

```bash
sudo apt install build-essential git libcurl4-openssl-dev nlohmann-json-dev
```

Build rpi-rgb-led-matrix

You can probably just build the examples, so follow the instructions at https://github.com/hzeller/rpi-rgb-led-matrix to do that
(But you probably already have if you're here)

Get lodepng
```bash
cd ./lodepng
curl -O https://raw.githubusercontent.com/lvandeve/lodepng/master/lodepng.h
curl -O https://raw.githubusercontent.com/lvandeve/lodepng/master/lodepng.cpp
```

Edit the makefile with your depency locations

Add your Open Weather API Key and change your weather location
```c++
std::string api_key = "API_KEY_HERE";
```
