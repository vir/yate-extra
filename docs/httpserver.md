httpserver module
=================

This module allows Yate server to process HTTP and HTTPS requests from web
browsers and other HTTP-enabled software. Incoming request is decoded and
transformed into a serie of Yate's messages. Other modules (like
[webserver](webserver.md) or websocket) can then process this messages and
procuce some response data.

## Configuration
Module has it's own configuration file [httpserver.conf](../httpserver.conf).
See comments inside for more info.


## Operation
Upon receiving of HTTP request, module issues several messages, and depending
on results produces response.

First of all, after parsin of request headers, __http.route__ message is
dispatched with the following parameters:

| Parameter | Description                                    |
|-----------|------------------------------------------------|
| server    | name of listener (section of [httpserver.conf](../httpserver.conf)) serving request |
| address   | remote address of client connection in form of _ip_:_port_ |
| local     | local address of client connection             |
| keepalive | boolean value, indicating that connection will not be closed after processiong of this request |
| reqbody   | boolean value, indicating that request body is expected in this request |
| version   | HTTP version                                   |
| method    | HTTP request method                            |
| uri       | request uri string                             |
| hdr_Xxxxx | incoming request headers                       |

If this __http.route__ message is handled, it's return value is added to
subsequent messages as _handler_ parameter. All other paramters are passed to
all subsequent messages unchanged.

If connection upgrade is offered, __http.upgrade__ message is dispatched. If
it is handled, "101 Switching protocols" response is produced and then run()
method of [Runnable](http://yate.null.ro/docs/api/TelEngine__Runnable.html)
object, extracted from response's _userData_, is called. When it returns,
connection is closed. Handling module should store reference to Socket object,
taken from __http.upgrade__ message and replace _userData_ with it's Runnable
reference. Example can be found in [websocket](../websocket.cpp) module.

Then __http.preserve__ message is dispatched, allowing handlers to process
request body. See example in [webserver](../webserver.cpp) module. If that
message is not handled, temporal request body buffer is created. Then request
body is read from network.

Finally, __http.serve__ message is dispatched. If temp. request buffer was
used, it's content is added as _content_ paramter. It that message is not
handled, "404 Not found" error response is produced and client connection is
closed.

Handled __http.serve__ message is used to construct HTTP response. Status code
is taken from _status_ parameter and response headers are build trom _ohdr*_
parameters.

If __http.serve__ message's _retValue_ is not empty, it is used as HTTP
response body. Otherwise, _userData_ of response is queried for
[Stream](http://yate.null.ro/docs/api/TelEngine__Stream.html) and
[RefObject](http://yate.null.ro/docs/api/TelEngine__RefObject.html) objects,
that will be used to produce response body.

