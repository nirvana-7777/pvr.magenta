/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Sam3Client.h"
#include "../Settings.h"
#include "../Utils.h"
#include "rapidjson/document.h"
#include <kodi/gui/dialogs/OK.h>
#include "../sso/SsoClient.h"
//#include <tinyxml2.h>
//#include "../tixml2ex.h"

std::string GetAttribute(const std::string& line, const std::string& str)
{

  int attrib_start = line.find(str);
  if (attrib_start == std::string::npos)
    return "";
  attrib_start+= str.size() + 2;
  int attrib_end = line.find("\"", attrib_start);
  if (attrib_end == std::string::npos)
    return "";
  else
    return line.substr(attrib_start, attrib_end - attrib_start);
}

void Sam3Client::SetSam3Url(const std::string& url) {
  m_sam3Url = url;
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Url set to: %s", m_sam3Url.c_str());
}

void Sam3Client::SetClientId(const std::string& id) {
  m_sam3ClientId = id;
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Client Id set to: %s", m_sam3ClientId.c_str());
}

void Sam3Client::SetLineAuthUrl(const std::string& url) {
  m_lineAuthUrl = url;
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] LineAuthUrl set to: %s", m_lineAuthUrl.c_str());
}

void Sam3Client::SetAuthorizeTokenUrl(const std::string& url) {
  m_authorizeTokensUrl = url;
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] AuthorizeTokensUrl set to: %s", m_authorizeTokensUrl.c_str());
}

void Sam3Client::SetDeviceToken(const std::string& token) {
  m_deviceToken = token;
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Device Token set to: %s", m_deviceToken.c_str());
}

void Sam3Client::ParseHtml(const std::string& result)
{
  std::string search = "<input type=\"hidden\" name=\"";
  int startpos = result.find("form id=\"login\"");
  if (startpos == std::string::npos)
    return;
  m_attributes.clear();
  while (result.find(search, startpos) != std::string::npos)
  {
    int startline = result.find(search, startpos);
    int endline = result.find("\">", startline);
    std::string line = result.substr(startline, endline - startline + 2);
//    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] hidden attributes %s", line.c_str());
    Sam3KV sam3Attribute;
    sam3Attribute.name = GetAttribute(line, "name");
    sam3Attribute.value = GetAttribute(line, "value");
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Added Attribute Name %s Value %s", sam3Attribute.name.c_str(), sam3Attribute.value.c_str());
    m_attributes.emplace_back(sam3Attribute);
    startpos = endline;
  }
}

bool Sam3Client::GetAuthMethods()
{
  if (m_lineAuthUrl.empty())
    return false;

  std::string url = m_lineAuthUrl;

  int statusCode = 0;
  std::string result;

  rapidjson::Document doc;
  result = m_httpClient->HttpGet(url, statusCode);

  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] GetAuthMethods failed");
    return false;
  }

  if (!doc.HasMember("content"))
  {
    return false;
  }
  const rapidjson::Value& content = doc["content"];
  if (content.HasMember("supportedAuthenticationKinds"))
  {
    const rapidjson::Value& authkinds = content["supportedAuthenticationKinds"];
    for (rapidjson::SizeType i = 0; i < authkinds.Size(); i++)
    {
      std::string method = authkinds[i].GetString();
      kodi::Log(ADDON_LOG_DEBUG, "Method: %s", method.c_str());
      if (method == GRANTPASSWORD)
        m_authMethods.password = true;
      else if (method == GRANTAUTHCODE)
        m_authMethods.code = true;
      else if (method == GRANTLINEAUTH)
        m_authMethods.line = true;
    }
  }

  return true;
}

Sam3Client::Sam3Client(CSettings* setting, HttpClient* httpclient, SsoClient* ssoclient):
  m_settings(setting),
  m_httpClient(httpclient),
  m_ssoClient(ssoclient)
{
  m_authMethods.password = false;
  m_authMethods.code = false;
  m_authMethods.line = false;
//  m_personaToken = m_settings->GetMagenta2PersonaToken();
  m_refreshToken = m_settings->GetMagentaRefreshToken();
}

bool Sam3Client::ReAuthenticate(std::string& personaToken)
{
  return m_ssoClient->SSOAuthenticate("", "", personaToken);
}

Sam3Client::~Sam3Client()
{

}

bool Sam3Client::InitSam3()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  GetAuthMethods();
  //TODO: Remove call to LineAuth
