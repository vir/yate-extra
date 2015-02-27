
/**
 * webcgi.cpp
 *
 * Common Gateway Protocol (RFC3875) module for Yate HTTP server.
 *
 * Copyright (c) 2015 Vasily i. Rdekin <vasilyredkin@gmail.com>
 * MIT License.
 */

#include <yatephone.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <ctype.h>
#include <stdlib.h>

#define min(a, b) ((a)<(b)?(a):(b))

using namespace TelEngine;

namespace { // anonymous

/**
 * YWebCGIModule
 */
class YWebCGIModule : public Module
{
public:
    enum {
	HttpRequest = Private,
	HttpReqData = (Private << 1),
    };
protected:
    virtual bool received(Message &msg, int id);
public:
    YWebCGIModule();
    virtual ~YWebCGIModule();
    virtual void initialize();
};

class CGIEnv
{
public:
    CGIEnv();
    void Set(const char* hdr, const char* val);
    void SetAddr(const char* prefix, const char* val);
    char ** ptr();
    void build(const NamedList& params);
private:
    char** m_buf;
    size_t m_size, m_count;
};

class Servant: public RefObject, public Stream
{
public:
    Servant(const String& path, NamedList& cfg);
    ~Servant();
    bool received(Message& msg, int id);
public: // GenObject
    void* getObject(const String& name) const;
public: // Stream
    virtual bool terminate();
    virtual bool valid() const;
    virtual int writeData(const void* buffer, int length);
    virtual int readData(void* buffer, int length);
protected:
    bool create(const char *script, const NamedList& env);
    void cleanup();
    void closeIn();
    void closeOut();
    void readHeaders(NamedList& msg, const char* prefix = NULL);
private:
    String m_path;
    pid_t m_pid;
    Stream* m_in;
    Stream* m_out;
    DataBlock m_buf;
};

/**
 * Local data
 */
static YWebCGIModule plugin;

static void cleanupUri(String& uri)
{
    int idx;
    while ((idx = uri.find("/../")) >= 0)
	uri = uri.substr(0, idx) + uri.substr(idx + 3);
    while ((idx = uri.find("/./")) >= 0)
	uri = uri.substr(0, idx) + uri.substr(idx + 2);
    while ((idx = uri.find("//")) >= 0)
	uri = uri.substr(0, idx) + uri.substr(idx + 1);
    if ((idx = uri.find('?')) >= 0)
	uri = uri.substr(0, idx);
}

/**
 * YWebCGIModule
 */
YWebCGIModule::YWebCGIModule()
    : Module("webcgi","misc")
{
    Output("Loaded module WebCGI");
}

YWebCGIModule::~YWebCGIModule()
{
    Output("Unloading module WebCGI");
}

void YWebCGIModule::initialize()
{
    static bool notFirst = false;
    Output("Initializing module WebCGI");
    if (notFirst)
	return;
    notFirst = true;
    installRelay(HttpRequest, "http.serve", 100);
    installRelay(HttpReqData, "http.preserve", 100);
    setup();
}

bool YWebCGIModule::received(Message &msg, int id)
{
    if (YSTRING("cgi") != msg.getValue("handler"))
	return false;

    switch(id) {
    case HttpReqData:
	if (! msg.getBoolValue("reqbody"))
	    return false;
    case HttpRequest:
	break;
    default:
	return Module::received(msg, id);
    }

    String path = msg.getValue("path");
    if (! path.length()) {
	String uri = msg.getParam("uri");
	cleanupUri(uri);
	path = msg.getValue(YSTRING("root"), "/var/www") + uri;
    }

    Debug(&plugin, DebugAll, "WebCGI is serving resource '%s'", path.c_str());

    Servant* s = reinterpret_cast<Servant*>(msg.userObject("Servant"));
    if(! s)
	s = new Servant(path, msg);
    return s->received(msg, id);
}

/**
 * CGIEnv
 */
CGIEnv::CGIEnv()
    : m_buf(NULL), m_size(0), m_count(0)
{
}
void CGIEnv::Set(const char* hdr, const char* val)
{
    if(m_count + 1 > m_size) {
	m_size += 50;
	m_buf = static_cast<char**>(realloc(m_buf, m_size * sizeof(char*)));
    }
    XDebug(DebugAll,"Set environment(%s = %s)", hdr, val);
    m_buf[m_count++] = strdup(String(hdr) + "=" + (val?val:""));
    m_buf[m_count] = NULL;
}
void CGIEnv::SetAddr(const char* prefix, const char* val)
{
    String s(val);
    char * t = strchr(const_cast<char*>(s.c_str()), ':');
    if (t) {
	*t++ = '\0';
	Set(String(prefix) + "_ADDR", s.c_str());
	Set(String(prefix) + "_PORT", t);
    }
}
char ** CGIEnv::ptr()
{
    return m_buf;
}
void CGIEnv::build(const NamedList& params)
{
    Set("GATEWAY_INTERFACE", "CGI/1.1");
    Set("REQUEST_METHOD", params.getValue("method"));
    //Set("REQUEST_SCHEME", params.getValue("scheme"));
    Set("REQUEST_URI", params.getValue("uri"));
    SetAddr("REMOTE", params.getValue("address"));
    SetAddr("SERVER", params.getValue("local"));
    Set("SERVER_PROTOCOL", String("HTTP/") + params.getValue("version"));
    const char* q = ::strchr(params.getValue("uri"), '?');
    Set("QUERY_STRING", q ? q + 1 : NULL);
    Set("DOCUMENT_ROOT", params.getValue("root"));
#if 1
    for (const ObjList* l = params.paramList()->skipNull(); l; l = l->skipNext()) {
	const NamedString* ns = static_cast<const NamedString*>(l->get());
	String key(ns->name());
	if (key.startSkip("hdr_", false)) {
	    String k("HTTP_");
	    k += key.toUpper();
	    int idx;
	    while((idx = k.find('-')) >= 0)
		k = k.substr(0, idx) + "_" + k.substr(idx + 1);
	    Set(k.c_str(), ns->c_str());
	}
    }
#endif
    for(int i = 0; i < 100; ++i)
	Set(String("PARAM")+String(i), String(i));
}

/**
 * Servant
 */
Servant::Servant(const String& path, NamedList& cfg)
    : m_path(path)
    , m_in(0), m_out(0)
{
    XDebug(&plugin, DebugAll, "Servant %p created, path: '%s'", this, m_path.c_str());
    if (!create(m_path.safe(), cfg)) {
	m_pid = 0;
	return;
    }
    if (m_in && !m_in->setBlocking(false))
	Debug(DebugWarn,"Failed to set nonblocking mode, expect trouble [%p]",this);
}

Servant::~Servant()
{
    XDebug(&plugin, DebugAll, "Servant %p destroyed, path: '%s'", this, m_path.c_str());
}

void* Servant::getObject(const String& name) const
{
    if (name == YATOM("Stream"))
	return static_cast<Stream*>(const_cast<Servant*>(this));
    if (name == YATOM("Servant"))
	return const_cast<Servant*>(this);
    return RefObject::getObject(name);
}

bool Servant::received(Message& msg, int id)
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
    if (id == YWebCGIModule::HttpRequest)
	readHeaders(msg, "ohdr_");
    msg.setParam("status", "200");
    msg.userData(this);
    msg.retValue() = String::empty();
    deref();
    return true;
}

