#pragma once
#include <Arduino.h>
#include "../ui/RemoteSettings.h"
class HTTPClient;
class NetworkClient;
namespace CompanionAssistant {
enum class Connection : uint8_t { Checking, Offline, Ready, ApiError };
struct Snapshot {
  Connection connection = Connection::Checking;
  char connectionMessage[80] = "Checking assistant bridge";
  uint32_t musicNavigation = 0, chatNavigation = 0;
  uint32_t acceptedTurns = 0, standbyRequests = 0;
  bool music = false, prompt = false, homeTurn = false;
  bool configured = false, busy = false, visible = false, replyReady = false;
  char status[64] = "Configure assistant.json";
  char transcript[1024] = "";
  char reply[1024] = "";
  uint32_t revision = 0;
};
void begin(bool storageAvailable); // Read configuration before starting Sound.
Snapshot snapshot();
int requestArtwork(HTTPClient &http, NetworkClient &socket, const char *videoId, const char *source);
bool askLast(bool music = false, bool answeringPrompt = false, bool homeTurn = false);
bool sayFollowUp(bool homeTurn);
void cancelHomeTurn();
void setConversation(bool active, bool keepRecentContext = false);
int takeVolumeRequest();
void finishVolumeRequest(bool ok);
void clearText();
void setDeviceStatus(const RemoteSettings::Status &status);
bool takeCommand(RemoteSettings::Command &command);
bool playReadyReply();
}
