# nutpunch

TODO: explain.

TODO: add a public nutpunch-server instance.

## mental(ly ill) notes

1. `WSAEMSGSIZE` (`10040`) error code is fixed by getting a message buffer of at least `NUTPUNCH_PAYLOAD_SIZE` bytes long. I don't know why the fuck the puncher server keeps sending these payloads.
