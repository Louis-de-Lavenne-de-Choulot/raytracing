CXX = C:/Users/LDL/Downloads/raytracing/tools/mingw64/bin/g++.exe
AR = C:/Users/LDL/Downloads/raytracing/tools/mingw64/bin/ar.exe

# Add SDL2 include path
SDL2_PATH = C:/Users/LDL/Downloads/raytracing/tools/sdl2/x64
SDL2_INCLUDE = $(SDL2_PATH)/include
SDL2_LIB = $(SDL2_PATH)/lib

CXXFLAGS = -std=c++20 \
    -I. \
    -I./include \
    -I./vcpkg/installed/x64-windows/include \
    -I"$(SDL2_INCLUDE)"

SOURCES = $(wildcard *.cpp)
# Exclude main.cpp and any other files you don't want in the library
SOURCES := $(filter-out main.cpp, $(SOURCES))
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = tools/honhengine/GameEngine.a

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $^
	del $(OBJECTS)
	@echo.
	@echo ========================================
	@echo Library created: $(TARGET)
	@echo ========================================

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

clean:
	del *.o
	del $(TARGET)

.PHONY: all clean