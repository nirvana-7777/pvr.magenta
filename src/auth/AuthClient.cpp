/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "AuthClient.h"
#include "../Settings.h"
#include "../Utils.h"
#include "rapidjson/document.h"
#include "JWT.h"
#include "../Base64.h"
#include "../taa/TaaClient.h"
#include "../sam3/Sam3Client.h"
#include "../sso/SsoClient.h"

void AuthClient::SetSam3Url(const std::string& url) {
  m_sam3Client->SetSam3Url(url);
}

void AuthClient::SetAuthorizeTokenUrl(const std::string& url) {
  m_sam3Client->SetAuthorizeTokenUrl(url);
}

void AuthClient::SetLineAuthUrl(const std::string& url) {
  m_sam3Client->SetLineAuthUrl(url);
}

void AuthClient::SetSam3ClientId(const std::string& id) {
  m_sam3Client->SetClientId(id);
}

void AuthClient::SetDeviceToken(const std::string& token) {
  m_sam3Client->SetDeviceToken(token);
}

bool AuthClient::InitSam3() {
  return m_sam3Client->InitSam3();
}

void AuthClient::SetTaaUrl(const std::string& url) {
  m_taaClient->SetTaaUrl(url);
}

void AuthClient::SetAccountUri(const std::string& accountUri) {
  m_accountUri = accountUri;
  kodi::Log(ADDON_LOG_DEBUG, "[Auth] AccountURI set to: %s", m_accountUri.c_str());
}


AuthClient::AuthClient(CSettings* setting, HttpClient* httpclient):
  m_settings(setting),
  m_httpClient(httpclient)
{
  m_ssoClient = new SsoClient(m_settings, m_httpClient);
  m_sam3Client = new Sam3Client(m_settings, m_httpClient, m_ssoClient);

  m_taaClient = new TaaClient(m_settings, m_httpClient, m_sam3Client);


  m_personaToken = m_settings->GetMagenta2PersonaToken();
//  m_deviceId = m_settings->GetMagentaDeviceID();
}


AuthClient::~AuthClient()
{

}

void AuthClient::ComposePersonaToken(const std::string& dcCtsPersonaToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  std::string rawToken = m_accountUri + ":" + dcCtsPersonaToken;
  m_personaToken = base64_encode(rawToken.c_str(), rawToken.length());
  kodi::Log(ADDON_LOG_DEBUG, "[Auth] reported new personaToken: %s", m_personaToken.c_str());
  m_settings->SetSetting("personaltoken", m_personaToken);
}

bool AuthClient::IsJWTexpired(const std::string& token)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string header;
  std::string payload;
  std::string signature;

//  kodi::Log(ADDON_LOG_DEBUG, "[Taa] Token to verify: %s", token.c_str());

  if (!ParseToken(token, header, payload, signature))
  {
    kodi::Log(ADDON_LOG_ERROR, "[Auth] Token Parse error");
    return true;
  }

  std::string decPayload = base64_decode(payload);

//  kodi::Log(ADDON_LOG_DEBUG, "[Taa] payload: %s decoded %s", payload.c_str(), decPayload.c_str());

  rapidjson::Document doc;
  doc.Parse(decPayload.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "[Auth] JWTexpired JSON parse error for %s", decPayload.c_str());
    return true;
  }

  time_t tokenExp = Utils::JsonIntOrZero(doc, "exp");

  kodi::Log(ADDON_LOG_DEBUG, "[Auth] Test if expired: %u", tokenExp);

  bool isExpired = (time(NULL) > tokenExp);
  if (isExpired)
    kodi::Log(ADDON_LOG_DEBUG, "[Auth] JWT is expired!");

  return isExpired;
}

bool AuthClient::IsPersonaTokenExpired(const std::string& personaToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string strToken = base64_decode(personaToken);
//  kodi::Log(ADDON_LOG_DEBUG, "[Taa] decoded persona token: %s", strToken.c_str());
  std::vector<std::string> persona_arr = kodi::tools::StringUtils::Split(strToken, ":", 3);
  if (persona_arr.size() != 3)
    return true;

//  kodi::Log(ADDON_LOG_DEBUG, "[Taa] decoded persona token part 2: %s", persona_arr.at(2).c_str());

  return IsJWTexpired(persona_arr.at(2));
}

bool AuthClient::GetPersonaToken(std::string& personaToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_personaToken.empty() || IsPersonaTokenExpired(m_personaToken))
  {
    std::string dcCtsPersonaToken;
    if (!m_taaClient->UpdateTaa(dcCtsPersonaToken))
    {
      kodi::Log(ADDON_LOG_DEBUG, "[Auth] TAAUpdate failed!");
      return false;
    }
    ComposePersonaToken(dcCtsPersonaToken);
  }

  personaToken = m_personaToken;

//  personaToken = m_sam3Client->GetPersonaToken();

  return true;
}

bool AuthClient::ReLogin()
{

  return true;
}
