#ifndef _NODEJS_DAYLITE_NODE_HPP_
#define _NODEJS_DAYLITE_NODE_HPP_

#include <mutex>
#include <queue>
#include <string>

#include <node.h>
#include <node_object_wrap.h>
#include <uv.h>

#include <daylite/node.hpp>
#include <daylite/bson.hpp>

#include <unordered_map>

namespace nodejs_daylite_node
{

class NodeJSDayliteNode : public node::ObjectWrap
{
    public:
        static void Init(v8::Handle<v8::Object> exports);

    private:
        explicit NodeJSDayliteNode();
        ~NodeJSDayliteNode();

        static void New(const v8::FunctionCallbackInfo<v8::Value> &args);
        static v8::Persistent<v8::Function> constructor;
        
        static void start(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void stop(const v8::FunctionCallbackInfo<v8::Value> &args);
        
        bool start(const char *ip, uint16_t port);
        bool stop();
        
        bool _started;
        std::shared_ptr<daylite::node> _node;
        
        // spinner worker
        static void spinner_work(uv_work_t *request);
        static void spinner_after(uv_work_t *request, int status);
        uv_work_t _spinner_baton;
        
        // async subscriber
        // This ensures that the callback is called in the context of the Node.js event loop
        uv_async_t _sub_async;
        static void handle_incoming_packages(uv_async_t *handle);
        
        // received package queue
        // required as multiple daylite callbacks might result in fewer uv async callbacks
        std::queue<v8::Local<v8::Object>> _sub_msg_queue;
        mutable std::mutex _sub_msg_queue_mutex;
        
        static void publish(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void subscribe(const v8::FunctionCallbackInfo<v8::Value> &args);
        v8::Local<v8::Object> process_bson(const daylite::bson &msg) const;
        v8::Persistent<v8::Function> _js_callback;
        
        std::unordered_map<std::string, std::shared_ptr<daylite::publisher> > _publishers;
        
        // the daylite subscriber callback
        static void generic_sub(const daylite::bson &msg, void *arg);
};

}

#endif // _NODEJS_DAYLITE_NODE_HPP_
