#pragma once

#include "rs_ui/Family.h"

/* RoughTrack's look is the family look it shares with RoughStomp: dark panels,
   cream lettering and faders with real caps. It lives in RoughStomp
   (ui/include/rs_ui/Family.h, fetched in cmake/Dependencies.cmake), so the two
   apps can't drift apart. Change it there, not here. */

namespace console = rs::ui::family;   // console::panel, console::cream, console::amber, ...

using ConsoleLookAndFeel = rs::ui::FamilyLookAndFeel;
