#pragma once

#include "esp_err.h"
#include "f1_types.h"

#include <vector>

namespace openf1 {

// Optional. Without credentials, only unauthenticated requests are made.
void set_credentials(const char *login, const char *password);

// Blocking calls; must not be invoked from the UI/touch context.
esp_err_t fetch_meetings(int year, std::vector<f1::Meeting> *meetings);
esp_err_t fetch_sessions(uint32_t meeting_key, std::vector<f1::Session> *sessions);

}  // namespace openf1
