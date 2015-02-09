/**
 * httpbase.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

/* 
 * plan:
 *   get with shutdown
 *   get with keepalive
 *   post with shutdown
 *   post with content-length
 *   get with known length (DataSource)
 *   post known length (DataConsumer)
 *   get unknown length (chunked)
 *   post chunked
 *   auth basic
 *   auth digest
 *   https
 *   upgrade to tls
 *   upgrade to websocket
 *
 */
#include <yatengine.h>
#include <string.h>

using namespace TelEngine;

static String s_test1Uri = "/test/1";

class TestThread : public Thread, private Mutex
{
public:
    TestThread(): m_serverPort(0)
	{ }
    virtual void run();
    virtual void cleanup();
    void configure(const NamedList& conf);
private:
    bool connectSocket();
    bool test_01_get_with_shutdown();
    bool test_02_get_with_keepalive();
private:
    String m_serverAddr;
    int m_serverPort;
    Socket m_sock;
};

class TestPlugin : public Plugin
{
public:
    TestPlugin();
    ~TestPlugin();
    virtual void initialize();
private:
    TestThread* m_testThread;
    bool m_first;
};

class TestHandler : public MessageHandler
{
public:
    TestHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

void TestThread::run()
{
    sleep(5);
    Debug(DebugInfo,"TestThread::run() [%p]",this);
    test_01_get_with_shutdown();
}

void TestThread::cleanup()
{
    Debug(DebugInfo,"TestThread::cleanup() [%p]",this);
}

void TestThread::configure(const NamedList& conf)
{
    Lock mylock(this);
    m_serverAddr = conf.getValue("addr", "0.0.0.0");
    if(m_serverAddr == "0.0.0.0")
	m_serverAddr = "127.0.0.1";
    m_serverPort = conf.getIntValue("port", 80);
}

bool TestThread::connectSocket()
{
    Lock mylock(this);
    m_sock.create(AF_INET, SOCK_STREAM);
    if (!m_sock.valid()) {
	Debug(DebugGoOn,"Unable to create the socket: %s", strerror(m_sock.error()));
	return false;
    }
    if (!m_sock.setBlocking(false)) {
	Debug(DebugGoOn, "Failed to set to nonblocking mode: %s", strerror(m_sock.error()));
	return false;
    }
    if (!m_sock.setLinger(5)) {
	Debug(DebugGoOn, "Failed to set linger: %s", strerror(m_sock.error()));
    }
    SocketAddr sa(AF_INET);
    sa.host(m_serverAddr);
    sa.port(m_serverPort);
    //if (!m_sock.connect(sa)) {
    if (!m_sock.connectAsync(sa.address(), sa.length(), 5000000UL)) {
	Debug(DebugGoOn,"Failed to connect to %s : %s", sa.addr().c_str(),strerror(m_sock.error()));
	return false;
    }
    Debug(DebugInfo,"Connected to %s", sa.addr().c_str());
    if (!m_sock.setBlocking(true)) {
	Debug(DebugGoOn, "Failed to set to blocking mode: %s", strerror(m_sock.error()));
	return false;
    }
    return true;
}

bool TestThread::test_01_get_with_shutdown()
{
    if(! connectSocket())
	return false;
    String req("GET ");
    req << s_test1Uri << " HTTP/1.0\r\n\r\n";
    int w = m_sock.send(req.c_str(), req.length());
    Debug(DebugAll, "Sent %d bytes to server: <<%s>>", w, req.c_str());
    Debug(DebugAll, "Shutting down sending part");
    m_sock.shutdown(false, true);
    char buf[8192];
    Debug(DebugAll, "Waiting for reply");
    int r = m_sock.readData(buf, sizeof(buf) - 1);
    if(r < 0) {
	Debug(DebugFail, "Socket read error: %s", strerror(m_sock.error()));
	return false;
    }
    String s;
    if(r >= 0) {
	buf[r] = 0;
	s = buf;
    }
    Output("Got HTTP response (%d bytes): %s\n", r, s.c_str());
    m_sock.shutdown(true, true);
    m_sock.terminate();
    return true;
}

bool TestThread::test_02_get_with_keepalive()
{
    if(! connectSocket())
	return false;

    String req("GET /test/1 HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
    int w = m_sock.send(req.c_str(), req.length());
    Debug(DebugAll, "Sent %d bytes to server: <<%s>>", w, req.c_str());
    Debug(DebugAll, "Shutting down sending part");
    m_sock.shutdown(false, true);
    char buf[8192];
    Debug(DebugAll, "Waiting for reply");
    int r = m_sock.readData(buf, sizeof(buf) - 1);
    if(r < 0) {
	Debug(DebugFail, "Socket read error: %s", strerror(m_sock.error()));
	return false;
    }
    String s;
    if(r >= 0) {
	buf[r] = 0;
	s = buf;
    }
    Output("Got HTTP response (%d bytes): %s\n", r, s.c_str());
    m_sock.shutdown(true, true);
    m_sock.terminate();

    m_sock.shutdown(true, true);
    m_sock.terminate();
    return true;
}

#if 0
bool TestThread::test_post_chunked()
{
    const char* chunked_data = "4\r\nWiki\r\n5\r\npedia\r\ne\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n";
    const char* chunked_xpct = "Wikipedia\r\n in\r\n\r\nchunks.";
}
#endif

bool TestHandler::received(Message &msg)
{
    Debug(DebugInfo, "Received message '%s' time=" FMT64U " thread=%p", msg.c_str(), msg.msgTime().usec(),Thread::current());
    if(msg != YSTRING("http.request"))
	return false;
    String method = msg.getValue("method");
    String uri = msg.getParam("uri");
    if(! uri.startSkip("/test/", false))
	return false;

    String r;
    r << method << " " << uri;

    msg.setParam("status", "200");
    msg.setParam("ohdr_Content-Type", "text/plain");
    msg.retValue() = r;
    return true;
};

TestPlugin::TestPlugin()
    : Plugin("testhttpbase")
    , m_testThread(NULL)
    , m_first(true)
{
    Output("I am module TestHttpModule");
}

TestPlugin::~TestPlugin()
{
}

void TestPlugin::initialize()
{
    NamedList* httpdconf = 0;
    Output("Initializing module TestHttpBase");
    Configuration cfg;
    cfg = Engine::configFile("httpserver");
    cfg.load();
    for (unsigned int i = 0; i < cfg.sections(); i++) {
	httpdconf = cfg.getSection(i);
	String name = httpdconf ? httpdconf->c_str() : "";
	if (! name.startSkip("listener ",false))
	    continue;
	name.trimBlanks();
	httpdconf->String::operator=(name);
	break; // XXX only first listener
    }
    if (m_first)
	m_testThread = new TestThread;
    m_testThread->configure(*httpdconf);
    if (m_first) {
	m_first = false;
	m_testThread->startup();
	Engine::install(new TestHandler("http.request"));
    }
//    delete httpdconf;
}

INIT_PLUGIN(TestPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
