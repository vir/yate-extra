/**
 * httpserver.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Generic HTTP server
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <yateclass.h>
#include <yatephone.h>
#include <yatemime.h> // for getUnfoldedLine
#include <string.h>
#include <stdio.h> // for snprintf
#include <stdlib.h> // for atoi

/**
 * Message http.preserve is dispatched after request headers is received.
 * Message http.serve is dispatched after whole request has been read.
 */

#define HDR_BUFFER_SIZE 2048
#define BODY_BUF_SIZE 4096
#ifndef min
# define min(a,b) ((a)<(b)?(a):(b))
#endif

using namespace TelEngine;
namespace { // anonymous

static Mutex s_mutex(true, "HTTPServer");

static TokenDict s_httpResponseCodes[] = {
    { "Continue", 100 },
    { "Switching Protocols", 101 },
    { "OK", 200 },
    { "Created", 201 },
    { "Accepted", 202 },
    { "Non-Authoritative Information", 203 },
    { "No Content", 204 },
    { "Reset Content", 205 },
    { "Partial Content", 206 },
    { "Multiple Choices", 300 },
    { "Moved Permanently", 301 },
    { "Found", 302 },
    { "See Other", 303 },
    { "Not Modified", 304 },
    { "Use Proxy", 305 },
    { "Temporary Redirect", 307 },
    { "Bad Request", 400 },
    { "Unauthorized", 401 },
    { "Payment Required", 402 },
    { "Forbidden", 403 },
    { "Not Found", 404 },
    { "Method Not Allowed", 405 },
    { "Not Acceptable", 406 },
    { "Proxy Authentication Required", 407 },
    { "Request Timeout", 408 },
    { "Conflict", 409 },
    { "Gone", 410 },
    { "Length Required", 411 },
    { "Precondition Failed", 412 },
    { "Request Entity Too Large", 413 },
    { "Request-URI Too Long", 414 },
    { "Unsupported Media Type", 415 },
    { "Requested Range Not Satisfiable", 416 },
    { "Expectation Failed", 417 },
    { "Server Internal Error", 500 },
    { "Not Implemented", 501 },
    { "Bad Gateway", 502 },
    { "Service Unavailable", 503 },
    { "Gateway Timeout", 504 },
    { "HTTP Version Not Supported", 505 },
    { 0, 0 },
};

//we gonna create here the list with all the new connections.
static ObjList s_connList;

// the incomming connections listeners list
static ObjList s_listeners;

class YHttpMessage;
class YHttpRequest;
class YHttpResponse;

class Connection;
class HTTPServerThread;

class BodyBuffer: public RefObject, public MemoryStream
{
public:
    BodyBuffer(const TelEngine::String& string)
	: MemoryStream(DataBlock(const_cast<char*>(string.c_str()), string.length()))
	{ }
    BodyBuffer(const DataBlock& data)
	: MemoryStream(data)
	{ }
    BodyBuffer(unsigned int length)
	{ m_data.resize(length); }
    BodyBuffer()
	{ }
    DataBlock& data()
	{ return m_data; }
};

class YHttpMessage: public RefObject
{
    YNOCOPY(YHttpMessage); // no automatic copies please
    friend class Connection;
public:
    YHttpMessage();
    const static unsigned long UnknownLength = -1UL;
    virtual ~YHttpMessage();
    Connection* connection()
	{ return m_conn; }
    void connection(Connection* conn);
    unsigned int contentLength() const
	{ return m_contentLength; }
    void contentLength(unsigned int cl)
	{ m_contentLength = cl; }
    const NamedList& headers() const
	{ return m_headers; }
    template<typename T>
    void setHeader(const char* name, T value)
	{ m_headers.setParam(name, value); }
    template<typename T>
    void addHeader(const char* name, T value)
	{ m_headers.setParam(name, value); }
    String getHeader(const char* name) const
	{ return m_headers.getValue(name); }
    bool hasHeader(const char* name) const
	{ return NULL != m_headers.getParam(name); }
    const String& httpVersion() const
	{ return m_httpVersion; }
    void httpVersion(const String& v)
	{ m_httpVersion = v; }
    void setBody(const DataBlock& body)
    {
	BodyBuffer* b = new BodyBuffer(body);
	setBody(b, b);
	b->deref();
	contentLength(body.length());
    }
    void setBody(const String& body)
    {
	BodyBuffer* b = new BodyBuffer(body);
	setBody(b, b);
	b->deref();
	contentLength(body.length());
    }
    void setBody(TelEngine::Stream* strm, TelEngine::RefObject* ref)
	{ m_bodyStream = strm; m_bodyObjectRef = ref; }
    Stream* bodyStream() const
	{ return m_bodyStream; }
private:
    NamedList m_headers;
    unsigned int m_contentLength;
    //RefPointer<Connection> m_conn;
    Connection* m_conn;
    String m_httpVersion;
    TelEngine::Stream* m_bodyStream;
    TelEngine::RefPointer<TelEngine::RefObject> m_bodyObjectRef;
};

class YHttpRequest: public YHttpMessage
{
    YNOCOPY(YHttpRequest); // no automatic copies please
public:
    YHttpRequest(Connection* conn = NULL);
    String m_method, m_uri;
public:
    bool parse(const char* buf, int len);
    void fill(TelEngine::Message& m);
    bool bodyExpected() const;
private:
    bool parseFirst(String& line);
};

class YHttpResponse: public YHttpMessage
{
    YNOCOPY(YHttpResponse); // no automatic copies please
public:
    YHttpResponse(Connection* conn = NULL);
    int status() const
	{ return m_rc; }
    void status(int rc)
	{ m_rc = rc; m_statusText = lookup(m_rc, s_httpResponseCodes); }
    String& statusText()
	{ return m_statusText; }
    void update(const Message& msg);
    bool build(DataBlock& buf);
public:
    int m_rc;
    String m_statusText;
};

class SockRef : public RefObject
{
public:
    inline SockRef(Socket** sock)
	: m_sock(sock)
	{ }
    void* getObject(const String& name) const
    {
	if (name == YATOM("Socket*"))
	    return m_sock;
	return RefObject::getObject(name);
    }
private:
    Socket** m_sock;
};

class HTTPServerListener : public RefObject
{
    friend class HTTPServerThread;
public:
    inline HTTPServerListener(const NamedList& sect)
	: m_cfg(sect)
	{ }
    ~HTTPServerListener();
    void init();
    inline NamedList& cfg()
	{ return m_cfg; }
    const String& address() const
	{ return m_address; }
private:
    void run();
    bool initSocket();
    Connection* checkCreate(Socket* sock, const char* addr);
    NamedList m_cfg;
    Socket m_socket;
    String m_address;
};

class HTTPServerThread : public Thread
{
public:
    inline HTTPServerThread(HTTPServerListener* listener)
	: Thread("HTTPServer Listener"), m_listener(listener)
	{ }
    virtual void run()
	{ m_listener->run(); }
private:
    RefPointer<HTTPServerListener> m_listener;
};

class Connection: public RefObject, public Thread
{
public:
    enum ConnToken {
	KeepAlive = 1,
	Close = 2,
	TE = 3,
	Trailers = 4,
	Upgrade = 8,
    };
private:
    static TokenDict s_connTokens[];
    void connectionHeader(const char* hdr);
    String connectionHeader();
public:
    Connection(Socket* sock, const char* addr, HTTPServerListener* listener);
    ~Connection();

