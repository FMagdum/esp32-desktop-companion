#pragma once
#include <stdint.h>

class Conversation {
public:
  enum class Event { None, Prompt, Standby };
  bool active() const { return phase != Phase::Standby; }
  bool listening() const { return phase == Phase::FollowUp || phase == Phase::Prompted; }
  bool answeringPrompt() const { return phase == Phase::Prompted; }
  void stop() { phase = Phase::Standby; }
  void start() { phase = Phase::FirstTurn; }
  void acceptedTurn() { if (active()) phase = Phase::FinishingTurn; }

  Event update(uint32_t now, bool busy, bool wakeEachTurn, bool offerFollowUp) {
    if (!active() || busy) return Event::None;
    switch (phase) {
      case Phase::FirstTurn: return standby();
      case Phase::FinishingTurn:
        if (wakeEachTurn) return standby();
        phase = Phase::FollowUp;
        openedAt = now;
        break;
      case Phase::FollowUp:
        if (wakeEachTurn) return standby();
        if (uint32_t(now - openedAt) >= 8000) {
          if (!offerFollowUp) return standby();
          phase = Phase::PromptPlaying;
          return Event::Prompt;
        }
        break;
      case Phase::PromptPlaying:
        phase = Phase::Prompted;
        openedAt = now;
        break;
      case Phase::Prompted:
        if (wakeEachTurn || uint32_t(now - openedAt) >= 4000) return standby();
        break;
      case Phase::Standby: break;
    }
    return Event::None;
  }

private:
  enum class Phase { Standby, FirstTurn, FinishingTurn, FollowUp, PromptPlaying, Prompted };
  Phase phase = Phase::Standby;
  uint32_t openedAt = 0;
  Event standby() { stop(); return Event::Standby; }
};
