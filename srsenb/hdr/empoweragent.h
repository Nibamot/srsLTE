#ifndef EMPOWER_AGENT_H
#define EMPOWER_AGENT_H

// For std::unique_ptr<T>
#include <memory>

// Forward declaration of the struct holding the configuration read
// from the cfg file or from the command line.
namespace srsenb {
struct all_args_t;
}

namespace Empower {
namespace Agent {

// Forward declaration (for Agent::
class CommonHeaderEncoder;

class Agent
{
public:
  Agent();
  ~Agent();

  /// @brief Initialize the agent
  bool init(const srsenb::all_args_t& all_args);

  /// @brief Start the agent main loop in its own thread.
  ///
  /// @return true on errors
  bool start();

private:
  // Helper method
  static void* run(void* arg);

  // The agent main loop.
  void mainLoop();

  // Private bits of the Agent (using PIMPL idiom).
  struct PrivateBits;
  std::unique_ptr<PrivateBits> mPrivateBits;

  // Fill the header of the messages being sent out
  void fillHeader(CommonHeaderEncoder& headerEncoder);
};

} // namespace Agent
} // namespace Empower

#endif
