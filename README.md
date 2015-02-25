yate-extra
==========

Several additional modules for Yate.

### g726codec
This is not my code (mostly). The original code is taken from asterisk long
time ago.

### sipdate
This module appends "Date" header to SIP registration response. Some Cisco
phones require that header.

### sysvipc
Javascript interface to System V IPC (only queues are implemented).

### httpserver
HTTP server module for Yate. It deserves it's own
[documentation](docs/httpserver.md).

### webserver
Minimalistic web server module for Yate. Requires _httpserver_ module to work.
It should have it's own [documentation](docs/webserver.md) too.

### websocket
WebSocket ([RFC6455](https://tools.ietf.org/html/rfc6455)) protocol
implementation. This module does nothing by it's own. Instead, it can be used
by other modules, willing to accept WebSocket connections. Example of such a
module can be found in [testwebsocket module](test/testwebsocket.cpp).

