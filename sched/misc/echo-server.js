#!/usr/bin/env node
var nextCIndex = 0;
require('net').createServer(function(socket) {

  var ci = ++nextCIndex;
  console.log('established connection', ci);

  socket.on("error", function (err) {
    console.error('err for connection', ci);
    console.error(err.stack);
  });
  socket.on('close', function(){
    console.log('lost connection', ci);
  });
  socket.write('hello\n');
  socket.pipe(socket);
}).listen(1337, '127.0.0.1');
console.log('listening at tcp://127.0.0.1:1337');
