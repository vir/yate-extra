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
#include <yatephone.h> // for DataSource and DataConsumer
#include <yatemime.h> // for getUnfoldedLine
#include <string.h>

/**
 * Message http.prereq is dispatched after request headers is received.
 * Message http.request is dispatched after whole request has been read.
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
    { "302 Found", 302 },
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
class HttpBodySource;
class HttpBodyConsumer;

class Connection;
class HTTPServerThread;

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
	{ m_bodyBuffer = body; contentLength(m_bodyBuffer.length()); }
    void setBody(const String& body)
	{ m_bodyBuffer.assign(const_cast<char*>(body.c_str()), body.length()); contentLength(m_bodyBuffer.length()); }
    const DataBlock& bodyBuffer() const
	{ return m_bodyBuffer; }
protected:
    DataBlock m_bodyBuffer;
private:
    NamedList m_headers;
    unsigned int m_contentLength;
    //RefPointer<Connection> m_conn;
    Connection* m_conn;
    String m_httpVersion;
};

class YHttpRequest: public YHttpMessage
{
    YNOCOPY(YHttpRequest); // no automatic copies please
public:
    YHttpRequest(Connection* conn = NULL);
    String m_method, m_uri;
public:
    bool parse(const char* buf, int len);
    DataSource* source();
    void bodySource(DataSource* src)
	{ TelEngine::destruct(m_bodySource); m_bodySource = src; }
    void fill(Message& m);
private:
    bool parseFirst(String& line);
private:
    RefPointer<DataSource> m_bodySource;
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
public:
    DataConsumer& consumer();
};

class HttpBodySource: public DataSource
{
public:
    HttpBodySource(Connection& conn, DataBlock* buf = NULL);
    virtual ~HttpBodySource();
};

class HttpBodyConsumer: public DataConsumer
{
    HttpBodyConsumer(Connection& conn);
    virtual ~HttpBodyConsumer();
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

class Connection : public GenObject, public Thread
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

    virtual void run();
    inline const String& address() const
	{ return m_address; }
    inline const NamedList& cfg() const
	{ return m_listener->cfg(); }
    void checkTimer(u_int64_t time);
private:
    bool received(unsigned long rlen);
    bool readRequestBody(Message& msg, bool preDisp);
    bool sendResponse();
    bool sendErrorResponse(int code);
private:
    Socket* m_socket;
    DataBlock m_buffer;
    String m_address;
    RefPointer<HTTPServerListener> m_listener;
    YHttpRequest* m_req;
    YHttpResponse* m_rsp;
    bool m_keepalive;
    unsigned int m_maxRequests;
    unsigned int m_maxReqBody;
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
{
}

YHttpMessage::~YHttpMessage()
{
}

void YHttpMessage::connection(Connection* conn)
{
    m_conn = conn;
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
    : m_bodySource(NULL)
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

bool YHttpRequest::parseFirst(String& line)
{
    XDebug(DebugAll,"YHttpRequest::parse firstline= '%s'",line.c_str());
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
    DDebug(DebugAll,"got request method='%s' uri='%s' version='%s'",
	m_method.c_str(), m_uri.c_str(), httpVersion().c_str());
    return true;
}

bool YHttpRequest::parse(const char* buf, int len)
{
    DDebug(DebugAll,"YHttpRequest::parse(%p,%d) [%p]",buf,len,this);
    XDebug(DebugAll,"Request to parse: %s [%p]", String(buf,len).c_str(), this);
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
	XDebug(DebugAll,"YHttpRequest::parse header='%s' value='%s'",name.c_str(),line->c_str());

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
    DDebug(DebugAll,"YHttpRequest::parse %d header lines, body %u bytes", headers().count(), contentLength());
    return true;
}


YHttpResponse::YHttpResponse(Connection* conn /* = NULL*/)
{
    if(conn)
	connection(conn);
}

void YHttpResponse::update(const Message& msg)
{
    status(msg.getIntValue("status", 200));
    String prefix = msg.getValue("ohdr_prefix", "ohdr_");
    unsigned int n = msg.length();
    for (unsigned int j = 0; j < n; j++) {
	const NamedString* hdr = headers().getParam(j);
	if (! hdr)
	    continue;
	String tmp = hdr->name();
	if (! tmp.startSkip(prefix, false))
	    continue;
	if (tmp == "Content-Length")
	    continue;
	setHeader(tmp, hdr->c_str());
    }
}

bool YHttpResponse::build(DataBlock& buf)
{
    XDebug(DebugAll,"YHttpResponse::build: httpVersion=%s status=%d, text='%s'", httpVersion().c_str(), status(), m_statusText.c_str());
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
    firstLine << "Content-Length: " << contentLength() << "\r\n";
    buf.clear();
    buf.append(firstLine);
    buf.append("\r\n");
    buf.append(bodyBuffer());
    return true;
}

/*
 * HttpBodySource
 */
HttpBodySource::HttpBodySource(Connection& conn, DataBlock* buf /* = NULL*/)
{
}

HttpBodySource::~HttpBodySource()
{
}



HttpBodyConsumer::HttpBodyConsumer(Connection& conn)
{
}

HttpBodyConsumer::~HttpBodyConsumer()
{
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
	conn->destruct();
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
    { "upgraqde",  Upgrade },
    { 0, 0 },
};

