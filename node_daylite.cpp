#include <iostream>

#include <node.h>
#include "nodejs_daylite_node.hpp"

using namespace v8;

void InitAll(Handle<Object> exports)
{
    nodejs_daylite_node::NodeJSDayliteNode::Init(exports);
}

NODE_MODULE(node_daylite, InitAll)