#include "srsenb/hdr/empoweragent.h"
#include "srsenb/hdr/enb.h"

#include "srslte/common/threads.h"

#include <empoweragentproto/empoweragentproto.hh>

#include <iostream>

namespace Empower {
namespace Agent {

/// @brief Private attributes of the Empower agent
struct Agent::PrivateBits {
  /// @brief Identifier of the agent thread
  pthread_t agentThread;

  /// @name Configuration from srsenb configuration file/command line (see `init()`)

  /// @{

  /// @brief The IPv4 address of the controller (to be contacted by the agent)
  NetworkLib::IPv4Address controllerAddress;

  /// @brief The TCP port of the controller (to be contacted by the agent)
  std::uint16_t controllerPort;

  /// @brief Delay (in milliseconds) between sending out HELLO
  //         requests, and also the timeout when waiting for incoming
  //         requests.
  std::uint32_t delayms;

  /// The cell identifier (from enb.cell_id)
  std::uint16_t cellId;

  /// @}

  std::uint32_t sequence;
};

Agent::Agent(void)
{
  mPrivateBits = std::unique_ptr<PrivateBits>(new PrivateBits);
}

Agent::~Agent() {}

bool Agent::init(const srsenb::all_args_t& all_args)
{
  // Copy into our private bits the parameters of the whole srsenb
  // configuration we are interested in.
  try {
    mPrivateBits->controllerAddress = NetworkLib::IPv4Address(all_args.empoweragent.controller_addr);
    mPrivateBits->controllerPort    = all_args.empoweragent.controller_port;
    mPrivateBits->delayms           = all_args.empoweragent.delayms;

    // Take the cell_id
    mPrivateBits->cellId = all_args.stack.s1ap.enb_id;

    // Initialize the sequence number to be used when sending messages
    mPrivateBits->sequence = 1;

  } catch (std::exception& e) {
    std::cerr << "AGENT: *** caught exception while initializing Empower agent: " << e.what() << '\n';
    return true;
  }

  // No error occurred while initializing
  return false;
}

bool Agent::start()
{

  pthread_attr_t     attr;
  struct sched_param param;
  param.sched_priority = 0;
  pthread_attr_init(&attr);
  // pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
  pthread_attr_setschedparam(&attr, &param);

  // Start the agent thread, executing `mainLoop()` via `run()`.
  if (pthread_create(&(mPrivateBits->agentThread), &attr, run, reinterpret_cast<void*>(this))) {
    std::cerr << "AGENT: *** error starting Empower agent thread\n";
    return true;
  }

  // No errors
  return false;
}

void* Agent::run(void* arg)
{
  Agent* thisInstance = reinterpret_cast<Agent*>(arg);
  thisInstance->mainLoop();
  return nullptr;
}

void Agent::mainLoop()
{

  IO io;

  // Configure the TCP connection destination, and the delay/timeout
  io.address(mPrivateBits->controllerAddress).port(mPrivateBits->controllerPort).delay(mPrivateBits->delayms);

  try {
    // Allocate a couple of buffers to read and write messages, and obtain
    // a writable view on them.
    NetworkLib::BufferWritableView readBuffer  = io.makeMessageBuffer();
    NetworkLib::BufferWritableView writeBuffer = io.makeMessageBuffer();

    for (;;) {

      bool performPeriodicTasks = false;
      bool dataIsAvailable      = false;

      if (io.isConnectionClosed()) {
        // Try to open the TCP connection to the controller
        io.openSocket();
      }

      // Now retest if the connection is still closed.
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

          std::cout << "AGENT: received message\n" << messageBuffer;

          // Decode the message
          MessageDecoder messageDecoder(messageBuffer);

          if (!messageDecoder.isFailure()) {

            switch (messageDecoder.header().entityClass()) {
              case EntityClass::HELLO_SERVICE:
                // We expect a reply...
                if (messageDecoder.isRequest()) {
                  std::cerr << "AGENT: *** got unexpected REQUEST for HELLO_SERVICE\n";
                  break;
                }

                {
                  // We don't really care about the reply.
                  std::cout << "AGENT: got a REPLY for HELLO_SERVICE (discarded)\n";
                }
                break;

              case EntityClass::CAPABILITIES_SERVICE:
                if (!messageDecoder.isRequest()) {
                  std::cerr << "AGENT: *** got unexpected REPLY for the CAPABILITIES_SERVICE\n";
                  break;
                }

#if 0
                {
                    // Insert capabilities reply here
                    TLVCapabilities tlvCap;
                    // tlvCap.setThis(...).setThat(...).set(...);
                    MessageEncoder messageEncoder(writeBuffer);

                    setupCommonHeaderFields(messageEncoder.header());

                    messageEncoder.header()
                           .messageClass(MessageClass::RESPONSE_SUCCESS)
                           .entityClass(EntityClass::CAPABILITIES_SERVICE);

                    //


                }

#endif

                break;

              case EntityClass::ECHO_SERVICE:
                std::cout << "AGENT: got REQUEST for ECHO_SERVICE\n";

                {
                  TLVBinaryData tlv;
                  messageDecoder.get(tlv);
                  // tlv.stringData(tlv.stringData() + " Here I am!");

                  MessageEncoder messageEncoder(writeBuffer);

                  fillHeader(messageEncoder.header());

                  messageEncoder.header()
                      .messageClass(MessageClass::RESPONSE_SUCCESS)
                      .entityClass(EntityClass::ECHO_SERVICE);

                  messageEncoder.add(tlv).end();

                  std::cout << "AGENT: sending REPLY for ECHO_SERVICE\n";

                  // Write a message to the socket
                  size_t len = io.writeMessage(messageEncoder.data());

                  std::cout << "AGENT: wrote " << len << " bytes\n";
                }

                break;

              default:
                std::cerr << "AGENT: *** got unexpected message class\n";
                break;
            }
          }
        }
      } else if (performPeriodicTasks) {
        // Timeout expired
        std::cout << "AGENT: waiting for messages... "
                     "(isConnectionClosed() is "
                  << io.isConnectionClosed() << ")\n";

        if (!io.isConnectionClosed()) {
          // Among the periodic task, if the connection is
          // opened, send a HELLO request (including a
          // periodicity).
          TLVPeriodicityMs tlvPeriodicity;

          // Use the current I/O delay as the periodicity
          tlvPeriodicity.milliseconds(io.delay());

          MessageEncoder messageEncoder(writeBuffer);
          fillHeader(messageEncoder.header());
          messageEncoder.header().messageClass(MessageClass::REQUEST_SET).entityClass(EntityClass::HELLO_SERVICE);

          // Add the periodicity TLV to the message, and end
          // adding.
          messageEncoder.add(tlvPeriodicity).end();

          // Send the HELLO request
          size_t len = io.writeMessage(messageEncoder.data());
          std::cout << "AGENT: sent REQUEST for HELLO_SERVCE (" << len << " bytes)\n";

          // Dump the HELLO request being sent
          // std::cout << messageEncoder.data() << '\n';
        }

      } else {
        // We should never end here.
        // throw ...
      }
    }

  } catch (std::exception& e) {
    std::cerr << "AGENT: *** caught exception in main agent loop: " << e.what() << '\n';
  }
}

void Agent::fillHeader(CommonHeaderEncoder& headerEncoder)
{
  headerEncoder.sequence(mPrivateBits->sequence).cellIdentifier(mPrivateBits->cellId);
  ++(mPrivateBits->sequence);
}

} // namespace Agent
} // namespace Empower
