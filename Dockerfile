FROM alpine:3.22

RUN apk add --no-cache wget
RUN mkdir -p /usr/local/bin \
    && wget https://github.com/Schwungus/nutpunch/releases/download/stable/nutpuncher-Linux-stable \
        -O /usr/local/bin/nutpuncher \
    && chmod +x /usr/local/bin/nutpuncher
EXPOSE 30001/udp

CMD ["/usr/local/bin/nutpuncher"]
