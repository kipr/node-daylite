#include <node_buffer.h>

#include <daylite/spinner.hpp>

#include "nodejs_daylite_node.hpp"

#include <iostream>
#include <fstream>

namespace nodejs_daylite_node
{

using namespace v8;
using namespace std;
using namespace daylite;

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
    
    if(_started) return true;
    
    uv_async_init(uv_default_loop(), &_sub_async, handle_incoming_packages);
    
    if(!_node->start(ip, port))
    {
        isolate->ThrowException(Exception::Error(
            String::NewFromUtf8(isolate, "Could not start the daylite node")));
        return false;
    }
    _started = true;

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
    usleep(1000U);
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
            
            auto meta = msg->Get(String::New("meta"))->ToObject();
            auto topic = String::Utf8Value(meta->Get(String::New("topic"))->ToString());
            if(_js_callback->IsEmpty()) continue;
            
            const unsigned argc = 1;
            Local<Value> argv[argc] = { msg };
            
            Local<Function>::New(isolate, _js_callback)->Call(isolate->GetCurrentContext()->Global(), argc, argv);
        }
    }
}

void NodeJSDayliteNode::generic_sub(const bson &msg, void *arg)
{
    NodeJSDayliteNode* obj = static_cast<NodeJSDayliteNode *>(arg);

    // enqueue it
    {
        std::lock_guard<std::mutex> lock(obj->_sub_msg_queue_mutex);
        obj->_sub_msg_queue.push(process_bson(msg));
    }
    
    // notify the Node.js event loop that there is work to do
    uv_async_send(&obj->_sub_async);
}

v8::Local<v8::Object> NodeJSDayliteNode::process_bson(const bson &msg) const
{
  auto ret = v8::Object::New();
  bson_iter_t iter;
  bson_iter_init (&iter, doc);
  while(bson_iter_next(&iter))
  {
    auto key = v8::String::New(bson_iter_key(&iter));
    v8::Local<v8::Value> value;
    if(BSON_ITER_HOLDS_DOUBLE(&iter)) value = v8::Number::New(bson_iter_double(&iter));
    else if(BSON_ITER_HOLDS_INT32(&iter)) value = v8::Integer::New(bson_iter_int32(&iter));
    else if(BSON_ITER_HOLDS_INT64(&iter)) value = v8::Integer::New(bson_iter_int64(&iter));
    else if(BSON_ITER_HOLDS_BOOL(&iter)) value = v8::Boolean::New(bson_iter_bool(&iter));
    else if(BSON_ITER_HOLDS_BINARY(&iter))
    {
      bson_value_t *const v = bson_iter_value(&it);
      const size_t len = v->value.v_binary.data_len;
      value = node::Buffer::New(isolate, len);
      copy(v->value.v_binary.data, v->value.v_binary.data + len, node::Buffer::Data(value));
    }
    else if(BSON_ITER_HOLDS_ARRAY(&iter) || BSON_ITER_HOLDS_DOCUMENT(&iter))
    {
      bson_value_t *const v = bson_iter_value(&it);
      value = process_bson(bson_new_from_data(v->value.v_doc.data, v->value.v_doc.data_len));
    }
    else
    {
      cerr << "Value type not implemented" << endl;
    }
    ret->Set(key, value);
  }
}

bson NodeJSDayliteNode::process_obj(const Local<Object> &obj) const
{
  
}

void NodeJSDayliteNode::publish(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    NodeJSDayliteNode *const obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
    
    // args == 1 -> set callback
    if(args.Length() != 2)
    {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong number of arguments")));
      return;
    }
    
    auto topic = String::Utf8Value(args[0].As<String>());
    auto msg = args[0].As<Object>();
    
    auto it = _publishers.find(topic);
    if(it == _publishers.end())
    {
      it = _publishers.insert({topic, obj->_node.advertise(topic)});
    }
    it->second->publish();
    
    args.GetReturnValue().Set(Boolean::New(isolate, true));
}

void NodeJSDayliteNode::subscribe(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    NodeJSDayliteNode *const obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
    
    // args == 0 -> reset callback
    if(args.Length() == 0)
    {
      obj->_js_callback.Reset();
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
    
    obj->_js_callback.Reset(isolate, args[0].As<Function>());
    args.GetReturnValue().Set(Boolean::New(isolate, true));
}

}
