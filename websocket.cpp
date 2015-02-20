
/**
 * websocket.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Filesystem access for HTTP server module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Author: Marian Podgoreanu
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

#include <yatephone.h>
#include <stdlib.h>
#include <string.h>

using namespace TelEngine;

namespace { // anonymous

class WSHeader
{
    unsigned char b[14]; // 2 .. 14 bytes of header, payload follows
public:
    enum Opcode {
	Continuation = 0x00,
	Text = 0x01,
	Binary = 0x02,
	Close = 0x08,
	Ping = 0x09,
	Pong = 0x0A,
    };
    bool fin() const { return 0 != (b[0] & 0x80); }
    void fin(bool x) { if(x) b[0] |= 0x80; else b[0] &= ~1; }
    unsigned int rsv() const { return (b[0] >> 4) & 0x07; }
    void rsv(unsigned int x) { b[0] &= 0x8F; b[0] |= (x & 0x07) << 4; }
    Opcode opcode() const { return (Opcode)(b[0] & 0x0F); }
    void opcode(Opcode c) { b[0] &= 0xF0; b[0] |= ((unsigned int)c & 0x0F); }
    bool mask() const { return 0 != (b[1] & 0x80); }
    void mask(bool x) { b[1] &= 0x7F; if(x) b[1] |= 0x80; }
    int64_t payloadLength() const;
    void payloadLength(int64_t x);
    unsigned int headerLength() const;
    unsigned int fullLength() const;
    uint32_t maskingKey() const;
    unsigned char* data();
    const char* data() const;
    String dump() const;
    void applyMask();
};

/**
 * WebSocketModule
 */
class WebSocketModule : public Module
{
    enum {
	HttpUpgrade = Private,
    };
protected:
    virtual bool received(Message &msg, int id);
public:
    WebSocketModule();
    virtual ~WebSocketModule();
    virtual void initialize();
    bool processUpgradeMsg(Message& msg);
};

class WSDataSource: public DataSource
{
public:
    WSDataSource(Socket* sock)
	: m_socket(sock)
	{ }
private:
    Socket* m_socket;
};

class WSDataConsumer: public DataConsumer
{
public:
    WSDataConsumer(Socket* sock)
	: m_socket(sock)
	{ }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    void Close(int code);
protected:
    bool sendData(const void* data, unsigned int length);
private:
    Socket* m_socket;
    Mutex m_mutex;
};

class WebSocketServer : public RefObject, public Runnable
{
public:
    WebSocketServer();
    ~WebSocketServer();
    bool init(Message& msg);
public: // GenObject
    void* getObject(const String& name) const;
public: // Runnable
    virtual void run();
private:
    Socket* m_socket;
    NamedList m_headers;
    String m_protocol;
    String m_extension;
    WSDataSource* m_ds;
    WSDataConsumer* m_dc;
};

/**
 * Local data
 */
static WebSocketModule plugin;
static Configuration s_cfg;

/**
 * WSHeader
 */
int64_t WSHeader::payloadLength() const
{
    int64_t r = 0;
    switch(b[1] & 0x7F) {
    case 126:
	r = (b[2] << 8) | b[3];
	break;
    case 127:
	r = b[2];
	r <<= 8;
	r |= b[3];
	r <<= 8;
	r |= b[4];
	r <<= 8;
	r |= b[5];
	r <<= 8;
	r |= b[6];
	r <<= 8;
	r |= b[7];
	r <<= 8;
	r |= b[8];
	r <<= 8;
	r |= b[9];
	break;
    default:
	r = b[1] & 0x7F;
	break;
    }
    return r;
}
void WSHeader::payloadLength(int64_t x)
{
    b[1] &= 0x80;
    if (x <= 125) {
	b[1] |= (unsigned char)x;
    }
    else if (x <= 65535) {
	b[1] |= 126;
	b[2] = (unsigned char)(x >> 8);
	b[3] = (unsigned char)x;
    }
    else {
	b[1] |= 127;
	b[9] = (unsigned char)x;
	b[8] = (unsigned char)(x >>= 8);
	b[7] = (unsigned char)(x >>= 8);
	b[6] = (unsigned char)(x >>= 8);
	b[5] = (unsigned char)(x >>= 8);
	b[4] = (unsigned char)(x >>= 8);
	b[3] = (unsigned char)(x >>= 8);
	b[2] = (unsigned char)(x >>= 8);
    }
}
unsigned int WSHeader::headerLength() const
{
    unsigned int r = 2;
    if (b[1] & 0x80)
	r += 4; // mask key
    switch(b[1] & 0x7F) {
    case 126:
	r += 2;
	break;
    case 127:
	r += 8;
	break;
    default:
	break;
    }
    return r;
}
unsigned int WSHeader::fullLength() const
{
    return headerLength() + payloadLength();
}
uint32_t WSHeader::maskingKey() const
{
    uint32_t r = 0;
    unsigned int i = 2;
    switch(b[1] & 0x7F) {
    case 126:
	i += 2;
	break;
    case 127:
	i += 8;
	break;
    }
    r = b[i++];
    r <<= 8;
    r |= b[i++];
    r <<= 8;
    r |= b[i++];
    r <<= 8;
    r |= b[i++];
    return r;
}
unsigned char* WSHeader::data()
{
    return &b[0] + headerLength();
}
const char* WSHeader::data() const
{
    return (const char*)&b[0] + headerLength();
}
String WSHeader::dump() const
{
    String r;
    if (fin())
	r << "[FIN] ";
    if (mask())
	r << "[MASK] ";
    r << "Opcode=" << (int)opcode();
    r << " Payload length=" << payloadLength();
    if (mask())
	r << " Masking key=" << maskingKey();
    r << " Header length=" << headerLength();
#if 0
    if (opcode() == Text) {
	r << " Text='";
	r.append(data(), payloadLength());
	r << "'";
    }
    if (opcode() == Binary) {
	String s;
	s.hexify(const_cast<char*>(data()), payloadLength(), ' ');
	r << " Data=" << s;
    }
#endif
    return r;
}
void WSHeader::applyMask()
{
    unsigned char* p = data();
    unsigned char* mask = &b[2];
    switch(b[1] & 0x7F) {
    case 126: mask += 2; break;
    case 127: mask += 8; break;
    }
    for(unsigned int i = 0; i < payloadLength(); ++i, ++p)
	*p ^= mask[i % 4];
}

