#include "../Globals.h"
#include "HttpClient.h"
#include "Cache.h"
#include <random>
#include "../md5.h"
#include "../Utils.h"
#include <kodi/AddonBase.h>
#include "../Settings.h"
#include "../auth/AuthClient.h"
/*
static const std::string MAGENTA_USER_AGENT = std::string("Kodi/")
    + std::string(STR(KODI_VERSION)) + std::string(" pvr.magenta/")
    + std::string(STR(MAGENTA_VERSION));
*/
static const std::string SSO_USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

HttpClient::HttpClient(CSettings* settings):
  m_settings(settings)
{
  m_sessionId = "";
  m_platform = m_settings->GetTerminalType();
}

HttpClient::~HttpClient()
{

}

void HttpClient::SetSessionId(const std::string& id) {
  m_sessionId = id;
}

void HttpClient::SetDeviceToken(const std::string& token) {
  m_deviceToken = token;
  kodi::Log(ADDON_LOG_DEBUG, "Device Token set to: %s", token.c_str());
}

void HttpClient::ClearSession() {
  m_uuid = GetUUID();
}

std::string HttpClient::GetUUID()
{
  if (!m_uuid.empty())
  {
    return m_uuid;
  }

  m_uuid = GenerateUUID();
  return m_uuid;
}

std::string HttpClient::GenerateUUID()
{
    std::string tmp_s;
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "-";

    srand( (unsigned) time(NULL));

    for (int i = 0; i < 21; ++i)
    {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return tmp_s;
}

std::string HttpClient::HttpGetCached(const std::string& url, time_t cacheDuration, int &statusCode)
{

  std::string content;
  std::string cacheKey = md5(url);
  statusCode = 200;
  if (!Cache::Read(cacheKey, content))
  {
    content = HttpGet(url, statusCode);
    if (!content.empty())
    {
      time_t validUntil;
      time(&validUntil);
      validUntil += cacheDuration;
      Cache::Write(cacheKey, content, validUntil);
    }
  }
  return content;
}

std::string HttpClient::HttpGet(const std::string& url, int &statusCode)
{
  return HttpRequest("GET", url, "", statusCode);
}

std::string HttpClient::HttpDelete(const std::string& url, int &statusCode)
{
  return HttpRequest("DELETE", url, "", statusCode);
}

std::string HttpClient::HttpPost(const std::string& url, const std::string& postData, int &statusCode)
{
  return HttpRequest("POST", url, postData, statusCode);
}

std::string HttpClient::HttpRequest(const std::string& action, const std::string& url, const std::string& postData, int &statusCode)
{
  Curl curl;

  if (url.find("ssom") != std::string::npos)
    curl.AddHeader("User-Agent", SSO_USER_AGENT);
  else
    curl.AddHeader("User-Agent", Magenta2Parameters[m_platform].user_agent);
  if ((url.find("oauth2") != std::string::npos) || (url.find("factorx") != std::string::npos) || (url.find("/caas/atvlauncher/v1/token") != std::string::npos)) {
    curl.AddHeader("Content-Type", "application/x-www-form-urlencoded");
  } else {
    curl.AddHeader("Content-Type", "application/json");
  }

  if (m_sessionId.empty())
  {
    //MagentaTV 1
    std::string csrftoken = m_settings->GetMagentaCSRFToken();
    if (!csrftoken.empty()) {
      curl.AddHeader("X_CSRFToken", csrftoken);
    }
  } else
  {
    //MagentaTV 2.0
    curl.AddHeader("Accept-Ranges", "none");
    if (((url.find("license") != std::string::npos) ||
         (url.find("link") != std::string::npos) ||
         (url.find("npvr-audience") != std::string::npos)) &&
         (m_authClient != nullptr)) {
//      curl.AddHeader("Authorization", "Basic " + m_settings->GetMagenta2PersonaToken());
      std::string personaToken;
      if (m_authClient->GetPersonaToken(personaToken))
        curl.AddHeader("Authorization", "Basic " + personaToken);
      else
        kodi::Log(ADDON_LOG_ERROR,"Couldn't fetch persona token!");
    } else if (url.find("ssom") != std::string::npos) {
      curl.AddHeader("origin", "https://web2.magentatv.de");
      curl.AddHeader("referer", "https://web2.magentatv.de/");
      curl.AddHeader("session-id", m_sessionId);
      curl.AddHeader("device-id", m_settings->GetMagentaDeviceID());
    } else if (url.find("prod.dcm.telekom-dienste.de") != std::string::npos) {
      curl.AddHeader("x-dt-session-id", m_sessionId);
      curl.AddHeader("x-dt-call-id", Utils::CreateUUID());
    } else if (url.find("cvss/IPTV2015%40ACC/vodclient") != std::string::npos) {
      curl.AddHeader("x-device-authorization", "TAuth realm=\"device\",device_token=\"" + m_deviceToken + "\"");
    } else if (url.find("oauth2/auth?") != std::string::npos) {
      curl.AddHeader("referer", "https://web2.magentatv.de/");
    }
    if (url.find("npvr-audience") != std::string::npos) {
      curl.AddHeader("accept", "application/json;v=2");
    }
    if (url.find("yo-digital.com") != std::string::npos) {
      curl.AddHeader("requestid", Utils::CreateUUID());
    }
    if (url.find("wcps.t-online.de") != std::string::npos && (action == "GET")) {
      curl.AddHeader("x-stbserialnumber", m_settings->GetMagentaDeviceID());
      curl.AddHeader("dt-session-id", m_sessionId);
      curl.AddHeader("dt-call-id", Utils::CreateUUID());
    }
  }

  std::string content = HttpRequestToCurl(curl, action, url, postData, statusCode);

  if (statusCode >= 400 || statusCode < 200) {
    kodi::Log(ADDON_LOG_ERROR, "Open URL failed with %i.", statusCode);
    if (m_statusCodeHandler != nullptr) {
      m_statusCodeHandler->ErrorStatusCode(statusCode);
    }
    return "";
  }
  return content;
}

std::string HttpClient::HttpRequestToCurl(Curl &curl, const std::string& action,
    const std::string& url, const std::string& postData, int &statusCode)
{
  kodi::Log(ADDON_LOG_DEBUG, "Http-Request: %s %s.", action.c_str(), url.c_str());
  std::string content;
  if (action == "POST")
  {
    content = curl.Post(url, postData, statusCode);
  }
  else if (action == "DELETE")
  {
    content = curl.Delete(url, statusCode);
  }
  else
  {
    content = curl.Get(url, statusCode);
  }
  m_effectiveUrl = curl.GetEffectiveUrl();
  return content;

}
