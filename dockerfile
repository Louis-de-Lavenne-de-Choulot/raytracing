# Use the official GCC image from Docker Hub
FROM gcc:latest

# Set the working directory inside the container
WORKDIR /codehere
# Update and install cross-platform build dependencies
RUN apt-get update
RUN apt-get install -y \
    mingw-w64 \
    cmake \
    nano

RUN curl -L https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-devel-2.30.11-mingw.zip -o SDL2-devel-2.30.11-mingw.zip && \
    unzip SDL2-devel-2.30.11-mingw.zip && \
    rm SDL2-devel-2.30.11-mingw.zip && \
    mv SDL2-2.30.11/x86_64-w64-mingw32 sdl2 && \
    rm -rf SDL2-2.30.11

# Command to run when starting the container (optional)
CMD ["bash"]