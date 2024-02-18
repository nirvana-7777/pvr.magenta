/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "SsoClient.h"
#include "../Settings.h"
#include "../Utils.h"
#include "rapidjson/document.h"

SsoClient::SsoClient(CSettings* setting, HttpClient* httpclient):
  m_settings(setting),
  m_httpClient(httpclient)
{
//  m_personaToken = m_settings->GetMagenta2PersonaToken();
//  m_deviceId = m_settings->GetMagentaDeviceID();
}


SsoClient::~SsoClient()
{

}

std::string SsoClient::SSOLogin()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  std::string url = SSO_URL + "login";
  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpGet(url, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206) || (!doc.HasMember("loginRedirectUrl")))
  {
    if (!doc.GetParseError())
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] SSO Login returned: %s", result.c_str());
   else
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] SSO Login failed");
    return "";
  }

  return Utils::JsonStringOrEmpty(doc, "loginRedirectUrl");
}

bool SsoClient::SSOAuthenticate(const std::string& code, const std::string& state, std::string& personaToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = SSO_URL + "authenticate";

  //{
  //    "checkRefreshToken": true,
  //    "returnCode": {
  //        "code": "129593504",
  //        "state": "129338912"
  //    }
  //}
  std::string body;
  if (code.empty() && state.empty())
  {
    body = "{\"checkRefreshToken\": true }";
  }
  else
  {
    body = "{\"checkRefreshToken\": true, "
                   "\"returnCode\": {"
                   "\"code\": \"" + code + "\", "
                   "\"state\": \"" + state + "\"}}";
  }
//  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] URL: %s, Body %s", url.c_str(), body.c_str());

  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpPost(url, body, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206) || (!doc.HasMember("userInfo")))
  {
    if (!doc.GetParseError())
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] SSO Authenticate returned: %s", result.c_str());
   else
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] SSO Authenticate failed");
    return false;
  }

  if (!doc.HasMember("userInfo"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to authenticate");
    return false;
  }

//  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Result %s", result.c_str());

  const rapidjson::Value& userInfo = doc["userInfo"];
  std::string m_userId = Utils::JsonStringOrEmpty(userInfo, "userId");
  std::string m_accountId = Utils::JsonStringOrEmpty(userInfo, "accountId");
  std::string m_displayName = Utils::JsonStringOrEmpty(userInfo, "displayName");
  personaToken = Utils::JsonStringOrEmpty(userInfo, "personaToken");
//  kodi::Log(ADDON_LOG_DEBUG, "UserID: %s, AccountID: %s", m_userId.c_str(), m_accountId.c_str());
//  if (!m_personaToken.empty())
//  {
//    kodi::Log(ADDON_LOG_DEBUG, "New personaToken: %s", m_personaToken.c_str());
//    m_settings->SetSetting("personaltoken", m_personaToken);
//  }

  return true;
}
