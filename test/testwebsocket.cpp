
/**
 * testwebsocket.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Test module for websocket interface
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
#include <string.h>
#include <stdio.h>

using namespace TelEngine;

namespace { // anonymous

const static char* MY_PROTOCOL_NAME = " megaecho";

class TestWebSocketModule : public Module
{
    enum {
	HttpRequest = Private,
	WebSocketInit = (Private << 1),
    };
protected:
    virtual bool received(Message &msg, int id);
    bool processMsg(Message& msg);
    bool serveRequest(Message& msg);
public:
    TestWebSocketModule();
    virtual ~TestWebSocketModule();
    virtual void initialize();
};

class EchoEndpoint: public DataEndpoint
{
    class DS: public DataSource
    {
    public:
	DS(): DataSource("data") { }
    };
    class DC: public DataConsumer
    {
    public:
	DC(DS& ds): DataConsumer("data"), m_ds(ds) { }
	virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags) { return m_ds.Forward(data, tStamp, flags); }
    private:
	DS& m_ds;
    };
public:
    EchoEndpoint(): m_dc(m_ds)
    {
	XDebug(DebugAll, "EchoEndpoint[%p] created", this);
	setSource(&m_ds);
	setConsumer(&m_dc);
    }
    ~EchoEndpoint()
    {
	XDebug(DebugAll, "EchoEndpoint[%p] destroyed", this);
    }
private:
    DS m_ds;
    DC m_dc;
};

/**
 * Local data
 */
static TestWebSocketModule plugin;


/**
 * TestWebSocketModule
 */
TestWebSocketModule::TestWebSocketModule()
    : Module("testwebsocket","misc",true)
{
    Output("Loaded module TestWebSocket");
}

TestWebSocketModule::~TestWebSocketModule()
{
    Output("Unloading module TestWebSocket");
}

void TestWebSocketModule::initialize()
{
    static bool notFirst = false;
    Output("Initializing module TestWebSocket");
    if (notFirst)
	return;
    notFirst = true;
    setup();
    installRelay(HttpRequest, "http.serve", 100);
    installRelay(WebSocketInit, "websocket.init", 100);
}

bool TestWebSocketModule::received(Message &msg, int id)
{
    XDebug(&plugin, DebugAll, "TestWebSocketModule::received[%p](%s = %d)", this, msg.c_str(), id);
    switch(id) {
    case WebSocketInit:
	return processMsg(msg);
    case HttpRequest:
	return serveRequest(msg);
    }
    return Module::received(msg, id);
}

bool TestWebSocketModule::processMsg(Message& msg)
{
    String proto = msg.getValue("protocol");
    ObjList* lst = proto.split(',');
    ObjList* item = lst->find(MY_PROTOCOL_NAME);
    TelEngine::destruct(lst);
    if (item) // not really used!
    {
	EchoEndpoint* e = new EchoEndpoint;
	msg.userData(e);
	e->deref();
	msg.retValue() = MY_PROTOCOL_NAME;
	return true;
    }
    return false;
}

const static char* wstest_html =
    "<!DOCTYPE html>\r\n"
    "<meta charset=\"utf-8\" />\r\n"
    "<title>WebSocket Test</title>\r\n"
    "<script language=\"javascript\" type=\"text/javascript\">\r\n"
    "var wsUri = \"ws://%s/ws/echo\";\r\n"
    "var output;\r\n"
    "function init()\r\n"
    "{\r\n"
    "  output = document.getElementById(\"output\");\r\n"
    "  testWebSocket();\r\n"
    "}\r\n"
    "function testWebSocket()\r\n"
    "{\r\n"
    "  websocket = new WebSocket(wsUri, Array(\"echo\", \"superecho\", \"megaecho\"));\r\n"
    "  websocket.onopen = function(evt) { writeToScreen(\"CONNECTED\"); doSend(\"WebSocket rocks\"); };\r\n"
    "  websocket.onclose = function(evt) { writeToScreen(\"DISCONNECTED\"); };\r\n"
    "  websocket.onmessage = function(evt) { writeToScreen('<span style=\"color: blue;\">RESPONSE: ' + evt.data+'</span>'); };\r\n"
    "  websocket.onerror = function(evt) { writeToScreen('<span style=\"color: red;\">ERROR:</span> ' + evt.data); };\r\n"
    "}\r\n"
    "function doSend(message)\r\n"
    "{\r\n"
    "  writeToScreen(\"SENT: \" + message); \r\n"
    "  websocket.send(message);\r\n"
    "}\r\n"
    "function doClose()\r\n"
    "{\r\n"
    "  websocket.close();\r\n"
    "}\r\n"
    "function writeToScreen(message)\r\n"
    "{\r\n"
    "  var pre = document.createElement(\"p\");\r\n"
    "  pre.style.wordWrap = \"break-word\";\r\n"
    "  pre.innerHTML = message;\r\n"
    "  output.appendChild(pre);\r\n"
    "}\r\n"
    "window.addEventListener(\"load\", init, false);\r\n"
    "</script>\r\n"
    "<h2>WebSocket Test</h2>\r\n"
    "<input type=\"text\" id=\"msg\" value=\"WebSocket rocks\" />\r\n"
    "<button onClick=\"doSend(document.getElementById('msg').value)\">Send</button>\r\n"
    "<button onClick=\"doClose()\">Disconnect</button>\r\n"
    "<div id=\"output\"></div>\r\n";

bool TestWebSocketModule::serveRequest(Message& msg)
{
    XDebug(&plugin, DebugAll, "TestWebSocketModule::serveRequest[%p](%s)", this, msg.c_str());
    if (YSTRING("GET") != msg.getValue("method"))
	return false;
    if (YSTRING("/ws/test.html") != msg.getValue("uri"))
	return false;
    String local_addr = msg.getValue("local");
    char* str = new char[strlen(wstest_html) + local_addr.length()];
    sprintf(str, wstest_html, local_addr.c_str());
    msg.retValue() = str;
    msg.setParam("status", "200");
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
