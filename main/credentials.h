#pragma once

#if __has_include("wifi_credentials.local.h")
#include "wifi_credentials.local.h"
#else
#define F1_WIFI_SSID ""
#define F1_WIFI_PASSWORD ""
#endif

#if __has_include("openf1_credentials.local.h")
#include "openf1_credentials.local.h"
#else
#define OPENF1_LOGIN ""
#define OPENF1_PASSWORD ""
#endif
