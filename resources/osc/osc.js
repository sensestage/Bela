var dgram = require('dgram');
var osc = require('osc-min');
// Default IPs and ports
var localIp = '0.0.0.0';
var localPort = 7563;
var remoteIp = '127.0.0.1';
var remotePort = 7562;

var args = process.argv;
if(args.length > 2){
	if(args[2] == 'help'){
		console.log([
		"Usage: ",
		"`node osc <remoteIp> <remotePort> <localIp> <localPort>`",
		"All parameters are optional, defaults to:",
		"`node osc "+remoteIp+" "+remotePort+" "+localIp+" "+localPort+"`"
		].join("\n"));
		return;
	}
	remoteIp = args[2];
	if(args.length > 3)
		remotePort = args[3];
	if(args.length > 4)
		localIp = args[4];
	if(args.length > 5)
		remotePort = args[5];
}

console.log("send to: "+remoteIp+":"+remotePort+", receive on: "+localIp+":"+localPort)


// socket to send and receive OSC messages from bela
var socket = dgram.createSocket('udp4');
socket.bind(localPort, localIp);
		
socket.on('message', (message, info) => {

	var msg = osc.fromBuffer(message);
	
	if (msg.oscType === 'message'){
		parseMessage(msg);
	} else if (msg.elements){
		for (let element of msg.elements){
			parseMessage(element);
		}
	}
	
});

var baseTimeString = "roundtripLatency";
var count = 0;
function parseMessage(msg){

	var address = msg.address.split('/');
	if (!address || !address.length || address.length <2){
		console.log('bad OSC address', address);
		return;
	}
	
	// setup handshake
	if (address[1] === 'osc-setup'){
		sendHandshakeReply();
		
		// start sending OSC messages to Bela
		setInterval(sendOscTest, 1000);
		
	} else if (address[1] === 'osc-acknowledge'){
		if(msg.args[0].type != 'integer'){
			console.log("Unexpected type for argument 0: " + msg.args[0].type);
			return;
		}
		var receivedCount = msg.args[0].value;
		console.timeEnd(baseTimeString + receivedCount);
		//console.log('received osc-acknowledge', msg.args);
	}
}

function sendOscTest(){
	var buffer = osc.toBuffer({
		address : '/osc-test',
		args 	: [
			{type: 'integer', value: count},
			{type: 'float', value: 3.14}
		]
	});
	socket.send(buffer, 0, buffer.length, remotePort, remoteIp, function(err) {
		console.time(baseTimeString + count);
		count++;
		if (err) console.log(err);
	});
}

function sendHandshakeReply(){
	var buffer = osc.toBuffer({ address : '/osc-setup-reply' });
	socket.send(buffer, 0, buffer.length, remotePort, remoteIp, function(err) {
		if (err) console.log(err);
	});
}
