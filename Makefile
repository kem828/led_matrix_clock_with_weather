CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
LDFLAGS = -lpthread -lcurl
INCLUDES = -I/.../rpi-rgb-led-matrix-master/include -I/.../rpi-rgb-led-matrix-master
LIBS = -L/.../rpi-rgb-led-matrix-master/lib -lrgbmatrix

SRC = clock.cc lodepng/lodepng.cpp
OBJ = $(SRC:.cpp=.o)
BIN = clock

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(INCLUDES) $(LIBS) $(LDFLAGS)

clean:
	rm -f $(OBJ) $(BIN)
