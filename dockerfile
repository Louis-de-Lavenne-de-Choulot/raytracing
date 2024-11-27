# Use the official GCC image from Docker Hub
FROM gcc:latest

# Set the working directory inside the container
WORKDIR /codehere

# Copy the current directory contents into the container at /usr/src/app
COPY . .

# Command to run when starting the container (optional)
CMD ["bash"]