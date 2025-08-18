FROM scottyhardy/docker-wine:stable-nordp

ENV NO_RDP=yes RDP_SERVER=no DUMMY_PULSEAUDIO=yes USER_SUDO=no WINEDLLOVERRIDES="mscoree=d;mshtml=d"
RUN wget https://github.com/Schwungus/nutpunch/releases/download/stable/nutpuncher-stable-windows.exe -O /usr/local/bin/nutpuncher.exe
EXPOSE 30001/udp

ENTRYPOINT ["/usr/bin/entrypoint"]
CMD ["wine", "/usr/local/bin/nutpuncher.exe"]
