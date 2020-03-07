#include "srsenb/hdr/agent.h"
#include "srsenb/hdr/enb.h"

#include <empoweragentproto/empoweragentproto.hh>

#include <iostream>

namespace Empower {
namespace Agent {

agent::agent(srslte::logger* logger_) : logger(logger_)
{
}

agent::~agent() {}

bool agent::init(const srsenb::all_args_t& all_args)
{

	agent_log.init("AGENT", logger);
	agent_log.set_level(srslte::LOG_LEVEL_INFO);
	agent_log.set_hex_limit(32);

    controller_address = NetworkLib::IPv4Address(all_args.agent.controller_address);
    controller_port    = all_args.agent.controller_port;
    delayms            = all_args.agent.delayms;

	// Configure the TCP connection destination, and the delay/timeout
	io.address(controller_address).port(controller_port).delay(delayms);

    // Take the pci
    pci = all_args.enb.pci;

    // Take the dl_earfcn
    dl_earfcn = all_args.enb.dl_earfcn;

    // Take the ul_earfcn
    ul_earfcn = all_args.enb.ul_earfcn;

    // Take the n_prbs
    n_prb = all_args.enb.n_prb;

    // Take the enb_id
    enb_id = all_args.stack.s1ap.enb_id;

    // Initialize the sequence number to be used when sending messages
    sequence = 1;

    return false;

}

bool agent::start()
{

  pthread_attr_t     attr;
  struct sched_param param;
  param.sched_priority = 0;
  pthread_attr_init(&attr);
  // pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
  pthread_attr_setschedparam(&attr, &param);

  // Start the agent thread, executing `mainLoop()` via `run()`.
  if (pthread_create(&(agent_thread), &attr, run, reinterpret_cast<void*>(this))) {
    std::cerr << "AGENT: *** error starting Empower agent thread\n";
    return true;
  }

  // No errors
  return false;
}

void* agent::run(void* arg)
{
  agent* this_instance = reinterpret_cast<agent*>(arg);
  this_instance->main_loop();
  return nullptr;
}

void agent::send_hello_request()
{

	try {

		// Allocate a couple of buffers to read and write messages, and obtain
		// a writable view on them.
		//NetworkLib::BufferWritableView readBuffer  = io.makeMessageBuffer();
		Empower::NetworkLib::BufferWritableView writeBuffer = io.makeMessageBuffer();

	    MessageEncoder messageEncoder(writeBuffer);

	    fill_header(messageEncoder.header());

	    messageEncoder.header()
	    	.messageClass(MessageClass::REQUEST_SET)
			.entityClass(EntityClass::HELLO_SERVICE);

		TLVPeriodicityMs tlvPeriodicity;
		tlvPeriodicity.milliseconds(io.delay());

		// Add the period TLV to the message
		messageEncoder.add(tlvPeriodicity).end();

	    // Send the HELLO_SERVICE Request message
	    size_t len = io.writeMessage(messageEncoder.data());

	    agent_log.info("Sending Request for HELLO_SERVICE (%lu bytes)", len);

	} catch (std::exception& e) {
		agent_log.error("Error while sending response (%s)", e.what());
	}

}


void agent::send_caps_response()
{

	try {

		// Allocate a couple of buffers to read and write messages, and obtain
		// a writable view on them.
		//NetworkLib::BufferWritableView readBuffer  = io.makeMessageBuffer();
		Empower::NetworkLib::BufferWritableView writeBuffer = io.makeMessageBuffer();

	    MessageEncoder messageEncoder(writeBuffer);

	    fill_header(messageEncoder.header());
	    messageEncoder.header()
	        .messageClass(MessageClass::RESPONSE_SUCCESS)
	        .entityClass(EntityClass::CAPABILITIES_SERVICE);

	    // Add the cells TLV to the message
	    TLVCell tlvCell;
	    tlvCell
	      .pci(pci)
	      .nPrb(n_prb)
	      .dlEarfcn(dl_earfcn)
	      .ulEarfcn(ul_earfcn);

	    messageEncoder.add(tlvCell).end();

	    // Send the CAP Response massage
	    size_t len = io.writeMessage(messageEncoder.data());

	    agent_log.info("Sending RESPONSE for CAPABILITIES_SERVICE (%lu bytes)", len);

	} catch (std::exception& e) {
		agent_log.error("Error while sending response (%s)", e.what());
	}

}

void agent::send_ue_reports()
{

	try {

		// Allocate a couple of buffers to read and write messages, and obtain
		// a writable view on them.
		//NetworkLib::BufferWritableView readBuffer  = io.makeMessageBuffer();
		NetworkLib::BufferWritableView writeBuffer = io.makeMessageBuffer();

	    MessageEncoder messageEncoder(writeBuffer);

	    fill_header(messageEncoder.header());
	    messageEncoder.header()
	        .messageClass(MessageClass::RESPONSE_SUCCESS)
	        .entityClass(EntityClass::UE_REPORTS_SERVICE);

	    std::map<uint16_t, user>::iterator it;

	    for (it = users.begin(); it != users.end(); it++) {

	    	// Add the UE Report TLV to the message
	    	TLVUEReport tlvUeReport;
	    	tlvUeReport
		      .rnti(0);

	    	messageEncoder.add(tlvUeReport);

	    }

	    messageEncoder.end();

	    // Send the UE_REPORTS_SERVICE Response massage
	    size_t len = io.writeMessage(messageEncoder.data());

	    // Send the UE Reports message
	    agent_log.info("Sending RESPONSE for UE_REPORTS_SERVICE (%lu bytes)", len);

	} catch (std::exception& e) {
		agent_log.error("Error while sending response (%s)", e.what());
	}

}

void agent::main_loop() {

	try {

		// Allocate a couple of buffers to read and write messages, and obtain
		// a writable view on them.
		NetworkLib::BufferWritableView readBuffer = io.makeMessageBuffer();
		NetworkLib::BufferWritableView writeBuffer = io.makeMessageBuffer();

		for (;;) {

			bool performPeriodicTasks = false;
			bool dataIsAvailable = false;

			if (io.isConnectionClosed()) {
				// Try to open the TCP connection to the controller
				io.openSocket();
			}

			// Now test again if the connection is still closed.
			if (io.isConnectionClosed()) {
				// Connection still closed. Sleep for a while, and remember to
				// perform the periodic tasks.
				io.sleep();
				performPeriodicTasks = true;
			} else {
				// The connection is opened. Let's see if there's data to be read
				// (waiting for the timeout).
				dataIsAvailable = io.isDataAvailable();

				if (!dataIsAvailable) {
					// Timeout expired. Remember to perform the periodic tasks.
					performPeriodicTasks = true;
				}
			}

			if (dataIsAvailable) {

				// Read a message
				auto messageBuffer = io.readMessage(readBuffer);

				if (!messageBuffer.empty()) {

					// Decode the message
					MessageDecoder messageDecoder(messageBuffer);

					if (!messageDecoder.isFailure()) {

						uint32_t msg = unsigned(messageDecoder.header().entityClass());
						uint32_t seq = messageDecoder.header().sequence();
						uint32_t xid = messageDecoder.header().transactionId();

						agent_log.info("Received message type %u xid %u seq %u", msg, xid, seq);

						switch (messageDecoder.header().entityClass()) {
						case EntityClass::HELLO_SERVICE: {
							break;
						}
						case EntityClass::CAPABILITIES_SERVICE: {
							send_caps_response();
							break;
						}
						case EntityClass::UE_REPORTS_SERVICE: {
							ue_reports = true;
							ue_reports_xid = xid;
							break;
						}
						default:
							agent_log.error("Unexpected message class");
							break;
						}
					}
				}

			} else if (performPeriodicTasks) {

				if (!io.isConnectionClosed()) {
					send_hello_request();
				}

			} else {
				// We should never end here.
				// throw ...
			}
		}

	} catch (std::exception &e) {
		std::cerr << "AGENT: *** caught exception in main agent loop: "
				<< e.what() << '\n';
	}
}

void agent::fill_header(CommonHeaderEncoder& headerEncoder)
{
  headerEncoder.sequence(sequence).elementId(static_cast<std::uint64_t>(enb_id));
  ++sequence;
}

} // namespace Agent
} // namespace Empower
