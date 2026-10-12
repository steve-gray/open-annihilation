// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The macOS half of the default-opener questions. file_types.cpp calls these; file_types_mac.mm
// defines them. Not a public header.
#pragma once

#include "oa/platform/file_types.hpp"

namespace oa::platform::file_types {

/// The system's own default-opener questions.
[[nodiscard]] HandlerCalls apple_handler_calls();

/// This copy, when it is an application bundle.
[[nodiscard]] Handler apple_running_application();

} // namespace oa::platform::file_types
