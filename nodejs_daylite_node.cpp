#include <node_buffer.h>

#include <daylite/spinner.hpp>

#include "nodejs_daylite_node.hpp"

#include <iostream>
#include <fstream>

namespace nodejs_daylite_node
{

using namespace v8;
using namespace std;

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
    
    NODE_SET_PROTOTYPE_METHOD(tpl, "publish", publish);
    NODE_SET_PROTOTYPE_METHOD(tpl, "set_callback", set_callback);
    NODE_SET_PROTOTYPE_METHOD(tpl, "subscribe", subscribe);
    NODE_SET_PROTOTYPE_METHOD(tpl, "unsubscribe", unsubscribe);

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
    
    cout << "Stopping daylite..." << endl;
    
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
            auto o = obj->process_bson(msg);
            
            if(obj->_js_callback.IsEmpty()) continue;
            
            const unsigned argc = 1;
            Local<Value> argv[argc] = { o };
            
            auto global_isolate = isolate->GetCurrentContext()->Global();
            Local<Function>::New(isolate, obj->_js_callback)->Call(global_isolate, argc, argv);
        }
    }
}

void NodeJSDayliteNode::generic_sub(const daylite::bson &msg, void *arg)
{
    NodeJSDayliteNode* obj = static_cast<NodeJSDayliteNode *>(arg);

    // enqueue it
    {
        std::lock_guard<std::mutex> lock(obj->_sub_msg_queue_mutex);
        obj->_sub_msg_queue.push(msg);
    }
    
    // notify the Node.js event loop that there is work to do
    uv_async_send(&obj->_sub_async);
}

v8::Local<v8::Object> NodeJSDayliteNode::process_bson(const daylite::bson &msg) const
{
  Isolate *isolate = Isolate::GetCurrent();
  auto ret = v8::Object::New(isolate);
  bson_iter_t iter;
  bson_iter_init (&iter, msg);
  while(bson_iter_next(&iter))
  {
    auto key = v8::String::NewFromUtf8(isolate, bson_iter_key(&iter));
    v8::Local<v8::Value> value;
    if(BSON_ITER_HOLDS_DOUBLE(&iter)) value = v8::Number::New(isolate, bson_iter_double(&iter));
    else if(BSON_ITER_HOLDS_INT32(&iter)) value = v8::Integer::New(isolate, bson_iter_int32(&iter));
    else if(BSON_ITER_HOLDS_INT64(&iter)) value = v8::Integer::New(isolate, bson_iter_int64(&iter));
    else if(BSON_ITER_HOLDS_BOOL(&iter)) value = v8::Boolean::New(isolate, bson_iter_bool(&iter));
    else if(BSON_ITER_HOLDS_UTF8(&iter))
    {
      uint32_t length = 0;
      const char *const str = bson_iter_utf8(&iter, &length);
      value = v8::String::NewFromUtf8(isolate, str, String::kNormalString, length);
    }
    else if(BSON_ITER_HOLDS_BINARY(&iter))
    {
      const bson_value_t *const v = bson_iter_value(&iter);
      const size_t len = v->value.v_binary.data_len;
      value = node::Buffer::New(isolate, len).ToLocalChecked();
      copy(v->value.v_binary.data, v->value.v_binary.data + len, node::Buffer::Data(value));
    }
    else if(BSON_ITER_HOLDS_ARRAY(&iter) || BSON_ITER_HOLDS_DOCUMENT(&iter))
    {
      const bson_value_t *const v = bson_iter_value(&iter);
      value = process_bson(bson_new_from_data(v->value.v_doc.data, v->value.v_doc.data_len));
    }
    else
    {
      cerr << "Value type not implemented" << endl;
    }
    ret->Set(key, value);
  }
  return ret;
}

daylite::bson NodeJSDayliteNode::process_obj(const Local<Object> &obj) const
{
  daylite::bson ret(bson_new());
  Local<Array> property_names = obj->GetOwnPropertyNames();
  for(size_t i = 0; i < property_names->Length(); ++i)
  {
    Local<Value> key = property_names->Get(i);
    Local<Value> value = obj->Get(key);
    assert(key->IsString());
    auto key_str = key->ToString();
    auto key_data = *String::Utf8Value(key_str);
    if(value->IsString())
    {
      auto val_str = value->ToString();
      bson_append_utf8(ret, key_data, key_str->Length(), *String::Utf8Value(val_str), val_str->Length());
    }
    else if(value->IsBoolean())
    {
      bson_append_bool(ret, key_data, key_str->Length(), value->ToBoolean()->Value());
    }
    else if(value->IsNumber())
    {
      if(value->IsInt32())
      {
        auto i32 = value->ToInt32();
        bson_append_int32(ret, key_data, key_str->Length(), i32->Value());
        continue;
      }
      
      auto i64 = value->ToInteger();
      if(i64.IsEmpty())
      {
        bson_append_int64(ret, key_data, key_str->Length(), i64->Value());
        continue;
      }
      
      bson_append_double(ret, key_data, key_str->Length(), value->ToNumber()->Value());
    }
    else if(value->IsObject())
    {
      daylite::bson subdoc = process_obj(value->ToObject());
      bson_append_document(ret, key_data, key_str->Length(), subdoc);
    }
  }
  return ret;
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
    
    auto topic = *String::Utf8Value(args[0]);
    auto msg = args[1].As<Object>();
    
    auto it = obj->_publishers.find(topic);
    shared_ptr<daylite::publisher> pub;
    if(it == obj->_publishers.end())
    {
      pub = obj->_node->advertise(topic);
      obj->_publishers.insert({topic, pub});
    }
    else pub = it->second;
    
    
    pub->publish(obj->process_obj(msg));
    
    args.GetReturnValue().Set(Boolean::New(isolate, true));
}

void NodeJSDayliteNode::set_callback(const FunctionCallbackInfo<Value> &args)
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

void NodeJSDayliteNode::subscribe(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    NodeJSDayliteNode *const obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
    
    // args == 1 -> subscriber
    if(args.Length() != 1)
    {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong number of arguments")));
      return;
    }

    if(!args[0]->IsString())
    {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong arguments")));
      return;
    }
    
    auto str = std::string(*String::Utf8Value(args[0].As<String>()));
    
    if(obj->_subscribers.find(str) != obj->_subscribers.end()) return;
    auto sub = obj->_node->subscribe(str, &NodeJSDayliteNode::generic_sub, obj);
    sub->set_send_packed(true);
    obj->_subscribers.insert({str, sub});
    args.GetReturnValue().Set(Boolean::New(isolate, true));
}

void NodeJSDayliteNode::unsubscribe(const FunctionCallbackInfo<Value> &args)
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    
    NodeJSDayliteNode *const obj = ObjectWrap::Unwrap<NodeJSDayliteNode>(args.Holder());
    
    // args == 1 -> subscriber
    if(args.Length() != 1)
    {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong number of arguments")));
      return;
    }

    if(!args[0]->IsString())
    {
      isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong arguments")));
      return;
    }
    
    auto str = std::string(*String::Utf8Value(args[0].As<String>()));
    auto it = obj->_subscribers.find(str);
    if(it == obj->_subscribers.end())
    {
      args.GetReturnValue().Set(Boolean::New(isolate, false));
      return;
    }

    obj->_subscribers.erase(it);
    args.GetReturnValue().Set(Boolean::New(isolate, true));
}

}
