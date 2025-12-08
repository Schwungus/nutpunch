FROM alpine:3.23

RUN apk add --no-cache wget \
    && mkdir -p /usr/local/bin \
    && wget https://github.com/Schwungus/nutpunch/releases/download/stable/nutpuncher-Linux-stable \
        -O /usr/local/bin/nutpuncher \
    && chmod +x /usr/local/bin/nutpuncher \
    && apk del wget
EXPOSE 30001/udp

CMD ["/usr/local/bin/nutpuncher"]