Connection::Connection(Socket* sock, const char* addr, HTTPServerListener* listener)
    : Thread("HTTPServer connection"),
      m_socket(sock),
      m_address(addr), m_listener(listener),
      m_req(0), m_rsp(0),
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

void Connection::run()
{
    if (!m_socket)
	return;

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
		m_buffer.append(rbuf.data(), readsize);
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
    const char * data = (const char*)m_buffer.data();
    unsigned int len = m_buffer.length();
    // Find an empty line
    unsigned int bodyOffs = getEmptyLine(data, len);
    if (bodyOffs > len)
	return true; // not enouth data, but still ok

    // Got all headers, start processing request
    m_req = new YHttpRequest(this);

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

    // Prepare request body
    m_buffer.cut(-bodyOffs); // now m_buffer holds body's beginning
#if 0
    if(0 != m_req->contentLength())
	m_req->bodySource(new HttpBodySource(*this, &m_buffer));
#endif

    // Dispatch http.prereq
    Message m("http.prereq");
//    m.userData(this);
    m.addParam("server", m_listener->cfg().c_str());
    m.addParam("address", m_address);
    m.addParam("keepalive", String::boolText(m_keepalive));
    m_req->fill(m);
    bool preDisp = Engine::dispatch(m);

    if (! readRequestBody(m, preDisp))
	return false; // error response is already sent

    m_rsp = new YHttpResponse(this);

    // Dispatch http.request
    m = "http.request";
    m.retValue().clear();
    if (m_req->bodyBuffer().length())
	m.setParam("content", String(reinterpret_cast<char*>(m_req->bodyBuffer().data()), m_req->bodyBuffer().length()));
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
    m_rsp->setBody(m.retValue());

    if(! sendResponse())
	return false;
    if(! m_keepalive) {
	DDebug("HTTPServer",DebugInfo,"Closing non-keepalive connection %d",m_socket->handle());
	m_socket->shutdown(true, true);
	return false;
    }

    // Request complete
    TelEngine::destruct(m_req);
    TelEngine::destruct(m_rsp);
    return true;
}

bool Connection::readRequestBody(Message& msg, bool preDisp)
{
    unsigned int cl = m_req->contentLength();
    bool untilEof = !m_keepalive && cl == YHttpMessage::UnknownLength; // HTTP 1.0 request
    unsigned int maxBodyBuf = msg.getIntValue("maxreqbody", m_maxReqBody);
    if(cl != YHttpMessage::UnknownLength && cl > maxBodyBuf) // request body is too long
	return sendErrorResponse(413);
    DataSource* src = 0;//(m_requestBodySource && m_requestBodySource->isValid()) ? m_requestBodySource : NULL;

    if(m_buffer.length()) { // body part that arrived with headers
	if(src)
	    src->Forward(m_buffer);
	else if(m_req->bodyBuffer().length() < maxBodyBuf)
	    m_req->m_bodyBuffer.append(m_buffer);
    }

    char buf[BODY_BUF_SIZE];

    u_int32_t killtime = Time::secNow() + m_timeout;
    while(cl) {
	int r = m_socket->readData(buf, min(cl, sizeof(buf)));
	XDebug("HTTPServer", DebugAll, "readRequestBody: read %d bytes, left %d, untilEof=%s, maxBodyBuf=%u", r, cl, String::boolText(untilEof), maxBodyBuf);
	if(r == 0 && untilEof)
	    break;
	if(r < 0 && m_socket->canRetry() && (!m_timeout || Time::secNow() < killtime)) {
	    yield();
	    continue;
	}
	if(r <= 0)
	    return sendErrorResponse(400);

	DataBlock blk(buf, r, false);
	if(src)
	    src->Forward(blk);
	else if(m_req->bodyBuffer().length() < maxBodyBuf)
	    m_req->m_bodyBuffer.append(blk);

	if(cl != YHttpMessage::UnknownLength)
	    cl -= r;
    }

    if(src)
	src->Forward(DataBlock(0, 0), DataNode::invalidStamp(), DataNode::DataEnd);
    return true;
}

bool Connection::sendResponse()
{
    if(! m_rsp->build(m_buffer))
	return false;
    if (unsigned int written = (unsigned int)m_socket->writeData(m_buffer.data(), m_buffer.length()) != m_buffer.length()) {
	Debug("HTTPServer",DebugInfo,"Socket %d wrote only %d out of %d bytes",m_socket->handle(),written,m_buffer.length());
	// Destroy the thread, will kill the connection
	return false;
    }
    return true;
}

bool Connection::sendErrorResponse(int code)
{
    YHttpResponse e(this);
    e.setHeader("Connection", "close");
    e.status(code);
    String b(code);
    b << " " << e.statusText() << "\r\n";
    e.setBody(b);
    if(! e.build(m_buffer))
	return false;
    if (unsigned int written = (unsigned int)m_socket->writeData(m_buffer.data(), m_buffer.length()) != m_buffer.length()) {
	Debug("HTTPServer",DebugInfo,"Socket %d wrote only %d out of %d bytes",m_socket->handle(),written,m_buffer.length());
    }
    return false;
}

void Connection::connectionHeader(const char* hdr)
{
    m_connection = 0;
    ObjList* conntokens = String(m_req->getHeader("Connection")).split(',', false);
    for (ObjList* tok = conntokens->skipNull(); tok; tok = tok->skipNext()) {
	String key = tok->get()->toString();
	m_connection |= lookup(key.toLower(), s_connTokens);
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
