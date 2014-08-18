/**
 * sysvipc.cpp
 *
 * System V IPC for YATE's javascript
 *
 * MIT License.
 */

#include <yatengine.h>
//#include <yatescript.h>
#include "libs/yscript/yatescript.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

class SysVIPCObj : public JsObject
{
    YCLASS(SysVIPCObj,JsObject)
public:
    inline SysVIPCObj(Mutex* mtx)
	: JsObject("SysVIPCObj",mtx,true)
	{
	    Debug(DebugAll,"SysVIPCObj::SysVIPCObj(%p) [%p]",mtx,this);
	    params().addParam(new ExpFunction("queue"));
	    params().addParam(new ExpFunction("semaphore"));
	    params().addParam(new ExpFunction("shmem"));
	    params().addParam(new ExpFunction("ftok"));
	}
    virtual ~SysVIPCObj()
	{
	    Debug(DebugAll,"SysVIPCObj::~SysVIPCObj() [%p]",this);
	}
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
    static void initialize(ScriptContext* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

class SysVQueue : public JsObject
{
    YCLASS(SysVQueue,JsObject)
public:
    inline SysVQueue(Mutex* mtx, int key, int flags)
	: JsObject("SysVQueue", mtx, true)
	, m_key(key), m_owner(false)
	, m_blocking(true)
	{
	    Debug(DebugAll,"SysVQueue::SysVQueue(%p) [%p]",mtx,this);
	    params().addParam(new ExpFunction("id"));
	    params().addParam(new ExpFunction("key"));
	    params().addParam(new ExpFunction("stat"));
	    params().addParam(new ExpFunction("enqueue"));
	    params().addParam(new ExpFunction("dequeue"));
	    params().addParam(new ExpFunction("dequeueNb"));
	    params().addParam(new ExpFunction("remove"));
	    params().addParam(new ExpFunction("blocking"));
	    m_id = msgget(key, flags);
	}
    virtual ~SysVQueue()
	{
	    Debug(DebugAll,"SysVQueue::~SysVQueue() [%p]",this);
	    if(m_owner)
		remove();
	}
    int id() const
	{ return m_id; }
    bool ok() const
	{ return id() != -1; }
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);
protected:
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);
    void remove()
    {
	msgctl(m_id, IPC_RMID, 0);
    }
private:
    int m_key;
    bool m_owner;
    int m_id;
    bool m_blocking;
};

class SysVIPCHandler : public MessageHandler
{
public:
    SysVIPCHandler()
	: MessageHandler("script.init",90,"jsext")
	{ }
    virtual bool received(Message& msg);
};

class SysVIPCPlugin : public Plugin
{
public:
    SysVIPCPlugin();
    virtual void initialize();
private:
    SysVIPCHandler* m_handler;
};


JsObject* SysVIPCObj::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    Debug(DebugAll,"SysVIPCObj::runConstructor '%s'("FMT64") [%p]",oper.name().c_str(),oper.number(),this);
//    const char* val = 0;
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
//	    val = static_cast<ExpOperation*>(args[0])->c_str();
	    // fall through
//	case 0:
//	    return new SysVIPCObj(mutex(),val);
	default:
	    return 0;
    }
}

void SysVIPCObj::initialize(ScriptContext* context)
{
    Debug(DebugAll,"SysVIPCObj::initialize(%p)",context);
    if (!context)
	return;
    Mutex* mtx = context->mutex();
    Lock mylock(mtx);
    NamedList& params = context->params();
    if (!params.getParam(YSTRING("SysVIPC")))
	addObject(params,"SysVIPC",new SysVIPCObj(mtx));
//	addConstructor(params,"SysVIPC",new SysVIPCObj(mtx));
    else
	Debug(DebugInfo,"An SysVIPC already exists, nothing to do");
}

bool SysVIPCObj::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    if (oper.name() == YSTRING("queue")) {
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	int key = static_cast<ExpOperation*>(args[0])->toInteger();
	const char * mode = static_cast<ExpOperation*>(args[1])->c_str();
	int flags = 0;
	while(mode && *mode) {
	    switch(*mode) {
		case 'r': case 'R': flags |= S_IRUSR; break;
		case 'w': case 'W': flags |= S_IWUSR; break;
		case 'c': case 'C': flags |= IPC_CREAT; break;
		case 'x': case 'X': flags |= IPC_EXCL; break;
		default: return false;
	    }
	    ++mode;
	}
	SysVQueue* q = new SysVQueue(mutex(), key, flags);
	if (q->ok())
	    ExpEvaluator::pushOne(stack,new ExpWrapper(q));
	else {
	    delete q;
	    ExpEvaluator::pushOne(stack,JsParser::nullClone());
	}
    }
    else if (oper.name() == YSTRING("semaphore")) {
    }
    else if (oper.name() == YSTRING("shmem")) {
    }
    else if (oper.name() == YSTRING("ftok")) {
Output("Got call to SysVIPCObj::ftok()\n");
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	const char * path = static_cast<ExpOperation*>(args[0])->c_str();
	char proj = static_cast<ExpOperation*>(args[1])->c_str()[0];
	key_t k = ftok(path, proj);
	ExpOperation* op = new ExpOperation(String(k));
	if (!op)
	    op = new ExpWrapper(0,"SysVIPC");
	ExpEvaluator::pushOne(stack,op);
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}

