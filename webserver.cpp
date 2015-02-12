
/**
 * webserver.cpp
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

class WebServer: public GenObject
{
public:
    WebServer(const String& name, const String& root);
    ~WebServer();
    bool received(Message &msg, bool reqdata);
    static String guessContentType(const String& path);
private:
    String m_name, m_root;
};

/**
 * YWebServerModule
 */
class YWebServerModule : public Module
{
    enum {
	HttpRequest = Private,
	HttpReqData,
    };
protected:
    virtual bool received(Message &msg, int id);
public:
    YWebServerModule();
    virtual ~YWebServerModule();
    virtual void initialize();
private:
    WebServer* m_server;
};

class Servant : public RefObject
{
public:
    Servant(const String& path);
    ~Servant();
    bool received(Message& msg);
public: // GenObject
    void* getObject(const String& name) const;
private:
    String m_path;
    File m_fh;
};

/**
 * Local data
 */
static YWebServerModule plugin;
static Configuration s_cfg;

/**
 * WebServer
 */
WebServer::WebServer(const String& name, const String& root)
    : m_name(name)
    , m_root(root)
{
    Debug(&plugin, DebugAll, "WebServer '%s' created, document root: '%s'", m_name.c_str(), m_root.c_str());
}

WebServer::~WebServer()
{
    Debug(&plugin, DebugAll, "WebServer '%s' destroyed.", m_name.c_str());
}

String WebServer::guessContentType(const String& path)
{
    if(path.endsWith(".png"))
	return "image/png";
    if(path.endsWith(".jpg") || path.endsWith(".jpeg"))
	return "image/jpeg";
    if(path.endsWith(".htm") || path.endsWith(".html"))
	return "text/html";
    if(path.endsWith(".js"))
	return "application/x-javascript";
    if(path.endsWith(".css"))
	return "text/css";
    if(path.endsWith(".txt") || path.endsWith(".asc"))
	return "text/plain";
    return "application/octet-stream";
}

bool WebServer::received(Message &msg, bool reqdata)
{
    if(YSTRING("GET") != msg.getValue("method") || reqdata)
	return false;

    String path = m_root + msg.getParam("uri");
    Debug(&plugin, DebugAll, "WebServer '%s' is serving file '%s'", m_name.c_str(), path.c_str());

    if(! File::exists(path)) {
	msg.setParam("status", "404");
	return true;
    }
#if 0
    File f;
    if(! f.openPath(path)) {
	msg.setParam("status", "403");
	return true;
    }
    int64_t l = f.length();
    char buf[l + 1];
    f.readData(buf, l);
    f.terminate();
    buf[l] = '\0';

    msg.setParam("status", "200");
    msg.setParam("ohdr_Content-Type", guessContentType(path));
    msg.retValue() = buf;
    return true;
#else
    Servant* s = reinterpret_cast<Servant*>(msg.userObject("Servant"));
    if(! s)
	s = new Servant(path);
    return s->received(msg);
#endif
}


/**
 * YWebServerModule
 */
YWebServerModule::YWebServerModule()
    : Module("webserver","misc")
    , m_server(NULL)
{
    Output("Loaded module WebServer");
}

YWebServerModule::~YWebServerModule()
{
    Output("Unloading module WebServer");
    TelEngine::destruct(m_server);
}

void YWebServerModule::initialize()
{
    static bool notFirst = false;
    Output("Initializing module WebServer");
    // Load configuration
    s_cfg = Engine::configFile("webserver");
    s_cfg.load();

    TelEngine::destruct(m_server);
    m_server = new WebServer("WebServer", s_cfg.getValue("general", "document_root", "/var/www"));
    if (notFirst)
	return;
    notFirst = true;
    installRelay(HttpRequest, "http.serve", 100);
    installRelay(HttpReqData, "http.preserve", 100);
    setup();
}

bool YWebServerModule::received(Message &msg, int id)
{
    if(! m_server)
	return false;
    switch(id) {
    case HttpRequest:
	return m_server->received(msg, false);
    case HttpReqData:
	return m_server->received(msg, true);
    }
    return Module::received(msg, id);
}

/**
 * Servant
 */
Servant::Servant(const String& path)
    : m_path(path)
{
    XDebug(&plugin, DebugAll, "Servant %p created, path: '%s'", this, m_path.c_str());
}

Servant::~Servant()
{
    XDebug(&plugin, DebugAll, "Servant %p destroyed, path: '%s'", this, m_path.c_str());
}

void* Servant::getObject(const String& name) const
{
    if (name == YATOM("Stream"))
	return const_cast<File*>(&m_fh);
    if (name == YATOM("Servant"))
	return const_cast<Servant*>(this);
    return RefObject::getObject(name);
}

bool Servant::received(Message& msg)
{
    XDebug(&plugin, DebugAll, "Servant %p got message '%s'", this, msg.c_str());
    if(! m_fh.openPath(m_path)) {
	msg.setParam("status", "403");
	return true;
    }
    int64_t l = m_fh.length();
    msg.setParam("status", "200");
    msg.userData(this);
    msg.setParam("ohdr_Content-Length", String(l));
    msg.retValue() = String::empty();
    deref();
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