//  if (m_authMethods.line)
//  {
//    LineAuth();
//  }

  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpGetCached(m_sam3Url, 60 * 60 * 24 * 30, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    return false;
  }
  m_authorization_endpoint = Utils::JsonStringOrEmpty(doc, "authorization_endpoint");
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Authorization endpoint: %s", m_authorization_endpoint.c_str());
  m_token_endpoint =  Utils::JsonStringOrEmpty(doc, "token_endpoint");
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Token endpoint: %s", m_token_endpoint.c_str());
  m_userinfo_endpoint =  Utils::JsonStringOrEmpty(doc, "userinfo_endpoint");
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] UserInfo endpoint: %s", m_userinfo_endpoint.c_str());
  m_issuer =  Utils::JsonStringOrEmpty(doc, "issuer");
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Issuer: %s", m_issuer.c_str());
  m_bcAuthStart = Utils::JsonStringOrEmpty(doc, "backchannel_auth_start");
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Back Channel Auth Start: %s", m_bcAuthStart.c_str());

  //TODO: Remove call to LineAuth
//  if (m_authMethods.code)
//  {
//    BackChannelAuth();
//  }
//  RefreshToken("openid");
  return true;
}


bool Sam3Client::Sam3Login(std::string& personaToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  int statusCode = 0;
  std::string result;
//  std::string url = m_authorization_endpoint +
//                    "?client_id=" + m_sam3ClientId +
//                    "&redirect_uri=https%3A%2F%2Fweb2.magentatv.de%2Fauthn%2Fidm" +
//                    "&response_type=code" +
//                    "&scope=openid+offline_access";

  if (m_authMethods.line)
  {
    LineAuth();
  }

  std::string url = m_ssoClient->SSOLogin();

  result = m_httpClient->HttpGet(url, statusCode);
//  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] %s", result.c_str());
  //tinyxml2::XMLDocument htmlDoc;

//  htmlDoc.Parse(result.c_str());
  if ((statusCode != 200) && (statusCode != 206))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get HTML %s status code: %i", url.c_str(), statusCode);
    return false;
  }
/*
  auto doc = tinyxml2::load_document (result.c_str());

  if (auto div = tinyxml2::find_element(*doc, "/body/div[@class='container-fixed']/div[@class='tbs-container']/div[@class='login-box']/div[@class='offset-bottom-1']"))
  {
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Login found");
  } else {
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Login not found");
  }
*/
/*
  tinyxml2::XMLElement* pRootElement = htmlDoc.RootElement();
  tinyxml2::XMLElement* pElement = pRootElement->FirstChildElement("body");
  if (!pElement) {
    kodi::Log(ADDON_LOG_ERROR, "No Body found");
    return false;
  }
  tinyxml2::XMLElement* pDiv = pRootElement->FirstChildElement("div");
  if (!pDiv) {
    kodi::Log(ADDON_LOG_ERROR, "No div found");
    return false;
  }
*/
  ParseHtml(result);

  url = m_issuer + "/factorx";
  std::string postData = "";

  for (auto& attribute : m_attributes)
  {
    postData += attribute.name + "=" + attribute.value + "&";
  }
  postData += "pw_usr=" + Utils::UrlEncode(m_settings->GetMagentaUsername()) + "&pw_submit=&hidden_pwd=";

  result = m_httpClient->HttpPost(url, postData, statusCode);

//  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] PostData %s", postData.c_str());
  if ((statusCode != 200) && (statusCode != 206))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to post factorx %s body: %s status code: %i", url.c_str(), postData.c_str(), statusCode);
    return false;
  }

  ParseHtml(result);
  postData = "";

  for (auto& attribute : m_attributes)
  {
    postData += attribute.name + "=" + attribute.value + "&";
  }
  postData += "hidden_usr=" + Utils::UrlEncode(m_settings->GetMagentaUsername()) +
              "&pw_pwd=" + Utils::UrlEncode(m_settings->GetMagentaPassword()) +
              "&pw_submit=";

  result = m_httpClient->HttpPost(url, postData, statusCode);
  if ((statusCode != 200) && (statusCode != 206))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to post factorx %s body: %s status code: %i", url.c_str(), postData.c_str(), statusCode);
    return false;
  }
  std::string effectiveUrl = m_httpClient->GetEffectiveUrl();
  int codePos = effectiveUrl.find("code");
  if (codePos != std::string::npos)
    codePos += 5;
  else
    return false;
  int statePos = effectiveUrl.find("state");
  if (statePos != std::string::npos)
    statePos += 6;
  else
    return false;

  std::string code = effectiveUrl.substr(codePos, 8);
  std::string state = effectiveUrl.substr(statePos, 10);
//  kodi::Log(ADDON_LOG_DEBUG, "URL: %s, Code: %s, State: %s", effectiveUrl.c_str(), code.c_str(), state.c_str());
  kodi::Log(ADDON_LOG_DEBUG, "URL: %s, Code: %s, State: %s", effectiveUrl.c_str(), code.c_str(), state.c_str());

  return m_ssoClient->SSOAuthenticate(code, state, personaToken);
}

