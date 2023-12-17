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

std::string Sam3Client::SSOLogin()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  std::string url = "https://ssom.magentatv.de/login";
  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpGet(url, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206) || (!doc.HasMember("loginRedirectUrl")))
  {
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] SSO Login failed");
    return "";
  }

  return Utils::JsonStringOrEmpty(doc, "loginRedirectUrl");
}

bool Sam3Client::SSOAuthenticate(const std::string& code, const std::string& state)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = "https://ssom.magentatv.de/authenticate";

  //{
  //    "checkRefreshToken": true,
  //    "returnCode": {
  //        "code": "129593504",
  //        "state": "129338912"
  //    }
  //}

  std::string body = "{\"checkRefreshToken\": true, "
                       "\"returnCode\": {"
                       "\"code\": \"" + code + "\", "
                       "\"state\": \"" + state + "\"}}";

  //kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Body %s", body.c_str());

  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpPost(url, body, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206) || (!doc.HasMember("userInfo")))
  {
    kodi::Log(ADDON_LOG_DEBUG, "[Sam3] SSO Authenticate failed");
    return "";
  }

  if (!doc.HasMember("userInfo"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to authenticate");
    return false;
  }

  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Result %s", result.c_str());

  const rapidjson::Value& userInfo = doc["userInfo"];
  std::string m_userId = Utils::JsonStringOrEmpty(userInfo, "userId");
  std::string m_accountId = Utils::JsonStringOrEmpty(userInfo, "accountId");
  std::string m_displayName = Utils::JsonStringOrEmpty(userInfo, "displayName");
  m_personaToken = Utils::JsonStringOrEmpty(userInfo, "personaToken");
  kodi::Log(ADDON_LOG_DEBUG, "UserID: %s, AccountID: %s", m_userId.c_str(), m_accountId.c_str());
  if (!m_personaToken.empty())
  {
    kodi::Log(ADDON_LOG_DEBUG, "New personaToken: %s", m_personaToken.c_str());
    m_settings->SetSetting("personaltoken", m_personaToken);
  }

  return true;
}

Sam3Client::Sam3Client(CSettings* setting, HttpClient* httpclient):
  m_settings(setting),
  m_httpClient(httpclient)
{
  m_personaToken = m_settings->GetMagenta2PersonalToken();
}

Sam3Client::~Sam3Client()
{

}

void Sam3Client::SetSam3ClientId(const std::string& id)
{
  m_sam3ClientId = id;
  m_sam3ClientId = "10LIVESAM30000004901FTVWEBCLIENTACC00000";
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Client ID: %s", m_sam3ClientId.c_str());
}

bool Sam3Client::InitSam3(const std::string& url)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpGetCached(url, 60 * 60 * 24 * 30, statusCode);

  rapidjson::Document doc;
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    return false;
  }
  m_authorization_endpoint = Utils::JsonStringOrEmpty(doc, "authorization_endpoint");
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Authorization endpoint: %s", m_authorization_endpoint.c_str());
  m_issuer =  Utils::JsonStringOrEmpty(doc, "issuer");
  kodi::Log(ADDON_LOG_DEBUG, "[Sam3] Issuer: %s", m_issuer.c_str());

  return true;
}


bool Sam3Client::Sam3Login()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  int statusCode = 0;
  std::string result;
//  std::string url = m_authorization_endpoint +
//                    "?client_id=" + m_sam3ClientId +
//                    "&redirect_uri=https%3A%2F%2Fweb2.magentatv.de%2Fauthn%2Fidm" +
//                    "&response_type=code" +
//                    "&scope=openid+offline_access";

  std::string url = SSOLogin();

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

  return SSOAuthenticate(code, state);
}

// https://accounts.login.idm.telekom.com/oauth2/auth?state=1340809026
//&claims=%7B%22id_token%22%3A%7B%22urn%3Atelekom.com%3Aall%22%3A%7B%22essential%22%3Afalse%7D%7D%7D
//&client_id=10LIVESAM30000004901FTVWEBCLIENTACC00000
//&code_challenge=1299732986
//&nonce=64e42751031c29a8b5b2a3031508e6c9
//&prompt=x-no-sso
//&redirect_uri=https%3A%2F%2Fweb2.magentatv.de%2Fauthn%2Fidm
//&response_type=code
//&scope=openid+offline_access
//&x-customizing-xrds-id=mtv

//https://accounts.login.idm.telekom.com/oauth2/auth?
//client_id=10LIVESAM30000004901NGTVMAGENTA000000000
//&redirect_uri=https%3A%2F%2Fweb.magentatv.de%2Fauthn%2Fidm
//&response_type=code
//&scope=openid+offline_access


bool Sam3Client::LineAuth()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
/*
  if (m_sts.authorizeTokensUrl.empty() || m_sts.deviceToken.empty())
    return false;

  std::string url = m_sts.authorizeTokensUrl;
  std::string body = "grant_type=" + Utils::UrlEncode("urn:com:telekom:ott-app-services:access-auth") +
                     "&client_id=" + m_sam3ClientId +
                     "&token=" + m_sts.deviceToken +
                     "&scope=tvhubs offline_acces";
  rapidjson::Document doc;
  if (!GetPostJson(url, body, doc)) {
    return false;
  }

*/
  return true;
}
