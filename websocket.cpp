
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

using namespace TelEngine;

namespace { // anonymous

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
};

/**
 * Local data
 */
static WebSocketModule plugin;
static Configuration s_cfg;


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
    if (String(msg.getValue("hdr_Upgrade")).toLower() != YSTRING("websocket"))
	return false;
    String key = msg.getParam("Sec-WebSocket-Key");
    key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA1 hash(key.trimSpaces());
    Base64 b64(const_cast<unsigned char*>(hash.rawDigest()), hash.hashLength(), true);
    String response;
    b64.encode(response);
    msg.setParam("ohdr_Sec-WebSocket-Accept", response);

    {
	String p = msg.getParam("hdr_Sec-WebSocket-Protocol");
    }

    WebSocketServer* wss = new WebSocketServer();
    wss->init(msg);
    msg.userData(wss);
    wss->deref();
    return true;
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
    m_socket = sock;
    m_headers = msg;
    return true;
}

void WebSocketServer::run()
{
    XDebug(&plugin, DebugAll, "WebSocketServer[%p] run() entry", this);
    Thread::sleep(5);
    XDebug(&plugin, DebugAll, "WebSocketServer[%p] run() exit", this);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
