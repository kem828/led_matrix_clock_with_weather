# led_matrix_clock_with_weather
Clock with weather display designed to be run on a raspberry pi (tested on 3B) to a 128x64 matrix array

Built on https://github.com/hzeller/rpi-rgb-led-matrix


Gets local weather based on lat\lon from OpenWeather (Need free API key)
![alt text](https://github.com/kem828/led_matrix_clock_with_weather/blob/main/led_clock.jpg?raw=true "Forgive the colors, my camera isn't very good")

**Install Dependencies**

```bash
sudo apt install build-essential git libcurl4-openssl-dev nlohmann-json-dev
```

Build rpi-rgb-led-matrix

You can probably just build the examples, so follow the instructions at [https://github.com/hzeller/rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) to do that
(But you probably already have if you're here)

Get lodepng
```bash
cd ./lodepng
curl -O https://raw.githubusercontent.com/lvandeve/lodepng/master/lodepng.h
curl -O https://raw.githubusercontent.com/lvandeve/lodepng/master/lodepng.cpp
```

Edit the makefile with your dependency locations

Add your Open Weather API Key and change your weather location
```c++
std::string api_key = "API_KEY_HERE";
```

Build the clock
```bash
make
```

Run it!
```bash
./clock
```

This takes the rpi-rgb-led-matrix parameters, so if you are using a chain of 2 64x64 displays for example:
```bash
sudo ./clock --led-rows=64 --led-cols=64 --led-chain=2
```

Any display related issues, you'll have more luck at https://github.com/hzeller/rpi-rgb-led-matrix


All icons by https://www.instagram.com/maxhollingsheadart

Icons can be replaced either by 32x32 pngs in the icon folder, or you can change them to use simple shape drawn images (See the "Snow" weather in the code for an example of that)

keinan@keinanmarks.com
