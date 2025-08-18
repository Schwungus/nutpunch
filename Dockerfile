FROM scottyhardy/docker-wine:stable

ENV USE_XVFB=yes NO_RDP=yes DUMMY_PULSEAUDIO=yes
RUN wget https://github.com/Schwungus/nutpunch/releases/download/stable/nutpuncher-stable-windows.exe -O /usr/local/bin/nutpuncher.exe
EXPOSE 30001/udp

ENTRYPOINT ["/usr/bin/entrypoint"]
CMD ["wine", "/usr/local/bin/nutpuncher.exe"]
