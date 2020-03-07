#ifndef EMPOWER_AGENT_H
#define EMPOWER_AGENT_H

#include <memory>

#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/common/threads.h"
#include "srslte/common/log.h"
#include "srslte/common/logger_file.h"
#include "srslte/common/log_filter.h"

#include <empoweragentproto/empoweragentproto.hh>

#include <iostream>
#include <map>

// Forward declaration of the struct holding the configuration read
// from the cfg file or from the command line.
namespace srsenb {
struct all_args_t;
}

namespace Empower {
namespace Agent {

class user
{
	uint32_t plmnid;
	uint32_t m_tmsi;
	uint16_t rnti;
};

// Forward declaration (for Agent::
class CommonHeaderEncoder;

class agent : public srsenb::agent_interface_rrc
{
public:
  agent(srslte::logger* logger_);
  ~agent();

  /// @brief Initialize the agent
  bool init(const srsenb::all_args_t& all_args);

  /// @brief Start the agent main loop in its own thread.
  ///
  /// @return true on errors
  bool start();

  void add_user(uint16_t rnti) {

	  agent_log.info("ADDING: %u", rnti);

  }
  void rem_user(uint16_t rnti) {

	  agent_log.info("REMOVING: %u", rnti);

  }

  // The logger
  srslte::logger* logger = nullptr;
  srslte::log_filter agent_log;

private:

  // Helper method
  static void* run(void* arg);

  // The agent main loop.
  void main_loop();

  // Send CAP response.
  void send_hello_request();

  // Send CAPs response.
  void send_caps_response();

  // Send UE Reports
  void send_ue_reports();

  // I/O Socket
  IO io;

  // Identifier of the agent thread
  pthread_t agent_thread;

  // The IPv4 address of the controller (to be contacted by the agent)
  NetworkLib::IPv4Address controller_address;

  // The TCP port of the controller (to be contacted by the agent)
  std::uint16_t controller_port;

  // The hello period
  std::uint32_t delayms;

  /// The cell identifier (from enb.pci)
  std::uint16_t pci;

  /// The cell dl_earfcn (from rf.dl_earfcn)
  std::uint32_t dl_earfcn;

  /// The cell ul_earfcn (from rf.ul_earfcn)
  std::uint32_t ul_earfcn;

  /// The cell n_prb (from enb.n_prb)
  std::uint8_t n_prb;

  /// The eNodeB identifier (from enb.enb_id)
  std::uint32_t enb_id;

  // The sequence number
  std::uint32_t sequence;

  /// The User Equipments
  std::map<uint16_t, user> users;

  /// UE Reports
  bool ue_reports;
  std::uint32_t ue_reports_xid;

  // Fill the header of the messages being sent out
  void fill_header(CommonHeaderEncoder& headerEncoder);

};

} // namespace Agent
} // namespace Empower

#endif