bool Sam3Client::LineAuth()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_authorizeTokensUrl.empty() || m_deviceToken.empty())
    return false;

  std::string url = m_authorizeTokensUrl;
  std::string body = "grant_type=" + Utils::UrlEncode(GRANTLINEAUTH) +
                     "&client_id=" + m_sam3ClientId +
                     "&token=" + Utils::UrlEncode(m_deviceToken) +
                     "&scope=" + Utils::UrlEncode("tvhubs offline_access");
  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpPost(url, body, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    if (!doc.GetParseError())
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] LineAuth returned: %s", result.c_str());
    else
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] LineAuth failed");
    return false;
  }
  if (doc.HasMember("refresh_token"))
  {
    m_refreshToken = Utils::JsonStringOrEmpty(doc, "refresh_token");
    m_settings->SetSetting("refresh_token", m_refreshToken);
  }

  return true;
}

bool Sam3Client::BackChannelAuth()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_bcAuthStart.empty())
    return false;

  std::string url = m_bcAuthStart;
  std::string body = "&client_id=" + m_sam3ClientId +
                     "&scope=" + Utils::UrlEncode("tvhubs offline_access");

  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpPost(url, body, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    if (!doc.GetParseError())
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] BackChannel Auth start returned: %s", result.c_str());
    else
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Failed to start BackChannel Auth");
    return false;
  }

  std::string otp = Utils::JsonStringOrEmpty(doc, "initial_login_code");
  std::string expires = Utils::JsonStringOrEmpty(doc, "expires_in");
  std::string interval = Utils::JsonStringOrEmpty(doc, "interval");

  std::string text = "\n" + otp + "\n\n" + kodi::addon::GetLocalizedString(30047) + std::to_string((stoi(expires)/60)) + kodi::addon::GetLocalizedString(30048) + "\n";
  kodi::gui::dialogs::OK::ShowAndGetInput(kodi::addon::GetLocalizedString(30046), text);

  RemoteLogin(Utils::JsonStringOrEmpty(doc, "auth_req_id"), Utils::JsonStringOrEmpty(doc, "auth_req_sec"), m_sam3AccessTokens.tvhubs);

  return true;
}

bool Sam3Client::GetToken(const std::string& grantType, const std::string& scope, const std::string& credential1, const std::string& credential2, std::string& accessToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_token_endpoint.empty() || m_refreshToken.empty())
    return false;

  std::string url = m_token_endpoint;
  std::string body = "grant_type=" + Utils::UrlEncode(grantType) +
                     "&client_id=" + m_sam3ClientId;
  if (grantType == GRANTREFRESHTOKEN)
    body = body + "&refresh_token=" + Utils::UrlEncode(credential1) +
                  "&scope=" + Utils::UrlEncode(scope + " offline_access");
  else if (grantType == GRANTREMOTELOGIN)
    body = body + "&auth_req_id=" + Utils::UrlEncode(credential1) +
                  "&auth_req_sec=" + Utils::UrlEncode(credential2);

  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpPost(url, body, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    if (!doc.GetParseError())
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] GetToken returned: %s", result.c_str());
    else
      kodi::Log(ADDON_LOG_DEBUG, "[Sam3] GetToken failed");
    return false;
  }
  if (doc.HasMember("refresh_token"))
  {
    m_refreshToken = Utils::JsonStringOrEmpty(doc, "refresh_token");
    m_settings->SetSetting("refresh_token", m_refreshToken);
  }
  if (doc.HasMember("access_token"))
  {
    accessToken = Utils::JsonStringOrEmpty(doc, "access_token");
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Access Token: %s", accessToken.c_str());
  }
  if (doc.HasMember("id_token"))
  {
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] ID Token: %s", Utils::JsonStringOrEmpty(doc, "id_token").c_str());
  }

  return true;
}

bool Sam3Client::RefreshToken(const std::string& scope, std::string& accessToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  return (GetToken(GRANTREFRESHTOKEN, scope, m_refreshToken, "", accessToken));
}

bool Sam3Client::RemoteLogin(const std::string& auth_req_id, const std::string& auth_req_sec, std::string& accessToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  return (GetToken(GRANTREMOTELOGIN, "", auth_req_id, auth_req_sec, accessToken));
}

bool Sam3Client::GetAccessToken(const std::string& scope, std::string& accessToken)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s] requested scope [%s]", __FUNCTION__, scope);
  //TODO: Not only taa
  if (!m_sam3AccessTokens.taa.empty())
    accessToken = m_sam3AccessTokens.taa;
  else
  {
    if (!RefreshToken(scope, accessToken))
    {
      if (!LineAuth())
        BackChannelAuth();
      return RefreshToken(scope, accessToken);
    }
  }
  return true;
}
