# Base image: Ubuntu with build tools
FROM ubuntu:22.04

# Install basic development tools
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    gdb \
    make \
    netcat \
    vim \
    bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code
COPY ./src ./src
COPY ./scripts ./scripts
COPY data ./data

# Build server and client
RUN gcc -pthread -o server ./src/server.c
RUN gcc -o client ./src/client.c

CMD ["bash"]
