
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libwebsockets-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY websocket_server.c .

RUN gcc -Wall -Wextra websocket_server.c -o websocket_server \
    $(pkg-config --cflags --libs libwebsockets)

ENV PORT=10000

EXPOSE 10000

CMD ["./websocket_server"]