bool Servant::terminate()
{
    return true;
}

bool Servant::valid() const
{
    return true;
}

int Servant::writeData(const void* buffer, int length)
{
    return 0;
}

int Servant::readData(void* buffer, int length)
{
    if (m_buf.null()) {
	int rd = m_in->readData(buffer, length);
	XDebug(&plugin, DebugAll, "readData(%p, %d) from CGI (%d bytes) [%p]", buffer, length, rd, this);
	if (rd > 0)
	    return rd;
	cleanup();
	return 0;
    }
    else {
	int r = min(length, (int)m_buf.length());
	XDebug(&plugin, DebugAll, "readData(%p, %d) from buffer (%d bytes) r=%d [%p]", buffer, length, m_buf.length(), r, this);
	memcpy(buffer, m_buf.data(), r);
	if (r == (int)m_buf.length())
	    m_buf.clear();
	else
	    m_buf.cut(-r);
	return r;
    }
}

void Servant::readHeaders(NamedList& msg, const char* prefix/* = NULL*/)
{
    XDebug(&plugin, DebugAll, "readHeaders(%s, %s) from '%s' [%p]", msg.c_str(), prefix, m_path.c_str(), this);
    char *eoline = 0;
    m_buf.resize(1024);
    int posinbuf = 0;
    int totalsize = 0;
    for (;;) {
	int readsize = m_in ? m_in->readData(m_buf.data(posinbuf),m_buf.length()-posinbuf-1) : 0;
	if (!readsize) {
	    if (m_in)
		Debug(DebugInfo,"Read EOF on %p [%p]",m_in,this);
	    closeIn();
	    break;
	}
	else if (readsize < 0) {
	    if (m_in && m_in->canRetry()) {
		Thread::idle();
		continue;
	    }
	    Debug(DebugWarn,"Read error %d on %p [%p]",errno,m_in,this);
	    break;
	}
	XDebug(DebugAll,"readHeaders() read %d",readsize);
	totalsize = readsize + posinbuf;
	*m_buf.data(totalsize)=0;

	eoline = ::strchr(static_cast<char*>(m_buf.data()),'\n');
	if (!eoline) {
	    Debug(DebugWarn,"Too long header line from '%s' [%p]",m_path.c_str(),this);
	    error();
	    return;
	}
	XDebug(DebugAll,"eoline=%p, data=%p",eoline,m_buf.data());
	if (eoline == m_buf.data())
	    break;
	*eoline = 0;
	if (eoline[-1] == '\r') {
	    if (eoline-1 == m_buf.data())
		break;
	    eoline[-1] = 0;
	}
	char* val = ::strchr(static_cast<char*>(m_buf.data()),':');
	if(! val) {
	    Debug(DebugWarn,"No colon in header line from '%s': '%s' [%p]",m_path.c_str(),static_cast<char*>(m_buf.data()),this);
	    error();
	    return;
	}
	*val++ = 0;
	while(::isspace(*val))
	    ++val;

	String hdr(prefix);
	hdr += static_cast<char*>(m_buf.data());
	if (0 == strcmp("Status", static_cast<char*>(m_buf.data()))) {
	    XDebug(DebugAll,"Setting status '%s' [%p]", val, this);
	    msg.setParam("status", val);
	}
	else {
	    XDebug(DebugAll,"Adding header '%s', value '%s' [%p]",static_cast<char*>(m_buf.data()), val, this);
	    msg.addParam(hdr, val);
	}

	totalsize -= eoline-static_cast<char*>(m_buf.data())+1;
	::memmove(m_buf.data(),eoline+1,totalsize+1);
	posinbuf = totalsize;
    }
    m_buf.cut(-(eoline - static_cast<char*>(m_buf.data())));
    m_buf.truncate(totalsize);
    XDebug(DebugAll,"Done reading headers from '%s', left %d bytes in buffer [%p]",m_path.c_str(),m_buf.length(),this);
}

