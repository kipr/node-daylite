Bson = require 'bson'
EventEmitter = require('events').EventEmitter

NodeDaylite = require('bindings')('node_daylite')

class DayliteClient extends EventEmitter
  constructor: ->
    @node  = NodeDaylite.NodeJSDayliteNode()

  join_daylite: (port) =>
    @node.start()
    @node.subscribe (msg) =>
        console.log 'Got msg', msg
        @emit 'data', msg.meta.topic, msg.msg

  leave_daylite: =>
    @node = null
    return

  publish: (topic, msg) =>
    if @node?
      @node.write topic, msg

  subscribe: (topic, cb) =>
    @on 'data', (t, msg) ->
      cb(msg) if t is topic

module.exports =
  DayliteClient: DayliteClient
