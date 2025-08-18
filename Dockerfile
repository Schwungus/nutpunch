FROM scottyhardy/docker-wine:stable

RUN wget https://github.com/Schwungus/nutpunch/releases/download/stable/nutpuncher-stable-windows.exe -O /usr/local/bin/nutpuncher.exe
ENV USE_XVFB=yes NO_RDP=yes DUMMY_PULSEAUDIO=yes
EXPOSE 30001/udp

CMD ["/usr/bin/env", "wine", "/usr/local/bin/nutpuncher.exe"]
