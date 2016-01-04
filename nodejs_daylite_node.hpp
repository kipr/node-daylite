#ifndef _NODEJS_DAYLITE_NODE_HPP_
#define _NODEJS_DAYLITE_NODE_HPP_

#include <mutex>
#include <queue>
#include <string>

#include <node.h>
#include <node_object_wrap.h>
#include <uv.h>

#include <daylite/node.hpp>

#include <aurora/aurora_frame.hpp>

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
        std::queue<aurora::aurora_frame> _sub_msg_queue;
        mutable std::mutex _sub_msg_queue_mutex;
        
        // Stores the JavaScript callback
        static void subscribe(const v8::FunctionCallbackInfo<v8::Value> &args);
        v8::Persistent<v8::Function> _js_subscriber_callback;
        
        // the daylite subscriber callback
        static void daylite_subscriber_callback(const bson_t *raw_msg, void *arg);
        
        // TODO: make me generic!
        
        // libaurora
        std::shared_ptr<daylite::subscriber> _aurora_frame_sub;
        
        static void publish_aurora_key(const v8::FunctionCallbackInfo<v8::Value> &args);
        std::shared_ptr<daylite::publisher> _aurora_key_pub;
        
        static void publish_aurora_mouse(const v8::FunctionCallbackInfo<v8::Value> &args);
        std::shared_ptr<daylite::publisher> _aurora_mouse_pub;
        
};

}

#endif // _NODEJS_DAYLITE_NODE_HPP_
