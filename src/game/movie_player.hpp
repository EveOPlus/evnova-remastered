#pragma once
#include <string>
class SdlPlatform;

namespace game {
// SDL replacement for the named QuickTime movie modal. Local installation
// prepares a .mov.mpg companion; the original media remains untouched.
bool NovaMovie_Play(SdlPlatform &platform, const std::string &name);
} // namespace game
