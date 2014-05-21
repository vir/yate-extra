/**
 * sipdate.cpp
 *
 * Add "Date" header to SIP REGISTER replyes (required for some ip phones).
 *
 * Date format: Sun, 06 Nov 1994 08:49:37 GMT
 *
 * MIT License.
 */

#include <yatephone.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

using namespace TelEngine;

namespace { // anonymous

class YSipDatePlugin: public Module
{
    enum MyRelays {
	UserRegister = Private
    };
public:
    YSipDatePlugin();
    virtual ~YSipDatePlugin();
    virtual void initialize();
protected:
    virtual bool received(Message &msg, int id);
};

static YSipDatePlugin iplugin;
static Configuration s_cfg;

/**
 * YSipDatePlugin
 */
YSipDatePlugin::YSipDatePlugin()
    : Module("sipdate","misc")
{
    Output("Loaded module SIPDATE");
}

YSipDatePlugin::~YSipDatePlugin()
{
    Output("Unloading module SIPDATE");
}

void YSipDatePlugin::initialize()
{
    static bool notFirst = false;
    Output("Initializing module SIPDATE");
    s_cfg = Engine::configFile("sipdate");
    s_cfg.load();
    if (notFirst)
	return;
    notFirst = true;
    int user_register_prio = s_cfg.getIntValue("handlers","user_register", 20);
    Debug(this,DebugAll,"Installing user.register handler at priority %d", user_register_prio);
    installRelay(UserRegister, "user.register", user_register_prio);
    setup();
}

const char * wdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
const char * months[] = { 0, "Jan", "Feb", "mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

bool YSipDatePlugin::received(Message &msg, int id)
{
    int year;
    unsigned int month, day, hour, minute, sec, wday;
    Time::toDateTime(Time::secNow(), year, month, day, hour, minute, sec, &wday);
    char buf[40];
    sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d GMT", wdays[wday], day, months[month], year, hour, minute, sec);
    msg.setParam("osip_Date", buf);
    return false;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