/**
 * WebSocketModule
 */
WebSocketModule::WebSocketModule()
    : Module("websocket","misc")
{
    Output("Loaded module WebSocket");
}

WebSocketModule::~WebSocketModule()
{
    Output("Unloading module WebSocket");
}

void WebSocketModule::initialize()
{
    static bool notFirst = false;
    Output("Initializing module WebSocket");
    s_cfg = Engine::configFile("websocket");
    s_cfg.load();

    if (notFirst)
	return;
    notFirst = true;
    installRelay(HttpUpgrade, "http.upgrade", 100);
    setup();
}

bool WebSocketModule::received(Message &msg, int id)
{
    switch(id) {
    case HttpUpgrade:
	return processUpgradeMsg(msg);
    }
    return Module::received(msg, id);
}

bool WebSocketModule::processUpgradeMsg(Message& msg)
{
    if (0 != strcmp(msg.getValue("method"), "GET")) {
	DDebug(&plugin, DebugInfo, "Wrong method for websocket %s", msg.getValue("method"));
	return false;
    }
    if (atof(msg.getValue("version")) < 1.1) {
	DDebug(&plugin, DebugInfo, "Wrong HTTP version for websocket %s", msg.getValue("version"));
	return false;
    }
    if (String(msg.getValue("hdr_Upgrade")).toLower() != YSTRING("websocket")) {
	XDebug(&plugin, DebugAll, "Upgrade header is not 'websocket': %s", msg.getValue("hdr_Upgrade"));
	return false;
    }
    String key = msg.getParam("hdr_Sec-WebSocket-Key");
    if (! key.length()) {
	DDebug(&plugin, DebugInfo, "Required header Sec-WebSocket-Key is missing");
	return false;
    }
    String version = msg.getValue("hdr_Sec-WebSocket-Version");
    if (version != "13") {
	Debug(&plugin, DebugInfo, "Upgrade request with wrong websocket version %s", version.c_str());
	return false;
    }
    key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA1 hash(key.trimSpaces());
    Base64 b64(const_cast<unsigned char*>(hash.rawDigest()), hash.hashLength(), true);
    String response;
    b64.encode(response);
    msg.setParam("ohdr_Sec-WebSocket-Accept", response);

    WebSocketServer* wss = new WebSocketServer();
    wss->init(msg);
    msg.userData(wss);
    wss->deref();
    return true;
}

/**
 * WSDataSource
 */

/**
 * WSDataConsumer
 */
unsigned long WSDataConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    WSHeader z;
    z.fin(true);
    z.rsv(0);
    z.opcode(WSHeader::Text);
    z.mask(false);
    z.payloadLength(data.length());
    XDebug(&plugin, DebugAll, "Sending WebSocket data packet: %s", z.dump().c_str());
    Lock lock(m_mutex);
    sendData(&z, z.headerLength());
    sendData(data.data(), data.length());
    return 0;
}

void WSDataConsumer::Close(int code)
{
    WSHeader z;
    z.fin(true);
    z.rsv(0);
    z.opcode(WSHeader::Close);
    z.mask(false);
    z.payloadLength(2);
    z.data()[0] = ((unsigned char*)&code)[1];
    z.data()[1] = ((unsigned char*)&code)[0];
    XDebug(&plugin, DebugAll, "Sending WebSocket control packet: %s", z.dump().c_str());
    sendData(&z, z.fullLength());
}

