#include "openf1_client.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace openf1 {
namespace {

constexpr char BASE_URL[] = "https://api.openf1.org/v1";
constexpr char TOKEN_URL[] = "https://api.openf1.org/token";
constexpr int TOKEN_EXPIRY_MARGIN_S = 60;
constexpr int DEFAULT_TOKEN_LIFETIME_S = 3600;
static const char *TAG = "openf1";

std::string s_login;
std::string s_password;
std::string s_token;
time_t s_token_expiry = 0;

time_t parse_utc_time(const std::string &value)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
                    &year, &month, &day, &hour, &minute, &second) != 6) {
        return 0;
    }
    struct tm calendar = {};
    calendar.tm_year = year - 1900;
    calendar.tm_mon = month - 1;
    calendar.tm_mday = day;
    calendar.tm_hour = hour;
    calendar.tm_min = minute;
    calendar.tm_sec = second;
    return timegm(&calendar);
}

std::string json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

uint32_t json_uint(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) && item->valuedouble > 0 ? static_cast<uint32_t>(item->valuedouble) : 0U;
}

bool json_bool(const cJSON *object, const char *name)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, name));
}

std::string url_encode(const std::string &value)
{
    std::string out;
    char buf[4];
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr && event->user_data != nullptr) {
        static_cast<std::string *>(event->user_data)->append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
}

esp_err_t http_request(const std::string &url, const char *post_body, const std::string &bearer,
                       std::string *response, int *status_out)
{
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 12000;
    config.buffer_size = 4096;
    config.buffer_size_tx = 2048;
    config.event_handler = http_event_handler;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.method = post_body != nullptr ? HTTP_METHOD_POST : HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (post_body != nullptr) {
        esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, post_body, static_cast<int>(std::strlen(post_body)));
    }
    if (!bearer.empty()) {
        esp_http_client_set_header(client, "Authorization", ("Bearer " + bearer).c_str());
    }
    esp_err_t err = esp_http_client_perform(client);
    *status_out = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t refresh_token()
{
    s_token.clear();
    if (s_login.empty() || s_password.empty()) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::string body = "username=" + url_encode(s_login) + "&password=" + url_encode(s_password);
    std::string response;
    int status = 0;
    esp_err_t err = http_request(TOKEN_URL, body.c_str(), "", &response, &status);
    if (err != ESP_OK) {
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "Token request rejected (HTTP %d)", status);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    const std::string token = root != nullptr ? json_string(root, "access_token") : "";
    int lifetime = DEFAULT_TOKEN_LIFETIME_S;
    if (root != nullptr) {
        const cJSON *expires = cJSON_GetObjectItemCaseSensitive(root, "expires_in");
        if (cJSON_IsNumber(expires) && expires->valueint > 0) {
            lifetime = expires->valueint;
        } else if (cJSON_IsString(expires) && std::atoi(expires->valuestring) > 0) {
            lifetime = std::atoi(expires->valuestring);
        }
    }
    cJSON_Delete(root);
    if (token.empty()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_token = token;
    s_token_expiry = time(nullptr) + std::max(lifetime - TOKEN_EXPIRY_MARGIN_S, 0);
    return ESP_OK;
}

bool token_valid()
{
    return !s_token.empty() && time(nullptr) < s_token_expiry;
}

esp_err_t get_json(const std::string &url, std::string *response)
{
    if (!token_valid() && !s_login.empty()) {
        esp_err_t token_err = refresh_token();
        if (token_err != ESP_OK) {
            ESP_LOGW(TAG, "Token unavailable (%s), trying unauthenticated", esp_err_to_name(token_err));
        }
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        response->clear();
        int status = 0;
        esp_err_t err = http_request(url, nullptr, s_token, response, &status);
        if (err != ESP_OK) {
            return err;
        }
        if (status == 401 && attempt == 0 && !s_login.empty() && refresh_token() == ESP_OK) {
            continue;
        }
        return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
    }
    return ESP_FAIL;
}

}  // namespace

void set_credentials(const char *login, const char *password)
{
    s_login = login != nullptr ? login : "";
    s_password = password != nullptr ? password : "";
    s_token.clear();
    s_token_expiry = 0;
}

esp_err_t fetch_meetings(int year, std::vector<f1::Meeting> *meetings)
{
    if (meetings == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[96] = {};
    std::snprintf(url, sizeof(url), "%s/meetings?year=%d", BASE_URL, year);
    std::string response;
    esp_err_t err = get_json(url, &response);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::vector<f1::Meeting> parsed;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        f1::Meeting meeting;
        meeting.key = json_uint(item, "meeting_key");
        meeting.name = json_string(item, "meeting_name");
        meeting.location = json_string(item, "location");
        meeting.cancelled = json_bool(item, "is_cancelled");
        meeting.start_epoch = parse_utc_time(json_string(item, "date_start"));
        meeting.end_epoch = parse_utc_time(json_string(item, "date_end"));
        if (meeting.key != 0 && meeting.name.find("Grand Prix") != std::string::npos) {
            parsed.push_back(std::move(meeting));
        }
    }
    cJSON_Delete(root);

    std::sort(parsed.begin(), parsed.end(), [](const f1::Meeting &a, const f1::Meeting &b) {
        return a.start_epoch < b.start_epoch;
    });
    *meetings = std::move(parsed);
    return meetings->empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t fetch_sessions(uint32_t meeting_key, std::vector<f1::Session> *sessions)
{
    if (sessions == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[112] = {};
    std::snprintf(url, sizeof(url), "%s/sessions?meeting_key=%u", BASE_URL, static_cast<unsigned>(meeting_key));
    std::string response;
    esp_err_t err = get_json(url, &response);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::vector<f1::Session> parsed;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        f1::Session session;
        session.key = json_uint(item, "session_key");
        session.meeting_key = json_uint(item, "meeting_key");
        session.name = json_string(item, "session_name");
        session.cancelled = json_bool(item, "is_cancelled");
        session.start_epoch = parse_utc_time(json_string(item, "date_start"));
        if (session.key != 0 && session.meeting_key == meeting_key) {
            parsed.push_back(std::move(session));
        }
    }
    cJSON_Delete(root);

    std::sort(parsed.begin(), parsed.end(), [](const f1::Session &a, const f1::Session &b) {
        return a.start_epoch < b.start_epoch;
    });
    *sessions = std::move(parsed);
    return sessions->empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

}  // namespace openf1
