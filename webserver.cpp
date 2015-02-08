
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
    bool received(Message &msg);
protected:
    String guessContentType(const String& path);
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
    return "text/plain";
}

bool WebServer::received(Message &msg)
{
    if(YSTRING("GET") != msg.getValue("method"))
	return false;

    String path = m_root + msg.getParam("uri");
    Debug(&plugin, DebugAll, "WebServer '%s' is serving file '%s'", m_name.c_str(), path.c_str());

    if(! File::exists(path)) {
	msg.setParam("status", "404");
	return true;
    }
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
}


/**
 * YWebServerModule
 */
YWebServerModule::YWebServerModule()
    : Module("webserver","misc")
    , m_server(NULL)
{
    Output("Loaded module WEBSERVER");
}

YWebServerModule::~YWebServerModule()
{
    Output("Unloading module WEBSERVER");
    TelEngine::destruct(m_server);
}

void YWebServerModule::initialize()
{
    static bool notFirst = false;
    Output("Initializing module WEBSERVER");
    // Load configuration
    s_cfg = Engine::configFile("webserver");
    s_cfg.load();

    TelEngine::destruct(m_server);
    m_server = new WebServer("WebServer", s_cfg.getValue("general", "document_root", "/var/www"));
    if (notFirst)
	return;
    notFirst = true;
    installRelay(HttpRequest, "http.request", HttpRequest);
    setup();
}

bool YWebServerModule::received(Message &msg, int id)
{
    switch(id) {
    case HttpRequest:
	if(! m_server)
	    return false;
	return m_server->received(msg);
    }
    return Module::received(msg, id);
}


}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
