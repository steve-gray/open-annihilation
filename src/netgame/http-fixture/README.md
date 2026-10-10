# HTTP fixture

A test-only HTTP/1.1 server. The HTTP client, the catalogue and the downloader
drive it on 127.0.0.1 so a cut connection, a slow reply, a redirect or an odd
answer can be shown without leaving the machine. It is built only with the
tests, and nothing in the game links it.

Library `oa-netgame-http-fixture`, header
`include/oa/netgame/http_fixture/server.hpp`, namespace
`oa::netgame::http_fixture`.

## Entry points

- `Server` listens on 127.0.0.1. `start` without a port lets the system choose
  one; `start(port, error)` listens on that port, or on a chosen port when
  `port` is 0. `url` is `http://127.0.0.1:<port>` plus the path.
- `serve_folder` and `serve_bytes` answer GET and HEAD. `handle` answers one
  method and path. `redirect` answers with a Location header. `override_next`
  replaces the next requests to a path. Paths are matched without the query;
  the log keeps the whole target.
- `set_ranges` turns a single `bytes` range on or off. Ranges are on until a
  test turns them off. `set_idle_close_ms` sets how long a quiet connection
  stays open; it starts at 5000 milliseconds.
- `requests` and `clear_log` read and drop the log. `connections_accepted`
  counts connections from 1. `stop` joins the thread.
- The executable `oa-http-fixture` serves one folder:
  `--root DIR` (required), `--port N` (default 0), `--port-file FILE` (the
  port and a newline, written once listening), `--log FILE` (one tab-separated
  line per request: connection, method, target, Range header), `--seconds N`
  (stop after N seconds; otherwise run until SIGINT or SIGTERM). It prints
  `oa-http-fixture: serving <DIR> at http://127.0.0.1:<port>/`. Exit 0 when
  stopped, 1 when it cannot listen or write a file it was asked for, 2 for
  bad arguments.

## State

The server reads the folder it was given and the byte strings registered in
memory. It writes nothing of its own. The executable writes the port file and
the log file when those options are set.

## Invariants

The listener is 127.0.0.1 and no other address. A test that uses the server
does not open a connection anywhere else. A delay is a time at which one
connection's bytes may be written: the thread waits in short slices, so one
slow reply does not hold up the other connections, and `stop` returns without
waiting out the delay.

## Tests

`net-http-fixture` (`tests/server_test.cpp`) drives the server with the
stream sockets and checks the bytes on the connection: files and byte
strings, status 404 and 405, HEAD, keep-alive and `Connection: close`, one
range and a range that is refused or ignored, chunked framing, gzip, a
redirect, a POST handler, an override used once, a delayed and a paced
reply, a cut body, a raw reply, the idle close, the request log, and `stop`
while a delayed reply is still waiting.

## Limitations

- One range of the form `bytes=N-` or `bytes=N-M`. Several ranges, a suffix
  range and a malformed Range header are answered with the whole body.
- A request body is taken only from Content-Length, up to 4 MiB. A chunked
  request is refused. The header block, request line included, is at most
  16 KiB.
- At most 15 connections at once, because the wait watches the listener and
  15 connections together.
- No TLS.
- A handler runs on the server thread and must not call `stop` or `start`.