bool Servant::create(const char *script, const NamedList& cfg)
{
    XDebug(&plugin, DebugAll, "create(%s) [%p]", m_path.c_str(), this);
#ifdef _WINDOWS
    return false;
#else
    int pid;
    HANDLE ext2yate[2];
    HANDLE yate2ext[2];
    int x;
    if (::pipe(ext2yate)) {
	Debug(DebugWarn, "Unable to create ext->yate pipe: %s",strerror(errno));
	return false;
    }
    if (pipe(yate2ext)) {
	Debug(DebugWarn, "unable to create yate->ext pipe: %s", strerror(errno));
	::close(ext2yate[0]);
	::close(ext2yate[1]);
	return false;
    }
    pid = ::fork();
    if (pid < 0) {
	Debug(DebugWarn, "Failed to fork(): %s", strerror(errno));
	::close(yate2ext[0]);
	::close(yate2ext[1]);
	::close(ext2yate[0]);
	::close(ext2yate[1]);
	return false;
    }
    if (!pid) {
	// In child - terminate all other threads if needed
	Thread::preExec();
	// Try to immunize child from ^C and ^\ the console may receive
	::signal(SIGINT,SIG_IGN);
	::signal(SIGQUIT,SIG_IGN);
	// And restore default handlers for other signals
	::signal(SIGTERM,SIG_DFL);
	::signal(SIGHUP,SIG_DFL);
	// Redirect stdin and out
	::dup2(yate2ext[0], STDIN_FILENO);
	::dup2(ext2yate[1], STDOUT_FILENO);
	// Blindly close everything but stdin/out/err
	for (x=STDERR_FILENO+1;x<1024;x++)
	    ::close(x);
	// Execute script
	if (debugAt(DebugInfo))
	    ::fprintf(stderr, "Executing '%s'\n", script);
	CGIEnv env;
	env.build(cfg);
	env.Set("SCRIPT_FILENAME", m_path.c_str());
        ::execle(script, script, NULL, env.ptr());
	::fprintf(stderr, "Failed to execute '%s': %s\n", script, strerror(errno));
	// Shit happened. Die as quick and brutal as possible
	::_exit(1);
    }
    Debug(DebugInfo,"Launched script '%s'",script);
    m_in = new File(ext2yate[0]);
    m_out = new File(yate2ext[1]);

    // close what we're not using in the parent
    close(ext2yate[1]);
    close(yate2ext[0]);
    m_pid = pid;
    return true;
#endif
}

void Servant::cleanup()
{
#ifdef DEBUG
    Debugger debug(DebugAll,"cleanup()"," [%p]",this);
#endif
#ifndef _WINDOWS
    if (m_pid > 1) {
	closeOut();
	int w = 0;
	u_int32_t stoptime = Time::secNow() + 100;
	do {
	    Thread::yield();
	    w = ::waitpid(m_pid, 0, WNOHANG);
	} while(!w && Time::secNow() < stoptime);
	if (w == 0) {
	    Debug(DebugWarn,"Process %d has not exited on closing stdin - we'll kill it",m_pid);
	    ::kill(m_pid,SIGTERM);
	    Thread::yield();
	    w = ::waitpid(m_pid, 0, WNOHANG);
	}
	if (w == 0)
	    Debug(DebugWarn,"Process %d has still not exited yet?",m_pid);
	else if ((w < 0) && (errno != ECHILD))
	    Debug(DebugMild,"Failed waitpid on %d: %s",m_pid,strerror(errno));
    }
    if (m_pid > 0)
	m_pid = 0;
#endif
}

void Servant::closeIn()
{
    Stream* tmp = m_in;
    if (tmp)
	tmp->terminate();
}

void Servant::closeOut()
{
    Stream* tmp = m_out;
    if (tmp)
	tmp->terminate();
}


}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */




/**
 * extmodule.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * External module handler
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Portions copyright (C) 2005 Maciek Kaminski
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

#ifdef _WINDOWS

#include <process.h>

#else
#include <yatepaths.h>

#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>


/* vi: set ts=8 sw=4 sts=4 noet: */
