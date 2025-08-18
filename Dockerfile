FROM scottyhardy/docker-wine:stable

RUN wget https://github.com/Schwungus/nutpunch/releases/download/stable/nutpuncher-stable-windows.exe -O /usr/local/bin/nutpuncher.exe
EXPOSE 30001/udp

CMD ["/usr/bin/env", "wine", "/usr/local/bin/nutpuncher.exe"]