bool WSDataConsumer::sendData(const void* data, unsigned int length)
{
    int m_timeout = 10000;
    u_int32_t killtime = Time::secNow() + m_timeout;
    while (m_socket && m_socket->valid()) {
	bool writeok = false;
	bool error = false;
	if (m_socket->select(0, &writeok, &error, 10000)) {
	    if (error) {
		Debug("websocket",DebugInfo,"Socket exception condition on %d",m_socket->handle());
		return false;
	    }
	    if (!writeok) {
		if(!m_timeout || Time::secNow() < killtime) {
		    Thread::yield();
		    continue;
		}
		Debug("websocket",DebugAll, "Timeout waiting for socket %d", m_socket->handle());
		return false;
	    }

	    unsigned int written = (unsigned int)m_socket->writeData(data, length);
	    if (!m_socket->canRetry()) {
		Debug("websocket",DebugWarn,"Socket write error %d on %d",errno,m_socket->handle());
		return false;
	    }

	    if (written) {
		length -= written;
		data = (const char*)data + written;
		if (0 == length)
		    return true;
		killtime = Time::secNow() + m_timeout;
	    }
	}
	else if (!m_socket->canRetry()) {
	    Debug("websocket",DebugWarn,"socket select error %d on %d",errno,m_socket->handle());
	    return false;
	}
    }
    return false;
}

/**
 * WebSocketServer
 */
WebSocketServer::WebSocketServer()
    : m_socket(NULL)
    , m_headers("WebSocketHeaders")
{
    XDebug(&plugin, DebugAll, "WebSocketServer[%p] created", this);
}

WebSocketServer::~WebSocketServer()
{
    XDebug(&plugin, DebugAll, "WebSocketServer[%p] destroyed", this);
}

void* WebSocketServer::getObject(const String& name) const
{
    XDebug(&plugin, DebugAll, "WebSocketServer[%p]::getObject(%s)", this, name.c_str());
    if (name == YATOM("Runnable"))
	return static_cast<Runnable*>(const_cast<WebSocketServer*>(this));
    if (name == YATOM("WebSocketServer"))
	return const_cast<WebSocketServer*>(this);
    return RefObject::getObject(name);
}

bool WebSocketServer::init(Message& msg)
{
    XDebug(&plugin, DebugAll, "WebSocketServer[%p] got message '%s'", this, msg.c_str());
    Socket* sock = static_cast<Socket*>(msg.userObject("Socket"));
    if (!sock)
	return false;
    m_protocol = msg.getValue("hdr_Sec-WebSocket-Protocol");
    m_extension = msg.getValue("hdr_Sec-WebSocket-Extensions");
    m_socket = sock;
    m_headers = msg;
    m_ds = new WSDataSource(sock);
    m_dc = new WSDataConsumer(sock);
    return true;
}

void WebSocketServer::run()
{
    XDebug(&plugin, DebugAll, "WebSocketServer[%p] run() entry", this);
    while (m_socket && m_socket->valid()) {
	bool readok = false;
	bool error = false;
	if (m_socket->select(&readok, 0, 0, 100000)) {
	    if (error) {
		Debug("websocket",DebugInfo,"Socket exception condition on %d",m_socket->handle());
		return;
	    }
	    if (!readok) {
		Debug("websocket",DebugAll, "Timeout waiting for socket %d", m_socket->handle());
		return;
	    }
	    DataBlock rbuf(NULL, 1024);
	    int readsize = m_socket->readData(rbuf.data(), rbuf.length());
	    if (!readsize) {
		Debug("websocket",DebugInfo,"Socket condition EOF on %d",m_socket->handle());
		return;
	    }
	    else if (readsize > 0) {
		WSHeader* d = reinterpret_cast<WSHeader*>(rbuf.data());
		XDebug(&plugin, DebugAll, "Got WebSocket packet: %s", d->dump().c_str());
		if (d->mask())
		    d->applyMask();
		DataBlock b(d->data(), d->payloadLength());
		String s;
		s.hexify(b.data(), b.length(), ' ');
		XDebug(&plugin, DebugAll, "WebSocket packet payload: %s = '%s'", s.c_str(), String((const char*)b.data(), b.length()).c_str());

		Thread::sleep(1);
		String r("Hellow, world!");
		m_dc->Consume(DataBlock(const_cast<char*>(r.c_str()), r.length()), 0UL, 0UL);
		Thread::sleep(1);
		m_dc->Close(1000);
		Thread::sleep(1);
	    }
	    else if (!m_socket->canRetry()) {
		Debug("websocket",DebugWarn,"Socket read error %d on %d",errno,m_socket->handle());
		return;
	    }
	}
	else if (!m_socket->canRetry()) {
	    Debug("websocket",DebugWarn,"socket select error %d on %d",errno,m_socket->handle());
	    return;
	}
    }
    XDebug(&plugin, DebugAll, "WebSocketServer[%p] run() exit", this);
}


}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
