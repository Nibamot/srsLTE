# Empower agent changes to srsenb

Start from `.../srsenb/src/empoweragent.cc` and `.../srsenb/hdr/empoweragent.h`, and
`.../srsenb/src/main.cc` and `.../srsenb/src/enb.cc`

1. The Empower agent is started in a single separate thread;

2. then it periodically attempts to (re)open a TCP connection to the
   address and port of the controller every `delay` microseconds
   (address, port and delay are specified in a new section of the
   srsenb configuration file -- see below).

   If no connection can be opened, the agent performs the periodic tasks.

3. once a connection is opened, the agent waits up to `delay` microseconds for
   an incoming message from the controller.

   If a message is availabile, it is received and processed immediately.

   If no message is available, the agent performs the periodic
   tasks. Since the connection is opened, in this case it sends to the
   controller a HELLO message specifying just the `delay` interval.


The srsenb configuration has been extended with a new section for the
Empower agent (those in the example below are the default values):

```
[empoweragent]
controller_addr = 127.0.0.1
controller_port = 2110
delayms = 1500

```


# Build

Build and install SRSLte as usual *after ensuring* that the
`empower-enb-agent` library and headers have been built and installed
in `/usr/local` (for the moment, the changes to the main
`CMakeLists.txt` explicitly look for the package `EmpowerAgentProto`
in `/usr/local/include/empoweragentproto` and `/usr/local/lib`
