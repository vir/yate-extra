
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
#include <sys/stat.h>
#include <unistd.h>

using namespace TelEngine;

namespace { // anonymous

class WebServer: public GenObject
{
public:
    WebServer(const String& name);
    ~WebServer();
    bool received(Message &msg, bool reqdata);
    static String guessContentType(const String& path);
private:
    String m_name;
};

/**
 * YWebServerModule
 */
class YWebServerModule : public Module
{
    enum {
	HttpRequest = Private,
	HttpReqData = (Private << 1),
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
    Servant(const String& path, NamedList* cfg);
    ~Servant();
    bool received(Message& msg);
public: // GenObject
    void* getObject(const String& name) const;
private:
    String m_path;
    File m_fh;
    NamedList* m_cfg;
};

class DirectoryHandler : public RefObject, public Stream
{
public:
    DirectoryHandler(const String& path, NamedList* cfg);
    ~DirectoryHandler();
    bool received(Message& msg);
public: // GenObject
    void* getObject(const String& name) const;
public: // Stream
    virtual bool terminate();
    virtual bool valid() const;
    virtual int writeData(const void* buffer, int length);
    virtual int readData(void* buffer, int length);
private:
    String m_path;
    MemoryStream m_file;
    NamedList* m_cfg;
};

/**
 * Local data
 */
static YWebServerModule plugin;
static Configuration s_cfg;

/**
 * WebServer
 */
WebServer::WebServer(const String& name)
    : m_name(name)
{
    Debug(&plugin, DebugAll, "WebServer '%s' created", m_name.c_str());
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

static TelEngine::String guessHandler(const TelEngine::String path)
{
    if (path.endsWith("/"))
	return "directory";
#ifdef _WINDOWS
    struct _stat st;
    if (0 != _stat(path.c_str(), &st)) {
#else
    struct stat st;
    if (0 != stat(path.c_str(), &st)) {
#endif
	switch (errno) {
	    case EACCES: return "error 403";
	    case ENOENT: return "error 404";
	    default: return "error 500";
	}
    }
    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
	return "file";
    if (S_ISDIR(st.st_mode))
	return "directory";
    return "error 500";
}

static void cleanupUri(String& uri)
{
    int idx;
    while ((idx = uri.find("/../")) >= 0)
	uri = uri.substr(0, idx) + uri.substr(idx + 3);
    while ((idx = uri.find("/./")) >= 0)
	uri = uri.substr(0, idx) + uri.substr(idx + 2);
    while ((idx = uri.find("//")) >= 0)
	uri = uri.substr(0, idx) + uri.substr(idx + 1);
}

bool WebServer::received(Message &msg, bool reqdata)
{
    if (reqdata && ! msg.getBoolValue("reqbody"))
	return false;
    if (reqdata) // no handlers below accept request data
	return false;

    /* prepare configuration parameters list */
    NamedList* cfg = new NamedList("params");
    NamedList* nl;
    if ((nl = s_cfg.getSection(YSTRING("default"))))
	cfg->copyParams(*nl);
    String server = msg.getValue("server");
    if ((nl = s_cfg.getSection(server)))
	cfg->copyParams(*nl);
    if ((nl = s_cfg.getSection(msg.getValue("conf"))))
	cfg->copyParams(*nl);
    cfg->copyParams(msg);

    String handler = msg.getValue("handler", "auto");
    String path = msg.getValue("path");
    if (! path.length()) {
	String uri = msg.getParam("uri");
	cleanupUri(uri);
	path = cfg->getValue(YSTRING("root"), "/var/www") + uri;
    }
    if (handler == YSTRING("auto"))
	handler = guessHandler(path);

    String dumpcfg;
    cfg->dump(dumpcfg, ", ");
    Debug(&plugin, DebugAll, "WebServer '%s' is serving resource '%s', handler is '%s', cfg: %s", m_name.c_str(), path.c_str(), handler.c_str(), dumpcfg.c_str());

    if (handler == YSTRING("file")) {
	Servant* s = reinterpret_cast<Servant*>(msg.userObject("Servant"));
	if(! s)
	    s = new Servant(path, cfg);
	else
	    TelEngine::destruct(cfg);
	return s->received(msg);
    }
    if (handler == YSTRING("directory")) {
	DirectoryHandler* s = reinterpret_cast<DirectoryHandler*>(msg.userObject("DirectoryHandler"));
	if(! s)
	    s = new DirectoryHandler(path, cfg);
	else
	    TelEngine::destruct(cfg); // noone need it
	return s->received(msg);
    }
    TelEngine::destruct(cfg); // noone need it
    if (handler.startSkip("error")) {
	msg.setParam("status", handler);
	return true;
    }
    if (handler.startSkip("redirect")) {
	const char* status = msg.getValue("status");
	if (!status || *status != '3')
	    msg.setParam("status", "302");
	if (handler.find("://") < 0)
	    handler = YSTRING("http://") + msg.getParam("hdr_Host") + handler;
	msg.setParam("ohdr_Location", handler);
	return true;
    }
    if (handler == YSTRING("bulkfile")) {
	if(YSTRING("GET") != msg.getValue("method")) {
	    msg.setParam("status", "405");
	    return true;
	}
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
    return false;
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
    m_server = new WebServer("WebServer");
    if (notFirst)
	return;
    notFirst = true;
    installRelay(HttpRequest, "http.serve", 150);
    installRelay(HttpReqData, "http.preserve", 150);
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
Servant::Servant(const String& path, NamedList* cfg)
    : m_path(path)
    , m_cfg(cfg)
{
    XDebug(&plugin, DebugAll, "Servant %p created, path: '%s'", this, m_path.c_str());
}

Servant::~Servant()
{
    XDebug(&plugin, DebugAll, "Servant %p destroyed, path: '%s'", this, m_path.c_str());
    TelEngine::destruct(m_cfg);
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
    if(! File::exists(m_path)) {
	Debug(&plugin, DebugInfo, "File '%s' does not exist", m_path.c_str());
	msg.setParam("status", "404");
	return true;
    }
    if(YSTRING("GET") != msg.getValue("method")) {
	msg.setParam("status", "405");
	return true;
    }
    if(! m_fh.openPath(m_path)) {
	TelEngine::String error;
	Thread::errorString(error,m_fh.error());
	Debug(&plugin, DebugWarn, "Can not open File '%s': %s", m_path.c_str(), error.c_str());
	switch(m_fh.error()) {
#ifdef _WINDOWS
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:  msg.setParam("status", "404"); break;
	case ERROR_ACCESS_DENIED:   msg.setParam("status", "403"); break;
#else
        case EACCES: msg.setParam("status", "403"); break;
	case ENOENT: msg.setParam("status", "404"); break;
#endif
	default:     msg.setParam("status", "500"); break;
	}
	return true;
    }
    int64_t l = m_fh.length();
    msg.setParam("status", "200");
    msg.userData(this);
    msg.setParam("ohdr_Content-Type", WebServer::guessContentType(m_path));
    msg.setParam("ohdr_Content-Length", String(l));
    msg.retValue() = String::empty();
    deref();
    return true;
}

/*
 * DirectoryHandler
 */

DirectoryHandler::DirectoryHandler(const String& path, NamedList* cfg)
    : m_path(path)
    , m_cfg(cfg)
{
}

DirectoryHandler::~DirectoryHandler()
{
    TelEngine::destruct(m_cfg);
}

bool DirectoryHandler::received(Message& msg)
{
    if (! m_cfg->getBoolValue("dirlist", false)) {
	msg.setParam("status", "403");
	msg.retValue() = "Directory listing denied";
	return true;
    }
    ObjList dirs, files;
    int error;
    if (! File::listDirectory(m_path, &dirs, &files, &error)) {
	TelEngine::String s;
	Thread::errorString(s,error);
	Debug(&plugin,DebugNote,"Failed to list directory '%s': %d %s",
		m_path.c_str(),error,s.c_str());
	msg.setParam("status", "400");
	return true;
    }
    TelEngine::String s;
    for (ObjList* o = dirs.skipNull(); o; o = o->skipNext()) {
	String* n = static_cast<String*>(o->get());
	if (!*n)
	    continue;
	s << " * " << *n << "/\r\n";
    }
    for (ObjList* o = files.skipNull(); o; o = o->skipNext()) {
	String* n = static_cast<String*>(o->get());
	if (!*n)
	    continue;
	s << " * " << *n << "\r\n";
    }
    m_file.writeData(s.c_str(), s.length());
    m_file.seek(Stream::SeekBegin, 0);

    msg.setParam("status", "200");
    msg.userData(this);
    msg.setParam("ohdr_Content-Type", "text/plain");
    msg.setParam("ohdr_Content-Length", TelEngine::String(m_file.length()));
    msg.retValue() = String::empty();
    deref();
    return true;
}

void* DirectoryHandler::getObject(const String& name) const
{
    if (name == YATOM("Stream"))
	return static_cast<Stream*>(const_cast<DirectoryHandler*>(this));
    if (name == YATOM("Servant"))
	return const_cast<DirectoryHandler*>(this);
    return RefObject::getObject(name);
}

bool DirectoryHandler::terminate()
{
    return m_file.terminate();
}

bool DirectoryHandler::valid() const
{
    return m_file.valid();
}

int DirectoryHandler::writeData(const void* buffer, int length)
{
    return 0;
}

int DirectoryHandler::readData(void* buffer, int length)
{
    return m_file.readData(buffer, length);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
