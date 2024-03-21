/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "../Globals.h"
#include "TaaClient.h"
#include "../Settings.h"
#include "../Utils.h"
#include "rapidjson/document.h"
#include "../Base64.h"
#include "../auth/JWT.h"

void TaaClient::SetTaaUrl(const std::string& url) {
  m_taaUrl = url;
  kodi::Log(ADDON_LOG_DEBUG, "[Taa] TaaUrl set to: %s", m_taaUrl.c_str());
}
/*
void TaaClient::SetAccountUri(const std::string& accountUri) {
  m_accountUri = accountUri;
  kodi::Log(ADDON_LOG_DEBUG, "[Taa] AccountURI set to: %s", m_accountUri.c_str());
}
*/
TaaClient::TaaClient(CSettings* setting, HttpClient* httpclient, Sam3Client* sam3client):
  m_settings(setting),
  m_httpClient(httpclient),
  m_sam3Client(sam3client)
{
  m_dcCtsPersonaToken.clear();
//  m_personaToken = m_settings->GetMagenta2PersonaToken();
  m_deviceId = m_settings->GetMagentaDeviceID();
  m_platform = m_settings->GetTerminalType();
}


TaaClient::~TaaClient()
{

}

bool TaaClient::UpdateTaa(std::string& dcCtsPersonaToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_taaUrl.empty())
    return false;

  std::string taaAccessToken;
  if (!m_sam3Client->GetAccessToken("taa", taaAccessToken))
    return false;

  std::string url = m_taaUrl;
  std::string body = "{\"keyValue\": \"" + IDM + "/" + APPVERSION2 + "/" + "TokenChannelParams(id=Tv)/TokenDeviceParams(id=" + m_deviceId +
                     ", model=" + Magenta2Parameters[m_platform].device_name + ", os=" + Magenta2Parameters[m_platform].firmware + ")/DE/telekom\","
                     "\"accessToken\": \"" + taaAccessToken + "\"," +
                     "\"accessTokenSource\": \"" + IDM + "\"," +
                     "\"appVersion\": \"" + APPVERSION2 + "\"," +
                     "\"channel\": {" +
                     "\"id\": \"Tv\"}," +
                     "\"device\": {" +
                     "\"id\": \"" + m_deviceId + "\"," +
                     "\"model\": \"" + Magenta2Parameters[m_platform].device_name + "\"," +
                     "\"os\": \"" + Magenta2Parameters[m_platform].firmware + "\" }," +
                     "\"natco\": \"DE\"," +
                     "\"type\": \"telekom\"}";

//  kodi::Log(ADDON_LOG_DEBUG, "TAA Body: %s", body.c_str());

  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpPost(url, body, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    if (!doc.GetParseError())
      kodi::Log(ADDON_LOG_DEBUG, "[Taa] Login returned: %s", result.c_str());
    else
      kodi::Log(ADDON_LOG_DEBUG, "[Taa] Failed login");
    return false;
  }

//  kodi::Log(ADDON_LOG_DEBUG, "TAA result %s", result.c_str());
  bool deviceLimitExceeded = Utils::JsonBoolOrFalse(doc, "deviceLimitExceeded");

  if (deviceLimitExceeded)
  {
    kodi::Log(ADDON_LOG_ERROR, "[Taa] Device Limit exceeded");
    return false;
  }

  m_TaaAccessToken = Utils::JsonStringOrEmpty(doc, "accessToken");
  std::string header;
  std::string payload;
  std::string signature;
  if (ParseToken(m_TaaAccessToken, header, payload, signature))
  {
    kodi::Log(ADDON_LOG_DEBUG, "[Taa] header: %s, Payload %s, Signature: %s", header.c_str(), payload.c_str(), signature.c_str());
    std::string decPayload = base64_decode(payload);
//    kodi::Log(ADDON_LOG_DEBUG, "[Taa] decoded access token payload %s", decPayload.c_str());
    if (!ParseJWT(decPayload))
      return false;
    dcCtsPersonaToken = m_dcCtsPersonaToken;
  }
  m_TaaRefreshToken = Utils::JsonStringOrEmpty(doc, "refreshToken");
  if (ParseToken(m_TaaRefreshToken, header, payload, signature))
  {
    std::string decPayload = base64_decode(payload);
//    kodi::Log(ADDON_LOG_DEBUG, "[Taa] decoded refresh token payload %s", decPayload.c_str());

  }
  return true;
}

bool TaaClient::ParseJWT(const std::string& jwt)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  rapidjson::Document doc;
  doc.Parse(jwt.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_DEBUG, "[Taa] JWT Parse error");
    return false;
  }
  m_tokenIat = Utils::JsonIntOrZero(doc, "iat");
  m_tokenExp = Utils::JsonIntOrZero(doc, "exp");

  m_dcCtsPersonaToken = Utils::JsonStringOrEmpty(doc, "dc_cts_personaToken");
  m_accountToken = Utils::JsonStringOrEmpty(doc, "dc_cts_accountToken");

  m_personaId = Utils::JsonStringOrEmpty(doc, "dc_cts_personaId");
  m_consumerId = Utils::JsonStringOrEmpty(doc, "dc_cts_consumerId");
  m_accountId = Utils::JsonStringOrEmpty(doc, "dc_cts_accountId");
  m_tvAccountId = Utils::JsonStringOrEmpty(doc, "dc_tvAccountId");

  kodi::Log(ADDON_LOG_DEBUG, "[Taa] expire: %u", m_tokenExp);
//  m_settings->SetIntSetting("personaexpiry", m_tokenExp);

  return true;
}
/*
bool TaaClient::IsTokenValid()
{
  return (time(NULL) < m_tokenExp)? true:false;
}
*/