/* ========== */
JsObject* SysVQueue::runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    Debug(DebugAll,"SysVQueue::runConstructor '%s'("FMT64") [%p]",oper.name().c_str(),oper.number(),this);
//    const char* val = 0;
    ObjList args;
    switch (extractArgs(stack,oper,context,args)) {
	case 1:
//	    val = static_cast<ExpOperation*>(args[0])->c_str();
	    // fall through
//	case 0:
//	    return new SysVIPCObj(mutex(),val);
	default:
	    return 0;
    }
}

bool SysVQueue::runNative(ObjList& stack, const ExpOperation& oper, GenObject* context)
{
    ObjList args;
    if (oper.name() == YSTRING("id")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(String(m_id), "id"));
    }
    else if (oper.name() == YSTRING("key")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	ExpEvaluator::pushOne(stack,new ExpOperation(String(m_key), "key"));
    }
    else if (oper.name() == YSTRING("stat")) {
	struct msqid_ds st;
	if(-1 == msgctl(m_id, IPC_STAT, &st))
	    return false;
	/* XXX TODO: convert structure into javascript object XXX */
    }
    else if (oper.name() == YSTRING("enqueue")) {
	if (extractArgs(stack,oper,context,args) != 2)
	    return false;
	ExpOperation* mtype = static_cast<ExpOperation*>(args[0]);
	ExpOperation* text = static_cast<ExpOperation*>(args[1]);
	if (!text)
	    return false;
	size_t len = text->length();
	struct msgbuf * buf = (struct msgbuf *)::malloc(sizeof(long) + len);
	buf->mtype = mtype->toLong();
	memcpy(buf->mtext, text->c_str(), len);
	int rc = msgsnd(m_id, buf, len, m_blocking ? 0 : IPC_NOWAIT);
	::free(buf);
	if(rc == -1)
	    return false;
    }
    else if (oper.name() == YSTRING("dequeue")) {
	const char * flags = NULL;
	long mtype = 0;
	long msize = 0;
	switch(extractArgs(stack,oper,context,args)) {
	    case 3:
		flags = static_cast<ExpOperation*>(args[2])->c_str();
	    case 2:
		mtype = static_cast<ExpOperation*>(args[1])->toInteger();
	    case 1:
		msize = static_cast<ExpOperation*>(args[0])->toLong();
		break;
	    default:
		return false;
	}
	int fl = 0;
	if(! m_blocking)
	    fl |= IPC_NOWAIT;
	while(flags && *flags) {
	    switch(*flags) {
		case 'w': case 'W': fl |= IPC_NOWAIT; break;
		case 'e': case 'E': fl |= MSG_NOERROR; break;
		case 'x': case 'X': fl |= MSG_EXCEPT; break;
		default: return false;
	    }
	    ++flags;
	}
	struct msgbuf * buf = (struct msgbuf *)::malloc(sizeof(long) + msize);
	int rc = msgrcv(m_id, buf, msize, mtype, fl);
	if(rc != -1) {
	    JsArray* jsa = new JsArray(context,mutex());
	    jsa->push(new ExpOperation((int64_t)buf->mtype));
	    jsa->push(new ExpOperation(String(buf->mtext, rc)));
	    ExpEvaluator::pushOne(stack,new ExpWrapper(jsa));
	}
	::free(buf);
	if(rc == -1) {
	    switch(errno) {
		case ENOMSG:
		    ExpEvaluator::pushOne(stack,JsParser::nullClone());
		    break;
		default:
		    return false;
	    }
	}
    }
    else if (oper.name() == YSTRING("remove")) {
	if (extractArgs(stack,oper,context,args) != 0)
	    return false;
	remove();
    }
    else if (oper.name() == YSTRING("blocking")) {
	switch (extractArgs(stack,oper,context,args)) {
	    case 0:
		ExpEvaluator::pushOne(stack,new ExpOperation(m_blocking));
		break;
	    case 1:
		m_blocking = static_cast<ExpOperation*>(args[0])->valBoolean();
		break;
	    default:
		return false;
	}
    }
    else
	return JsObject::runNative(stack,oper,context);
    return true;
}
/* ========== */

static const Regexp s_libs("\\(^\\|,\\)sysvipc\\($\\|,\\)");
static const Regexp s_objs("\\(^\\|,\\)SysVIPCObj\\($\\|,\\)");

bool SysVIPCHandler::received(Message& msg)
{
    ScriptContext* ctx = YOBJECT(ScriptContext,msg.userData());
    const String& lang = msg[YSTRING("language")];
    Debug(DebugInfo,"Received script.init, language: %s, context: %p",lang.c_str(),ctx);
    if ((lang && (lang != YSTRING("javascript"))) || !ctx)
	return false;
    bool ok = msg.getBoolValue(YSTRING("startup"))
	|| s_libs.matches(msg.getValue(YSTRING("libraries")))
	|| s_objs.matches(msg.getValue(YSTRING("objects")));
    if (ok)
	SysVIPCObj::initialize(ctx);
    return ok;
}


SysVIPCPlugin::SysVIPCPlugin()
    : Plugin("sysvipc",true), m_handler(0)
{
    Output("Hello, I am module SysVIPCPlugin");
}

void SysVIPCPlugin::initialize()
{
    Output("Initializing module SysVIPCPlugin");
    if (!m_handler)
	Engine::install((m_handler = new SysVIPCHandler));
}

INIT_PLUGIN(SysVIPCPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */

