Bson = require 'bson'
EventEmitter = require('events').EventEmitter

NodeDaylite = require('bindings')('node_daylite')

class DayliteClient extends EventEmitter
  constructor: ->
    @node  = NodeDaylite.NodeJSDayliteNode()

  join_daylite: (port) =>
  
    @node.start()
    
    @node.subscribe (format, length, width, image_data) =>
        console.log 'Got frame', format, length, width
        console.log image_data
        
        msg = 
            format: format
            length: length
            width: width
            data: image_data
        
        @emit 'data', '/aurora/frame', msg

  leave_daylite: =>
    @node = null
    return

  publish: (topic, msg) =>
    if @node?
      doc =
        topic: topic
        msg: msg

      # @client.write Bson.BSONPure.BSON.serialize(doc, false, true, true)

  subscribe: (topic, cb) =>

    @on 'data', (t, msg) ->
      cb(msg) if t is topic

module.exports =
  DayliteClient: DayliteClient