    virtual void* getObject (const String& name) const;
    virtual void run();
    void runConnection();
    inline const String& address() const
	{ return m_address; }
    inline const NamedList& cfg() const
	{ return m_listener->cfg(); }
    void checkTimer(u_int64_t time);
private:
    bool received(unsigned long rlen);
    bool readRequestBody(Message& msg);
    bool sendResponse(YHttpResponse& rsp);
    bool sendErrorResponse(int code);
    bool sendData(unsigned int length, unsigned int offset = 0);
    void appendMissingErrorResponseBody(YHttpResponse& rsp);
private:
    Socket* m_socket;
    DataBlock m_rcvBuffer;
    DataBlock m_sndBuffer;
    String m_address;
    RefPointer<HTTPServerListener> m_listener;
    RefPointer<YHttpRequest> m_req;
    RefPointer<YHttpResponse> m_rsp;
    bool m_keepalive;
    unsigned int m_maxRequests;
    unsigned int m_maxReqBody;
    unsigned int m_maxSendChunkSize;
    unsigned int m_timeout;
    int/*ConnToken*/ m_connection;
};

class HTTPServer : public Plugin
{
public:
    HTTPServer();
    ~HTTPServer();
    virtual void initialize();
    virtual bool isBusy() const;
private:
    bool m_first;
};

YHttpMessage::YHttpMessage()
    : m_headers("HttpHeaders")
    , m_contentLength(UnknownLength)
    , m_conn(NULL)
    , m_httpVersion("1.0")
    , m_bodyStream(NULL)
{
}

YHttpMessage::~YHttpMessage()
{
}

void YHttpMessage::connection(Connection* conn)
{
    m_conn = conn;
    XDebug(DebugAll,"YHttpMessage[%p]::connection(%p)",this,conn);
}

// XXX from modules/ysipchan.cpp

// Find an empty line in a buffer
// Return the position past it or buffer length + 1 if not found
// NOTE: returned value may be buffer length
static inline unsigned int getEmptyLine(const char* buf, unsigned int len)
{
    int count = 0;
    unsigned int i = 0;
    for (; count < 2 && i < len; i++) {
	if (buf[i] == '\r') {
	    i++;
	    if (i < len && buf[i] == '\n')
		count++;
	    else
		count = 0;
	}
	else if (buf[i] == '\n')
	    count++;
	else
	    count = 0;
    }
    return (count == 2) ? i : len + 1;
}

YHttpRequest::YHttpRequest(Connection* conn /* = NULL*/)
{
    if(conn)
	connection(conn);
}

void YHttpRequest::fill(Message& m)
{
    m.addParam("version", httpVersion());
    m.addParam("method", m_method);
    m.addParam("uri", m_uri);
    unsigned int n = headers().length();
    for (unsigned int j = 0; j < n; j++) {
	const NamedString* hdr = headers().getParam(j);
	if (! hdr)
	    continue;
	m.addParam("hdr_" + hdr->name(), *hdr);
    }
}

bool YHttpRequest::bodyExpected() const
{
    if (m_method == YSTRING("TRACE"))
	return false;
    if (hasHeader("Transfer-Encoding") || hasHeader("Content-Length"))
	return true;
    if (m_method == YSTRING("POST") || m_method == YSTRING("PUT"))
	return true;
    return false;
}

bool YHttpRequest::parseFirst(String& line)
{
    XDebug(DebugAll,"YHttpRequest[%p]::parse firstline= '%s'",this,line.c_str());
    if (line.null())
	return false;
    static Regexp r2("^\\([[:alpha:]]\\+\\)[[:space:]]\\+\\([^[:space:]]\\+\\)[[:space:]]\\+[Hh][Tt][Tt][Pp]/\\([0-9]\\.[0-9]\\+\\)$");
    if (! line.matches(r2)) {
	Debug(DebugInfo,"Invalid first line '%s'",line.c_str());
	return false;
    }
    // Request: <method> <uri> <version>
    m_method = line.matchString(1).toUpper();
    m_uri = line.matchString(2);
    httpVersion(line.matchString(3).toUpper());
    DDebug(DebugAll,"YHttpRequest[%p] got request method='%s' uri='%s' version='%s'",
	this, m_method.c_str(), m_uri.c_str(), httpVersion().c_str());
    return true;
}

bool YHttpRequest::parse(const char* buf, int len)
{
    DDebug(DebugAll,"YHttpRequest[%p]::parse(%p,%d)",this,buf,len);
    XDebug(DebugAll,"Request to parse: %s", String(buf,len).c_str());
    String* line = MimeBody::getUnfoldedLine(buf, len);
    if (line->null())
	return false;
    if (!parseFirst(*line)) {
	line->destruct();
	return false;
    }
    line->destruct();
    while (len > 0) {
	line = MimeBody::getUnfoldedLine(buf,len);
	if (line->null()) {
	    // Found end of headers
	    line->destruct();
	    break;
	}
	int col = line->find(':');
	if (col <= 0) {
	    line->destruct();
	    return false;
	}
	String name = line->substr(0,col);
	name.trimBlanks();
	if (name.null()) {
	    line->destruct();
	    return false;
	}
	*line >> ":";
	line->trimBlanks();
	XDebug(DebugAll,"YHttpRequest[%p]::parse header='%s' value='%s'",this,name.c_str(),line->c_str());

#if 0
	if ((name &= "WWW-Authenticate") ||
	    (name &= "Proxy-Authenticate") ||
	    (name &= "Authorization") ||
	    (name &= "Proxy-Authorization"))
	    header.append(new MimeAuthLine(name,*line));
#endif
	addHeader(name,*line);

	if ((contentLength() == UnknownLength) && (name &= "Content-Length"))
	    contentLength(line->toLong(-1,10));
	line->destruct();
    }
    if (contentLength() == UnknownLength) { // try to determine boly length
	if(strcmp(httpVersion(), "1.0") > 0) {
	    if(! hasHeader("Transfer-Encoding")) // HTTP1.1: no Transfer-Encoding nor Content-Length => no body
		contentLength(0);
	}
	else if(m_method == YSTRING("GET") || m_method == YSTRING("HEAD")) // HTTP1.0
	    contentLength(0);
    }
    DDebug(DebugAll,"YHttpRequest[%p]::parse %d header lines, body %u bytes", this, headers().count(), contentLength());
    return true;
}


YHttpResponse::YHttpResponse(Connection* conn /* = NULL*/)
{
    if(conn)
	connection(conn);
}

void YHttpResponse::update(const Message& msg)
{
    contentLength(UnknownLength);
    status(msg.getIntValue("status", 200));
    String prefix = msg.getValue("ohdr_prefix", "ohdr_");
    unsigned int n = msg.length();
    for (unsigned int j = 0; j < n; j++) {
	const NamedString* hdr = msg.getParam(j);
	if (! hdr)
	    continue;
	String tmp = hdr->name();
	if (! tmp.startSkip(prefix, false))
	    continue;
	if (tmp == YSTRING("Content-Length")) {
	    contentLength(hdr->toLong(UnknownLength));
	    continue;
	}
	setHeader(tmp, hdr->c_str());
    }
}

bool YHttpResponse::build(DataBlock& buf)
{
    XDebug(DebugAll,"YHttpResponse[%p]::build: httpVersion=%s status=%d, text='%s'", this, httpVersion().c_str(), status(), m_statusText.c_str());
    String firstLine("HTTP/");
    firstLine << httpVersion() << " " << status() << " " << m_statusText << "\r\n";
    unsigned int n = headers().length();
    for (unsigned int j = 0; j < n; j++) {
	const NamedString* hdr = headers().getParam(j);
	if (! hdr)
	    continue;
	String tmp;
	MimeHeaderLine mhl(hdr->name(), *hdr);
	mhl.buildLine(tmp);
	tmp << "\r\n";
	firstLine << tmp;
    }
    buf.clear();
    buf.append(firstLine);
    buf.append("\r\n");
    return true;
}

/**
 * HTTPServerListener
 */
HTTPServerListener::~HTTPServerListener()
{
    DDebug("HTTPServer",DebugInfo,"No longer listening '%s' on %s",
	m_cfg.c_str(),m_address.c_str());
    s_mutex.lock();
    s_listeners.remove(this,false);
    s_mutex.unlock();
}

void HTTPServerListener::init()
{
    if (initSocket()) {
	s_mutex.lock();
	s_listeners.append(this);
	s_mutex.unlock();
    }
    deref();
}

bool HTTPServerListener::initSocket()
{
    // check configuration
    int port = m_cfg.getIntValue("port",5038);
    const char* host = c_safe(m_cfg.getValue("addr","127.0.0.1"));
    if (!(port && *host))
	return false;

    m_socket.create(AF_INET, SOCK_STREAM);
    if (!m_socket.valid()) {
	Alarm("HTTPServer","socket",DebugGoOn,"Unable to create the listening socket: %s",
	    strerror(m_socket.error()));
	return false;
    }

    if (!m_socket.setBlocking(false)) {
	Alarm("HTTPServer","socket",DebugGoOn, "Failed to set listener to nonblocking mode: %s",
	    strerror(m_socket.error()));
	return false;
    }

    SocketAddr sa(AF_INET);
    sa.host(host);
    sa.port(port);
    m_address << sa.host() << ":" << sa.port();
    m_socket.setReuse();
    if (!m_socket.bind(sa)) {
	Alarm("HTTPServer","socket",DebugGoOn,"Failed to bind to %s : %s",
	    m_address.c_str(),strerror(m_socket.error()));
	return false;
    }
    if (!m_socket.listen(2)) {
	Alarm("HTTPServer","socket",DebugGoOn,"Unable to listen on socket: %s",
	    strerror(m_socket.error()));
	return false;
    }
    Debug("HTTPServer",DebugInfo,"Starting listener '%s' on %s",
	m_cfg.c_str(),m_address.c_str());
    HTTPServerThread* t = new HTTPServerThread(this);
    if (t->startup())
	return true;
    delete t;
    return false;
}

void HTTPServerListener::run()
{
    for (;;)
    {
	Thread::idle(true);
	SocketAddr sa;
	Socket* as = m_socket.accept(sa);
	if (!as) {
	    if (!m_socket.canRetry())
		Debug("HTTPServer",DebugWarn, "Accept error: %s",strerror(m_socket.error()));
	    continue;
	} else {
	    String addr(sa.host());
	    addr << ":" << sa.port();
	    if (!checkCreate(as,addr))
		Debug("HTTPServer",DebugWarn,"Connection rejected for %s",addr.c_str());
	}
    }
}

Connection* HTTPServerListener::checkCreate(Socket* sock, const char* addr)
{
    if (!sock->valid()) {
	delete sock;
	return 0;
    }

    int arg = 1;
    if (m_cfg.getBoolValue("nodelay",true) &&
	    !sock->setOption(IPPROTO_TCP, TCP_NODELAY, &arg, sizeof(arg)))
	Debug("HTTPServer",DebugMild, "Failed to set tcp socket to TCP_NODELAY mode: %s", strerror(sock->error()));

    const NamedString* secure = m_cfg.getParam("sslcontext");
    if (TelEngine::null(secure))
	secure = 0;
    if (secure) {
	Message m("socket.ssl");
	m.addParam("server",String::boolText(true));
	m.addParam("context",*secure);
	m.copyParam(m_cfg,"verify");
	SockRef* s = new SockRef(&sock);
	m.userData(s);
	TelEngine::destruct(s);
	if (!(Engine::dispatch(m) && sock)) {
	    Debug("HTTPServer",DebugWarn, "Failed to switch '%s' to SSL for %s '%s'",
		cfg().c_str(),secure->name().c_str(),secure->c_str());
	    delete sock;
	    return 0;
	}
    }
    else if (!sock->setBlocking(false)) {
	Debug("HTTPServer",DebugGoOn, "Failed to set tcp socket to nonblocking mode: %s",
	    strerror(sock->error()));
	delete sock;
	return 0;
    }
    // should check IP address here
    Output("Remote%s connection from %s to %s",
	(secure ? " secure" : ""),addr,m_address.c_str());
    Connection* conn = new Connection(sock,addr,this);
    if (conn->error()) {
	conn->deref();
	return 0;
    }
    conn->startup();
    return conn;
}

/**
 * Connection
 */
TokenDict Connection::s_connTokens[] = {
    { "keep-alive", KeepAlive },
    { "close",     Close },
    { "te",        TE },
    { "trailers",  Trailers },
    { "upgrade",  Upgrade },
    { 0, 0 },
};

Connection::Connection(Socket* sock, const char* addr, HTTPServerListener* listener)
    : Thread("HTTPServer connection"),
      m_socket(sock),
      m_address(addr), m_listener(listener),
      m_keepalive(false),
      m_maxRequests(0),
      m_timeout(10)
{
    s_mutex.lock();
    s_connList.append(this);
    s_mutex.unlock();
    m_maxRequests = cfg().getIntValue("maxrequests", 0);
    m_maxReqBody = cfg().getIntValue("maxreqbody", 10 * 1024);
    m_timeout = cfg().getIntValue("timeout", 10);
    m_maxSendChunkSize = cfg().getIntValue("maxsendchunk", 8192);
    if (m_maxSendChunkSize < 10)
	m_maxSendChunkSize = 10;
    else if (m_maxSendChunkSize > 65535)
	m_maxSendChunkSize = 65535; // need to fit into 4 hex digits
}

Connection::~Connection()
{
    s_mutex.lock();
    s_connList.remove(this,false);
    s_mutex.unlock();
    Output("Closing connection to %s",m_address.c_str());
    delete m_socket;
    m_socket = 0;
}

void* Connection::getObject(const String& name) const
{
    XDebug(DebugAll,"Connection[%p]::getObject('%s')", this, name.c_str());
    if (name == YATOM("Connection"))
	return const_cast<Connection*>(this);
    if (name == YATOM("YHttpRequest"))
	return m_req;
    if (name == YATOM("YHttpResponse"))
	return m_rsp;
    if (name == YATOM("Socket"))
	return m_socket;
    if (name == YATOM("HTTPServerListener"))
	return m_listener;
    return GenObject::getObject(name);
}

void Connection::run()
{
    if (!m_socket)
	return;
    runConnection();
    deref();
}

void Connection::runConnection()
{
    u_int32_t killtime = Time::secNow() + m_timeout;
    while (m_socket && m_socket->valid()) {
	Thread::check();
	bool readok = false;
	bool error = false;
	if (m_socket->select(&readok, 0, &error, 10000)) {
	    if (error) {
		Debug("HTTPServer",DebugInfo,"Socket exception condition on %d",m_socket->handle());
		/* Can happen when client shuts down it's socket's sending part */
		if(m_keepalive)
		    return;
	    }
	    if (!readok) {
		if(!m_timeout || Time::secNow() < killtime) {
		    yield();
		    continue;
		}
		Debug("HTTPServer",DebugAll, "Timeout waiting for socket %d", m_socket->handle());
		return;
	    }
	    DataBlock rbuf(NULL, HDR_BUFFER_SIZE);
	    int readsize = m_socket->readData(rbuf.data(), rbuf.length());
	    if (!readsize) {
		Debug("HTTPServer",DebugInfo,"Socket condition EOF on %d",m_socket->handle());
		return;
	    }
	    else if (readsize > 0) {
		m_rcvBuffer.append(rbuf.data(), readsize);
		if (! received(readsize))
		    return;
		killtime = Time::secNow() + m_timeout;
	    }
	    else if (!m_socket->canRetry()) {
		Debug("HTTPServer",DebugWarn,"Socket read error %d on %d",errno,m_socket->handle());
		return;
	    }
	}
	else if (!m_socket->canRetry()) {
	    Debug("HTTPServer",DebugWarn,"socket select error %d on %d",errno,m_socket->handle());
	    return;
	}
    }
}

bool Connection::received(unsigned long rlen)
{
    const char * data = (const char*)m_rcvBuffer.data();
    unsigned int len = m_rcvBuffer.length();
    // Find an empty line
    unsigned int bodyOffs = getEmptyLine(data, len);
    if (bodyOffs > len)
	return true; // not enouth data, but still ok

    // Got all headers, start processing request
    m_req = new YHttpRequest(this);
    m_req->deref();

    // Parse the message headers
    if(! m_req->parse(data, bodyOffs)) {
	String tmp(data, bodyOffs);
	Debug("HTTPServer", DebugNote,
	    "got invalid message [%p]\r\n------\r\n%s\r\n------",
	    this, tmp.c_str());
	return false;
    }
    if(strcmp(m_req->httpVersion(), "1.0") > 0)
	m_keepalive = true;
    connectionHeader(m_req->getHeader("Connection"));
    Debug("HTTPServer", DebugAll, "Connection flags: %04X", m_connection);
    if(m_connection & KeepAlive)
	m_keepalive = true;
    if(m_connection & Close)
	m_keepalive = false;

    // Remove processed part from input buffer
    m_rcvBuffer.cut(-bodyOffs); // now m_rcvBuffer holds body's beginning

    Message m("http.route");
    m.userData(this);
    m.addParam("server", m_listener->cfg().c_str());
    m.addParam("address", m_address);
    m.addParam("local", m_listener->address());
    m.addParam("keepalive", String::boolText(m_keepalive));
    m_req->fill(m);
    if (Engine::dispatch(m)) {
	TelEngine::String rv = m.retValue();
	if (rv[0] >= '3' && rv[0] <= '9')
	    return sendErrorResponse(atoi(rv.c_str())); // XXX TODO add headers from m
	m.addParam("handler", rv);
	m.retValue() = TelEngine::String::empty();
    }

    if (m_connection & Upgrade && m_req->hasHeader("Upgrade")) {
	m = "http.upgrade";
	if (Engine::dispatch(m)) {
	    RefPointer<RefObject> ref = static_cast<RefObject*>(m.userObject("RefObject"));
	    Runnable* code = static_cast<Runnable*>(m.userObject("Runnable"));
	    XDebug("HTTPServer",DebugAll,"Connection[%p] got http.upgrade Runnable response %p", this, code);
	    if (code) {
		m_rsp = new YHttpResponse(this);
		m_rsp->httpVersion(m_req->httpVersion());
		m_rsp->update(m);
		m_rsp->addHeader("Connection", "Upgrade");
		m_rsp->addHeader("Upgrade", "websocket");
		m_rsp->status(101);
		m_rsp->contentLength(0);
		XDebug("HTTPServer",DebugAll,"Connection[%p]: sending 101 response %p", this, (YHttpResponse*)m_rsp);
		if(! sendResponse(*m_rsp))
		    return false;
		XDebug("HTTPServer",DebugAll,"Connection[%p]: sent 101 response %p", this, (YHttpResponse*)m_rsp);
		m_req = NULL;
		m_rsp = NULL;
		code->run();
		XDebug("HTTPServer",DebugAll,"Connection[%p]: done with upgraded connection", this);
	    }
	    return false;
	}
	else {
	    //m.delHeader("Upgrade");
	    m_connection &= ~Upgrade;
	}
    }

    // Dispatch http.prereq in case someone wants to read request body
    m = "http.preserve";
    if (Engine::dispatch(m)) {
	TelEngine::Stream* strm = reinterpret_cast<TelEngine::Stream*>(m.userObject(YATOM("Stream")));
	if(strm) {
	    TelEngine::RefObject* ref = reinterpret_cast<TelEngine::RefObject*>(m.userObject("RefObject"));
	    XDebug("HTTPServer",DebugInfo,"Connection[%p] got stream response %p, ref %p", this, strm, ref);
	    m_req->setBody(strm, ref);
	}
    }

    // if noone wants to read request body, lets prepare our own buffer
    BodyBuffer* request_body_buffer = NULL;
    if (! m_req->bodyStream() && m_req->bodyExpected()) {
	request_body_buffer = new BodyBuffer();
	m_req->setBody(request_body_buffer, request_body_buffer);
	request_body_buffer->deref();
    }

    // read request body finally
    if (m_req->bodyExpected() && ! readRequestBody(m))
	return false; // error response is already sent in readRequestBody()

    m_rsp = new YHttpResponse(this);
    m_rsp->httpVersion(m_req->httpVersion());

    // Dispatch http.request
    m = "http.serve";
    m.retValue().clear();
    if (request_body_buffer)
	m.setParam("content", String(reinterpret_cast<char*>(request_body_buffer->data().data()), request_body_buffer->data().length()));
    if (! Engine::dispatch(m)) {
	return sendErrorResponse(404);
    }

    // Keepalive
    m_keepalive = m.getBoolValue("keepalive", m_keepalive);
    if(! --m_maxRequests)
	m_keepalive = false;
    if(m_keepalive) {
	m_connection &= ~Close;
	m_connection |= KeepAlive;
    } else {
	m_connection &= ~KeepAlive;
	m_connection |= Close;
    }

    // Prepare response
    m_rsp->setHeader("Connection", connectionHeader());
    m_rsp->update(m);
    if (m.retValue().null() || 0 == m.retValue().length()) {
	TelEngine::Stream* strm = reinterpret_cast<TelEngine::Stream*>(m.userObject(YATOM("Stream")));
	if(strm) {
	    TelEngine::RefObject* ref = reinterpret_cast<TelEngine::RefObject*>(m.userObject("RefObject"));
	    XDebug("HTTPServer",DebugInfo,"Connection[%p] got stream response %p, ref %p", this, strm, ref);
	    m_rsp->setBody(strm, ref);
	}
	else {
	    m_rsp->contentLength(0);
	    appendMissingErrorResponseBody(*m_rsp);
	}
    }
    else {
	XDebug("HTTPServer",DebugInfo,"Connection[%p] got simple response <<%s>>", this, m.retValue().c_str());
	m_rsp->setBody(m.retValue());
    }

    // Send response
    if(! sendResponse(*m_rsp))
	return false;
    if(! m_keepalive) {
	DDebug("HTTPServer",DebugInfo,"Closing non-keepalive Connection[%p], socket %d",this,m_socket->handle());
	m_socket->shutdown(true, true);
	return false;
    }

    // Request complete
    m_req = NULL;
    m_rsp = NULL;
    return true;
}

bool Connection::readRequestBody(Message& msg)
{
    unsigned int cl = m_req->contentLength();
    bool untilEof = !m_keepalive && cl == YHttpMessage::UnknownLength; // HTTP 0.x request
    unsigned int maxBodyBuf = msg.getIntValue("maxreqbody", m_maxReqBody);
    if(cl != YHttpMessage::UnknownLength && cl > maxBodyBuf) // request body is too long
	return sendErrorResponse(413);

    TelEngine::Stream* strm = m_req->bodyStream();
    if (! strm) {
	Debug("HTTPServer",DebugWarn,"Connection[%p]: no request body buffer (socket %d)",this,m_socket->handle());
	return sendErrorResponse(500);
    }
    if (m_rcvBuffer.length()) { // body part that arrived with headers
	XDebug("HTTPServer", DebugAll, "Connection[%p]: readRequestBody: got %u bytes of body together with with headers", this, m_rcvBuffer.length());
	if(m_rcvBuffer.length() > maxBodyBuf)
	    return sendErrorResponse(413);
	strm->writeData(m_rcvBuffer);
    }

    char buf[BODY_BUF_SIZE];

    u_int32_t killtime = Time::secNow() + m_timeout;
    while(cl) {
	int r = m_socket->readData(buf, min(cl, sizeof(buf)));
	XDebug("HTTPServer", DebugAll, "Connection[%p]: readRequestBody: read %d bytes, left %d, untilEof=%s, maxBodyBuf=%u", this, r, cl, String::boolText(untilEof), maxBodyBuf);
	if(r == 0 && untilEof)
	    break;
	if(r < 0 && m_socket->canRetry() && (!m_timeout || Time::secNow() < killtime)) {
	    yield();
	    continue;
	}
	if(r <= 0)
	    return sendErrorResponse(400);

	if(strm->seek(TelEngine::Stream::SeekCurrent) + r > maxBodyBuf)
	    return sendErrorResponse(413);
	strm->writeData(buf, r);
	if(cl != YHttpMessage::UnknownLength)
	    cl -= r;
    }
    strm->terminate();
    return true;
}

bool Connection::sendData(unsigned int length, unsigned int offset /* = 0 */)
{
    unsigned char * pos = m_sndBuffer.data(offset);
    u_int32_t killtime = Time::secNow() + m_timeout;
    while (m_socket && m_socket->valid()) {
	Thread::check();
	bool writeok = false;
	bool error = false;
	if (m_socket->select(0, &writeok, &error, 10000)) {
	    if (error) {
		Debug("HTTPServer",DebugInfo,"Socket exception condition on %d",m_socket->handle());
		/* Can happen when client shuts down it's socket's sending part */
		return false; // XXX
	    }
	    if (!writeok) {
		if(!m_timeout || Time::secNow() < killtime) {
		    yield();
		    continue;
		}
		Debug("HTTPServer",DebugAll, "Timeout waiting for socket %d", m_socket->handle());
		return false;
	    }

	    unsigned int written = (unsigned int)m_socket->writeData(pos, length);
	    if (!m_socket->canRetry()) {
		Debug("HTTPServer",DebugWarn,"Socket write error %d on %d",errno,m_socket->handle());
		return false;
	    }

	    if (written) {
		length -= written;
		pos += written;
		if (0 == length)
		    return true;
		killtime = Time::secNow() + m_timeout;
	    }
	}
	else if (!m_socket->canRetry()) {
	    Debug("HTTPServer",DebugWarn,"socket select error %d on %d",errno,m_socket->handle());
	    return false;
	}
    }
    return false;
}

bool Connection::sendResponse(YHttpResponse& rsp)
{
    unsigned int to_send = rsp.contentLength();
    bool chunked = to_send == YHttpMessage::UnknownLength;

    if (chunked)
	rsp.addHeader("Transfer-Encoding", "chunked");
    else
	rsp.addHeader("Content-Length", TelEngine::String(to_send));
    if (! rsp.build(m_sndBuffer))
	return false;
    XDebug("HTTPServer",DebugInfo,"Connection[%p]::sendResponse(): chunked: %s, to_send: %u, stream: %p", this, String::boolText(chunked), to_send, rsp.bodyStream());

    if (! sendData(m_sndBuffer.length()))
	return false;
    m_sndBuffer.clear();

    if(rsp.bodyStream()) {
	m_sndBuffer.resize(m_maxSendChunkSize + 8); // 4 hex digits + crlf + data + crlf
	unsigned char * read_ptr = m_sndBuffer.data(6);
	for (;;) {
	    unsigned int to_read = m_maxSendChunkSize;
	    if (! chunked && to_send < m_maxSendChunkSize)
		to_read = to_send;
	    int rd = rsp.bodyStream()->readData(read_ptr, to_read);
	    if (! rd) {
		if (! chunked) {
		    Debug("HTTPServer",DebugInfo,"Connection[%p]::sendResponse: Socket %d: got EOF, while %u bytes more expected",this,m_socket->handle(),to_send);
		    return false;
		}
		XDebug("HTTPServer",DebugInfo,"Connection[%p]::sendResponse(): got EOF", this);
		break;
	    }
	    if (chunked) {
		snprintf((char*)m_sndBuffer.data(), 6, "%08x", rd);
		*m_sndBuffer.data(4) = '\r';
		*m_sndBuffer.data(5) = '\n';
		read_ptr[rd] = '\r';
		read_ptr[rd + 1] = '\n';
	    }
	    if (chunked) {
		if (! sendData(rd + 8))
		    return false;
		XDebug("HTTPServer",DebugInfo,"Connection[%p]::sendResponse(): sent chunk %u bytes", this, rd);
	    }
	    else {
		if (! sendData(rd, 6))
		    return false;
		to_send -= rd;
		XDebug("HTTPServer",DebugInfo,"Connection[%p]::sendResponse(): sent chunk %u bytes, %u bytes left", this, rd, to_send);
		if (! to_send)
		    break;
	    }
	}
	if (chunked) {
	    XDebug("HTTPServer",DebugInfo,"Connection[%p]::sendResponse(): sending empty chunk and empty trailer", this);
	    m_socket->writeData("0\r\n\r\n", 5);
	}
	else {
	    XDebug("HTTPServer",DebugInfo,"Connection[%p]::sendResponse(): done sending message", this);
	}
    }
    return true;
}

bool Connection::sendErrorResponse(int code)
{
    YHttpResponse e(this);
    e.setHeader("Connection", "close");
    e.status(code);
    appendMissingErrorResponseBody(e);
    sendResponse(e);
    return false;
}

void Connection::connectionHeader(const char* hdr)
{
    m_connection = 0;
    ObjList* conntokens = String(m_req->getHeader("Connection")).split(',', false);
    for (ObjList* tok = conntokens->skipNull(); tok; tok = tok->skipNext()) {
	String key = tok->get()->toString();
	m_connection |= lookup(key.trimSpaces().toLower(), s_connTokens);
    }
    TelEngine::destruct(conntokens);
}

String Connection::connectionHeader()
{
    String r;
    int flags = m_connection;
    for(int fl = 1; flags; fl <<= 1) {
	if(flags & fl) {
	    if(r.length())
		r << ",";
	    r << lookup(fl, s_connTokens);
	}
	flags &= ~fl;
    }
    return r;
}

void Connection::appendMissingErrorResponseBody(YHttpResponse& rsp)
{
    int status = rsp.status();
    if (status < 200 || status >= 600)
	return;
    String b(status);
    b << " " << rsp.statusText() << "\r\n";
    rsp.setBody(b);
    rsp.addHeader("Content-Type", "text/plain");
}

/**
 * HTTPServer
 */
HTTPServer::HTTPServer()
    : Plugin("httpserver"),
      m_first(true)
{
    Output("Loaded module HTTPServer");
}

HTTPServer::~HTTPServer()
{
    Output("Unloading module HTTPServer");
    s_connList.clear();
    s_listeners.clear();
}

bool HTTPServer::isBusy() const
{
    Lock mylock(s_mutex);
    return (s_connList.count() != 0);
}

void HTTPServer::initialize()
{
    if (m_first) {
	Output("Initializing module HTTPServer");
	Configuration cfg;
	cfg = Engine::configFile("httpserver");
	cfg.load();
	for (unsigned int i = 0; i < cfg.sections(); i++) {
	    NamedList* s = cfg.getSection(i);
	    String name = s ? s->c_str() : "";
	    if (! name.startSkip("listener ",false))
		continue;
	    name.trimBlanks();
	    s->String::operator=(name);
	    (new HTTPServerListener(*s))->init();
	}
	Lock mylock(s_mutex);
	// don't bother to install handlers until we are listening
	if (s_listeners.count()) {
	    m_first = false;
//	    Engine::self()->setHook(new RHook);
	}
    }
}

INIT_PLUGIN(HTTPServer);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
