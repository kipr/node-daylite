
#include <node_buffer.h>

#include <daylite/spinner.hpp>

#include "nodejs_daylite_node.hpp"

#include <aurora_key.hpp>
#include <aurora_mouse.hpp>

#include <iostream>
#include <fstream>

namespace nodejs_daylite_node
{

using namespace v8;
using namespace std;
using namespace aurora;

namespace
{
    template<typename T>
    inline bson_bind::option<T> safe_unbind(const bson_t *raw_msg)
    {
        using namespace bson_bind;
        T ret;
        try
        {
            ret = T::unbind(raw_msg);
        }
        catch(const invalid_argument &e)
        {
            return none<T>();
        }

        return some(ret);
    }
}



void NodeJSDayliteNode::Init(Handle<Object> exports)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
    tpl->SetClassName(String::NewFromUtf8(isolate, "NodeJSDayliteNode"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    // Prototype
    NODE_SET_PROTOTYPE_METHOD(tpl, "start", start);
    NODE_SET_PROTOTYPE_METHOD(tpl, "stop", stop);
    
    NODE_SET_PROTOTYPE_METHOD(tpl, "publish_aurora_key", publish_aurora_key);
    NODE_SET_PROTOTYPE_METHOD(tpl, "publish_aurora_mouse", publish_aurora_mouse);
    NODE_SET_PROTOTYPE_METHOD(tpl, "subscribe", subscribe);

    constructor.Reset(isolate, tpl->GetFunction());
    exports->Set(String::NewFromUtf8(isolate, "NodeJSDayliteNode"), tpl->GetFunction());
}


NodeJSDayliteNode::NodeJSDayliteNode()
    : _started(false)
    , _node(daylite::node::create_node("harrogate"))
{
    _spinner_baton.data = this;
    
    _sub_async.data = this;
}

NodeJSDayliteNode::~NodeJSDayliteNode()
{
    stop();
}

void NodeJSDayliteNode::New(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.IsConstructCall())
    {
        // Invoked as constructor: `new NodeJSDayliteNode(...)`
        NodeJSDayliteNode *obj = new NodeJSDayliteNode();
        obj->Wrap(args.This());
        args.GetReturnValue().Set(args.This());
    } else {
        // Invoked as plain function `NodeJSDayliteNode(...)`, turn into construct call.
        Local<Function> cons = Local<Function>::New(isolate, constructor);
        args.GetReturnValue().Set(cons->NewInstance(0, 0));
    }
}

Persistent<Function> NodeJSDayliteNode::constructor;

void NodeJSDayliteNode::start(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if(args.Length() != 0)
    {
        isolate->ThrowException(Exception::TypeError(
            String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }
    
    NodeJSDayliteNode *obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
    bool success = obj->start("127.0.0.1", 8374);
    
    args.GetReturnValue().Set(Boolean::New(isolate, success));
}

void NodeJSDayliteNode::stop(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if(args.Length() != 0)
    {
        isolate->ThrowException(Exception::TypeError(
            String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }
    
    NodeJSDayliteNode *obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
    bool success = obj->stop();
    
    args.GetReturnValue().Set(Boolean::New(isolate, success));
}

bool NodeJSDayliteNode::start(const char *ip, uint16_t port)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    if(_started)
    {
        return true;
    }
    
    uv_async_init(uv_default_loop(), &_sub_async, handle_incoming_packages);
    
    if(!_node->start(ip, port))
    {
        isolate->ThrowException(Exception::Error(
            String::NewFromUtf8(isolate, "Could not start the daylite node")));
        return false;
    }
    _started = true;

    _aurora_frame_sub = _node->subscribe("/aurora/frame", &daylite_subscriber_callback, this);
    _aurora_mouse_pub = _node->advertise("/aurora/key");
    _aurora_key_pub = _node->advertise("/aurora/mouse");
    
    // start the spinner
    uv_queue_work(uv_default_loop(), &_spinner_baton, spinner_work, spinner_after);
    
    return true;
}

bool NodeJSDayliteNode::stop()
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    if(!_started)
    {
        return true;
    }
    
    
    if(!_node->stop())
    {
        isolate->ThrowException(Exception::Error(
            String::NewFromUtf8(isolate, "Could not stop the daylite node")));
        return false;
    }
    _started = false;
    
    return true;
}


// the worker run function
void NodeJSDayliteNode::spinner_work(uv_work_t *request)
{
    daylite::spinner::spin_once();
    
    // sleep a bit after worker
    Sleep(50);
}

// called after spinner_worker_run finishes
void NodeJSDayliteNode::spinner_after(uv_work_t *request, int status)
{
    NodeJSDayliteNode *obj = static_cast<NodeJSDayliteNode *>(request->data);
    
    // Re-schedule it again if we are not stopped
    if(obj->_started)
    {
        uv_queue_work(uv_default_loop(), request, spinner_work, spinner_after);
    }
}

void NodeJSDayliteNode::handle_incoming_packages(uv_async_t *handle)
{
    ofstream myfile;
    myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
    myfile << "Handle Package before scope\n";
    myfile.close();
    
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    NodeJSDayliteNode *obj = static_cast<NodeJSDayliteNode *>(handle->data);
    
    // dequeue all packages
    {
        std::unique_lock<std::mutex> lock(obj->_sub_msg_queue_mutex);
        while(!obj->_sub_msg_queue.empty())
        {
            // pop the message
            auto msg = obj->_sub_msg_queue.front();
            obj->_sub_msg_queue.pop();
        
            // call the JavaScript callback
            if(!obj->_js_subscriber_callback.IsEmpty())
            {
                myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
                myfile << "Creating Callback\n";
                myfile.close();
                
                auto fn = Local<Function>::New(isolate, obj->_js_subscriber_callback);
                
                myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
                myfile << "Calling Callback\n";
                myfile.close();
                
                auto image_data = node::Buffer::New(msg.data.size());
                copy(msg.data.begin(), msg.data.end(), node::Buffer::Data(image_data));
                
                const unsigned argc = 4;
                Local<Value> argv[argc] = {
                    String::NewFromUtf8(isolate, msg.format.c_str()),
                    Number::New(isolate, msg.width),
                    Number::New(isolate, msg.height),
                    image_data };
                
                fn->Call(isolate->GetCurrentContext()->Global(), argc, argv);
            }
        }
    }
    
    myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
    myfile << "Handle Package after scope\n";
    myfile.close();
}

void NodeJSDayliteNode::daylite_subscriber_callback(const bson_t *raw_msg, void *arg)
{
    ofstream myfile;
    myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
    myfile << "Got Frame\n";
    myfile.close();
    
    NodeJSDayliteNode* obj = static_cast<NodeJSDayliteNode *>(arg);
    
    // unwrap the message
    const auto msg_option = safe_unbind<aurora_frame>(raw_msg);

    if(msg_option.some())
    {
        myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
        myfile << "Enqueue it\n";
        myfile.close();
        
        // enqueue it
        {
            std::lock_guard<std::mutex> lock(obj->_sub_msg_queue_mutex);
            obj->_sub_msg_queue.push(msg_option.unwrap());
        }
        
        myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
        myfile << "Notifying\n";
        myfile.close();
        
        // notify the Node.js event loop that there is work to do
        uv_async_send(&obj->_sub_async);
    }
    
    myfile.open ("C:\\Users\\stefa_000\\Desktop\\example.txt", ios::out | ios::app);
    myfile << "scheduled async\n";
    myfile.close();
}

void NodeJSDayliteNode::subscribe(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    NodeJSDayliteNode *obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
    
    // args == 0 -> reset callback
    if(args.Length() == 0)
    {
        obj->_js_subscriber_callback.Reset();
        args.GetReturnValue().Set(Boolean::New(isolate, true));
        return;
    }
    
    // args == 1 -> set callback
    if(args.Length() != 1)
    {
        isolate->ThrowException(Exception::TypeError(
            String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }

    if(!args[0]->IsFunction())
    {
        isolate->ThrowException(Exception::TypeError(
            String::NewFromUtf8(isolate, "Wrong arguments")));
        return;
    }
    
    obj->_js_subscriber_callback.Reset(isolate, args[0].As<Function>());

    args.GetReturnValue().Set(Boolean::New(isolate, true));
}

void NodeJSDayliteNode::publish_aurora_key(const FunctionCallbackInfo<Value> &args)
{
  Isolate* isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

  NodeJSDayliteNode *obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
  //success = n->start("127.0.0.1", 8374);

  //args.GetReturnValue().Set(Boolean::New(isolate, success));
}

void NodeJSDayliteNode::publish_aurora_mouse(const FunctionCallbackInfo<Value> &args)
{
  Isolate* isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

  NodeJSDayliteNode *obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
  //success = n->start("127.0.0.1", 8374);

  //args.GetReturnValue().Set(Boolean::New(isolate, success));
}

}