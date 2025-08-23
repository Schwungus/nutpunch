FROM scottyhardy/docker-wine:devel

RUN wget https://github.com/Schwungus/nutpunch/releases/download/stable/nutpuncher-stable.exe -O /usr/local/bin/nutpuncher.exe
ENV NO_RDP=yes RDP_SERVER=no DUMMY_PULSEAUDIO=yes USER_SUDO=no WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG="-all"
EXPOSE 30001/udp

ENTRYPOINT ["/usr/bin/entrypoint"]
CMD ["wine", "/usr/local/bin/nutpuncher.exe"]
