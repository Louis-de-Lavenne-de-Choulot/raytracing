# Compiler and flags
CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -O3 -I./sdl2/include -L./sdl2/lib -lmingw32 -lSDL2main -lSDL2 -static-libgcc -static-libstdc++ -Wall -Werror
SOURCES = $(wildcard *.cpp)  # All .cpp files in the current directory
EXECUTABLE = sdl_raytracing.exe

# Default target
all: $(EXECUTABLE)

# Link the executable
$(EXECUTABLE): $(SOURCES)
	$(CXX) $(SOURCES) -o $@ $(CXXFLAGS)

# Clean up build artifacts
clean:
	rm -f $(EXECUTABLE)