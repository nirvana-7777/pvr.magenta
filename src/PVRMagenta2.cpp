/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PVRMagenta2.h"

#include <algorithm>

#include <kodi/General.h>
#include <kodi/gui/dialogs/OK.h>
#include "Utils.h"
#include "Base64.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <tinyxml2.h>
#include <kodi/Filesystem.h>

void tokenize2(std::string const &str, const char* delim,
            std::vector<std::string> &out)
{
    char *token = strtok(const_cast<char*>(str.c_str()), delim);
    while (token != nullptr)
    {
        out.push_back(std::string(token));
        token = strtok(nullptr, delim);
    }
}

void replace(std::string& str, const std::string& from, const std::string& to) {
  size_t start_pos = str.find(from);
  while (start_pos != std::string::npos)
  {
    str.replace(start_pos, from.length(), to);
    start_pos = str.find(from);
  }
}
/*
bool CPVRMagenta2::IsChannelNumberExist(const unsigned int number)
{
  for (auto& thisChannel : m_channels)
  {
    if (thisChannel.iChannelNumber == number)
    {
      return true;
    }
  }
  return false;
}
*/
bool CPVRMagenta2::XMLGetString(const tinyxml2::XMLNode* pRootNode,
                            const std::string& strTag,
                            std::string& strStringValue)
{
  const tinyxml2::XMLElement* pElement = pRootNode->FirstChildElement(strTag.c_str());
  if (!pElement)
    return false;
  const tinyxml2::XMLNode* pNode = pElement->FirstChild();
  if (pNode)
  {
    strStringValue = pNode->Value();
    return true;
  }
  strStringValue.clear();
  return false;
}

bool CPVRMagenta2::GetMyGenres()
{
  std::string content;
  kodi::vfs::CFile myFile;
  std::string file = kodi::addon::GetAddonPath() + "mygenres2.json";
  kodi::Log(ADDON_LOG_DEBUG, "Opening mygenres: %s", file.c_str());
  if (myFile.OpenFile(file))
  {
    char buffer[1024];
    while (int bytesRead = myFile.Read(buffer, 1024))
      content.append(buffer, bytesRead);
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Failed to open mygenres.json");
    return false;
  }

  rapidjson::Document doc;
  doc.Parse(content.c_str());
  if (doc.GetParseError() || !doc.HasMember("genres"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to GetMyGenres");
    return false;
  }
  const rapidjson::Value& genres = doc["genres"];

  for (rapidjson::SizeType i = 0; i < genres.Size(); i++)
  {
    std::string primaryGenre = Utils::JsonStringOrEmpty(genres[i], "primaryGenre");
    int genreType = Utils::JsonIntOrZero(genres[i], "genreType");
    if (genres[i].HasMember("secondaryGenres"))
    {
      const rapidjson::Value& secondaryGenres = genres[i]["secondaryGenres"];
      for (rapidjson::SizeType j = 0; j < secondaryGenres.Size(); j++)
      {
        std::string secondaryGenre = Utils::JsonStringOrEmpty(secondaryGenres[j], "secondaryGenre");
        int genreSubType = Utils::JsonIntOrZero(secondaryGenres[j], "genreSubType");
        kodi::Log(ADDON_LOG_DEBUG, "Added genre: %s %i %s %i", primaryGenre.c_str(), genreType, secondaryGenre.c_str(), genreSubType);
      }
    }
  }

  return true;
}

bool CPVRMagenta2::GetAuthMethods()
{
  if (m_lineAuthUrl.empty() || m_sts.deviceToken.empty())
    return false;

  m_httpClient->SetDeviceToken(m_sts.deviceToken);
  std::string url = m_lineAuthUrl;
  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
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
      if (method == "password")
        m_authMethods.password = true;
      else if (method == "authorization_code")
        m_authMethods.code = true;
      else if (method == "urn:com:telekom:ott-app-services:access-auth")
        m_authMethods.line = true;
    }
  }

  return true;
}

bool CPVRMagenta2::LineAuth()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

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


  return true;
}

bool CPVRMagenta2::GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc)
{
  int statusCode = 0;
  std::string result;

  if (body.empty()) {
    result = m_httpClient->HttpGet(url, statusCode);
  } else
  {
  //  kodi::Log(ADDON_LOG_DEBUG, "Body: %s", body.c_str());
    result = m_httpClient->HttpPost(url, body, statusCode);
  }
//  kodi::Log(ADDON_LOG_DEBUG, "Result: %s", result.c_str());
  doc.Parse(result.c_str());
  if (statusCode == 206)
  {
//    kodi::Log(ADDON_LOG_DEBUG, "Status Code 206 Response: %s", result.c_str());
  }
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get JSON %s status code: %i", url.c_str(), statusCode);
    return false;
  }
  if (doc.HasMember("isException"))
  {
    if (Utils::JsonIntOrZero(doc, "responseCode") == 401)
    {
      kodi::Log(ADDON_LOG_DEBUG, "We need to reauthenticate!");
      if (!m_authMethods.password && !m_authMethods.code && !m_authMethods.line)
        GetAuthMethods();
      if (m_authMethods.line)
      {
        kodi::Log(ADDON_LOG_DEBUG, "LineAuth");
        LineAuth();
      }
      //SingleSignOn();
    }
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "Get Json for %s answered response code: %i and title %s",
                                          url.c_str(),
                                          Utils::JsonIntOrZero(doc, "responseCode"),
                                          Utils::JsonStringOrEmpty(doc, "title"));
    }
    return false;
  }
  return true;
}

bool CPVRMagenta2::GetSmil(const std::string& url, tinyxml2::XMLDocument& smilDoc)
{
  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpGet(url, statusCode);

  smilDoc.Parse(result.c_str());
  if ((smilDoc.Error()) || (statusCode != 200))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get SMIL %s status code: %i", url.c_str(), statusCode);
    return false;
  }

  return true;
}

bool CPVRMagenta2::AddDistributionRight(const unsigned int number, const std::string& right)
{
  for (auto& thisChannel : m_channels)
  {
    if (thisChannel.iUniqueId == number)
    {
//      thisChannel.isHidden = false;
      thisChannel.distributionRights.emplace_back(right);
      return true;
    }
  }
  kodi::Log(ADDON_LOG_DEBUG, "Failed to add distribution right: %s for channel %i", right.c_str(), number);
  return false;
}

bool CPVRMagenta2::GetChannelByNumber(const unsigned int number, Magenta2Channel& myChannel)
{
  /*
  for (const auto& thisChannel : m_channels)
  {
    if (thisChannel.iUniqueId == (int)channelUid)
    {
      myChannel.bRadio = thisChannel.bRadio;
      myChannel.iUniqueId = thisChannel.iUniqueId;
//      myChannel.mediaId = thisChannel.mediaId;
//      myChannel.bRadio = thisChannel.bRadio;
      myChannel.iChannelNumber = thisChannel.iChannelNumber;
      myChannel.strChannelName = thisChannel.strChannelName;
      myChannel.strIconPath = thisChannel.strIconPath;
//      myChannel.strStreamURL = thisChannel.strStreamURL;
      myChannel.physicalChannels = thisChannel.physicalChannels;
      myChannel.isHidden = thisChannel.isHidden;

      return true;
    }
  }
*/
  return false;
}

bool CPVRMagenta2::SingleSignOn()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = SSO_URL;
  std::string body = "{\"checkRefreshToken\": true}";

  rapidjson::Document doc;
  if (!GetPostJson(url, body, doc)) {
    return false;
  }

  if (!doc.HasMember("userInfo"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to authenticate");
    return false;
  }

  const rapidjson::Value& userInfo = doc["userInfo"];
  m_userId = Utils::JsonStringOrEmpty(userInfo, "userId");
  m_accountId = Utils::JsonStringOrEmpty(userInfo, "accountId");
  m_displayName = Utils::JsonStringOrEmpty(userInfo, "displayName");
  std::string personaToken = Utils::JsonStringOrEmpty(userInfo, "personaToken");
  kodi::Log(ADDON_LOG_DEBUG, "UserID: %s, AccountID: %s", m_userId.c_str(), m_accountId.c_str());
  if (!personaToken.empty())
  {
    kodi::Log(ADDON_LOG_DEBUG, "New personaToken: %s", personaToken.c_str());
    m_settings->SetSetting("personaltoken", personaToken);
  }

  return true;
}

bool CPVRMagenta2::HideDuplicateChannels()
{
  for (auto it = m_channels.begin(); it!=m_channels.end(); ++it)
  {
    for (auto it2 = it+1; it2!=m_channels.end(); ++it2)
    {
      if ((*it).iChannelNumber == (*it2).iChannelNumber)
      {
        (*it2).isHidden = true;
      }
    }
  }
  return true;
}

bool CPVRMagenta2::Bootstrap()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  m_deviceTokensUrl.clear();
  m_manifestBaseUrl.clear();
  m_clientModel.clear();
  m_deviceModel.clear();

  std::string url = BOOTSTRAP_URL;
  replace(url, "{configGroupId}", CONFIG_GROUP_ID);
  url = url + "deviceid=" + m_deviceId;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (doc.HasMember("baseSettings"))
  {
    const rapidjson::Value& baseSettings = doc["baseSettings"];
    m_clientModel = Utils::JsonStringOrEmpty(baseSettings, "clientModel");
    m_deviceModel = Utils::JsonStringOrEmpty(baseSettings, "deviceModel");
    m_accountBaseUrl = Utils::JsonStringOrEmpty(baseSettings, "accountBaseUrl");
    m_sam3ClientId = Utils::JsonStringOrEmpty(baseSettings, "sam3ClientId");
    m_deviceTokensUrl = Utils::JsonStringOrEmpty(baseSettings, "deviceTokensUrl");
    m_lineAuthUrl = Utils::JsonStringOrEmpty(baseSettings, "lineAuthUrl");
    if (doc.HasMember("dcm"))
    {
      const rapidjson::Value& dcm = doc["dcm"];
      m_manifestBaseUrl = Utils::JsonStringOrEmpty(dcm, "manifestBaseUrl");
    }
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Bootstrap returned without expected parameters");
    return false;
  }

  return true;
}

bool CPVRMagenta2::GetParameter(const std::string& key, std::string& value)
{
  for (const auto& parameter : m_parameters)
  {
    if (parameter.key == key) {
      value = parameter.value;
      return true;
    }
  }
  return false;
}

bool CPVRMagenta2::DeviceManifest()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  //m_deviceId = "25dff370-a775-4cdf-a724-3ab5fc9b3a1c";

  std::string url = m_deviceTokensUrl +
                    "?model=" + Utils::UrlEncode(DEVICEMODEL) +
                    "&deviceId=" + m_deviceId +
                    "&appname=" + APPNAME +
                    "&appVersion=" + APPVERSION +
                    "&firmware=" + Utils::UrlEncode(FIRMWARE) +
                    "&runtimeVersion=" + RUNTIMEVERSION +
                    "&duid=" + m_deviceId;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("settings") || !doc.HasMember("sts"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load device manifest");
    return false;
  }
  const rapidjson::Value& settings = doc["settings"];
  if (settings.HasMember("parameters")) {
    m_parameters.clear();
    const rapidjson::Value& parameters = settings["parameters"];
    for (rapidjson::SizeType i = 0; i < parameters.Size(); i++)
    {
      Magenta2KV kvpair;
      kvpair.key = Utils::JsonStringOrEmpty(parameters[i], "key");
      kvpair.value = Utils::JsonStringOrEmpty(parameters[i], "value");
      m_parameters.emplace_back(kvpair);
    }
    GetParameter("imageScalingBasicUrl", m_ngiss.basicUrl);
    GetParameter("imageScalingCallParameter", m_ngiss.callParameter);
    GetParameter("mpxBasicUrlGetApplicableDistributionRights", m_basicUrlGetApplicableDistributionRights);
    GetParameter("LineAuthURL", m_lineAuthUrl);
    GetParameter("mpxLocationIdUri", m_locationIdUri);
    GetParameter("mpxAccountPid", m_accountPid);
    GetParameter("mpxBasicUrlAllChannelSchedulesFeed", m_allChannelSchedulesFeed);
    GetParameter("mpxAllProgramsFeedUrl", m_allProgramsFeedUrl);
    GetParameter("widevineLicenseAcquisitionURL", m_widevineLicenseAcquisitionUrl);
  }
  const rapidjson::Value& sts = doc["sts"];
  m_sts.authorizeTokensUrl = Utils::JsonStringOrEmpty(sts, "authorizeTokensUrl");
  m_sts.deviceToken = Utils::JsonStringOrEmpty(sts, "deviceToken");

  return true;
}

bool CPVRMagenta2::Manifest()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  replace(m_manifestBaseUrl, "{configGroupId}", CONFIG_GROUP_ID);
  std::string url = m_manifestBaseUrl + "?deviceid=" + m_deviceId;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("mpx") || !doc.HasMember("livetv") || !doc.HasMember("ngiss"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load manifest");
    return false;
  }
  const rapidjson::Value& mpx = doc["mpx"];
  m_accountPid = Utils::JsonStringOrEmpty(mpx, "accountPid");
  m_locationIdUri = Utils::JsonStringOrEmpty(mpx, "locationIdUri");
  m_pvrBaseUrl = Utils::JsonStringOrEmpty(mpx, "pvrBaseUrl");
  m_basicUrlGetApplicableDistributionRights = Utils::JsonStringOrEmpty(mpx, "basicUrlGetApplicableDistributionRights");
  m_basicUrlSelectorService = Utils::JsonStringOrEmpty(mpx, "basicUrlSelectorService");
  if (mpx.HasMember("feeds"))
  {
    const rapidjson::Value& feeds = mpx["feeds"];
    m_entitledChannelsFeed = Utils::JsonStringOrEmpty(feeds, "entitledChannelsFeed");
    m_allChannelSchedulesFeed = Utils::JsonStringOrEmpty(feeds, "allChannelSchedulesFeed");
    m_allChannelStationsFeed = Utils::JsonStringOrEmpty(feeds, "allChannelStationsFeed");
    m_allListingsFeedUrl = Utils::JsonStringOrEmpty(feeds, "allListingsFeedUrl");
    m_allProgramsFeedUrl = Utils::JsonStringOrEmpty(feeds, "allProgramsFeedUrl");
    m_liveTvCategoryFeed = Utils::JsonStringOrEmpty(feeds, "liveTvCategoryFeed");
  }
  const rapidjson::Value& livetv = doc["livetv"];
  if (livetv.HasMember("drm"))
  {
    const rapidjson::Value& drm = livetv["drm"];
    m_widevineLicenseAcquisitionUrl = Utils::JsonStringOrEmpty(drm, "widevineLicenseAcquisitionUrl");
  }
  const rapidjson::Value& ngiss = doc["ngiss"];
  m_ngiss.basicUrl = Utils::JsonStringOrEmpty(ngiss, "basicUrl");
  m_ngiss.callParameter = Utils::JsonStringOrEmpty(ngiss, "callParameter");

//  kodi::Log(ADDON_LOG_DEBUG, "m_entitledChannelsFeed: %s", m_entitledChannelsFeed.c_str());

  return true;
}

bool CPVRMagenta2::GetDistributionRights()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  m_distributionRights.clear();
  std::string url = m_basicUrlGetApplicableDistributionRights + "?form=json&schema=1.2";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("getApplicableDistributionRightsResponse"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get distribution rights");
    return false;
  }
  const rapidjson::Value& rights = doc["getApplicableDistributionRightsResponse"];
  for (rapidjson::SizeType i = 0; i < rights.Size(); i++)
  {
    std::string right = rights[i].GetString();
    m_distributionRights.emplace_back(right);
    kodi::Log(ADDON_LOG_DEBUG, "Added right: %s", right.c_str());
  }

  return true;
}

void CPVRMagenta2::AddChannelEntry(const rapidjson::Value& entry)
{
  Magenta2Channel channel;
  channel.iUniqueId = Utils::JsonIntOrZero(entry, "channelNumber");
  channel.title = Utils::JsonStringOrEmpty(entry, "title");
  channel.id = Utils::JsonStringOrEmpty(entry, "id");
  channel.iChannelNumber = Utils::JsonIntOrZero(entry, "dt$displayChannelNumber");
//      channel.isEntitled = false;
  channel.isHidden = false;
  channel.bRadio = false;
  const rapidjson::Value& stations = entry["stations"];
  for (rapidjson::Value::ConstMemberIterator itr = stations.MemberBegin(); itr != stations.MemberEnd(); ++itr)
  {
    const rapidjson::Value& stationItem = (itr->value);
    channel.stationsId = Utils::JsonStringOrEmpty(stationItem, "id");
    channel.strChannelName = Utils::JsonStringOrEmpty(stationItem, "title");
    channel.isHd = Utils::JsonBoolOrFalse(stationItem, "isHd");
    const rapidjson::Value& mediaPids = stationItem["era$mediaPids"];
    channel.mediaPath = Utils::JsonStringOrEmpty(mediaPids, "urn:theplatform:tv:location:any");
    if (stationItem.HasMember("thumbnails"))
    {
      const rapidjson::Value& thumbnails = stationItem["thumbnails"];
      for (int i=0; i<Magenta2StationThumbnailTypes.size(); i++)
      {
        if (thumbnails.HasMember(Magenta2StationThumbnailTypes[i].c_str()))
        {
          const rapidjson::Value& thumbnail = thumbnails[Magenta2StationThumbnailTypes[i].c_str()];
          Magenta2Picture picture;
          picture.title = Utils::JsonStringOrEmpty(thumbnail, "title");
          picture.url = Utils::JsonStringOrEmpty(thumbnail, "url");
          picture.width = Utils::JsonIntOrZero(thumbnail, "width");
          picture.height = Utils::JsonIntOrZero(thumbnail, "height");
          channel.thumbnails.emplace_back(picture);
        }
      }
    }
    CPVRMagenta2::m_channels.emplace_back(channel);
    kodi::Log(ADDON_LOG_DEBUG, "Added channel: %u %s station: %s", channel.iUniqueId, channel.strChannelName.c_str(), channel.stationsId.c_str());
  }
}

void CPVRMagenta2::AddEntitlementEntry(const rapidjson::Value& entry)
{
  const rapidjson::Value& rights = entry["distributionRightIds"];
  unsigned int channelNumber = Utils::JsonIntOrZero(entry, "dt$channelNumber");
  for (rapidjson::SizeType i = 0; i < rights.Size(); i++)
  {
    AddDistributionRight(channelNumber, rights[i].GetString());
  }
}

bool CPVRMagenta2::GetFeed(/*const int& feed,*/ const int& maxEntries, /*const std::string& params,*/ std::string& baseUrl/*, kodi::addon::PVREPGTagsResultSet& results*/,
        handleentry_t HandleEntry)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  int startIndex = 1;
  int endIndex = maxEntries;
  bool nextRequest = true;

  std::string url;
  std::string range;
  rapidjson::Document doc;

  while (nextRequest) {
    range = std::to_string(startIndex) + "-";
    if (endIndex != 0)
      range += std::to_string(endIndex);
    url = baseUrl + "&range=" + range;

    if (!GetPostJson(url, "", doc)) {
      return false;
    }

    if (!doc.HasMember("entries"))
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to get feed");
      return false;
    }
    int entryCount = Utils::JsonIntOrZero(doc, "entryCount");
    if (entryCount < maxEntries)
      nextRequest = false;
    else {
      startIndex += entryCount;
      endIndex += entryCount;
    }
    const rapidjson::Value& entries = doc["entries"];
    for (rapidjson::SizeType i = 0; i < entries.Size(); i++)
    {
      (this->*HandleEntry)(entries[i]);
/*
      switch (feed) {
        case FEED_ALL_CHANNELS:
          AddChannelEntry(entries[i]);
          break;
        case FEED_ENTITLED_CHANNELS:
          AddEntitlementEntry(entries[i]);
          break;
        case FEED_CHANNEL_SCHEDULE:
          const rapidjson::Value& listings = entries[i]["listings"];
          for (rapidjson::SizeType j = 0; j < entries.Size(); j++)
          {

          }
          break;
      }
*/
    }
  }

  return true;
}

CPVRMagenta2::CPVRMagenta2(CSettings* settings, HttpClient* httpclient):
  m_settings(settings),
  m_httpClient(httpclient)
{
  m_authMethods.password = false;
  m_authMethods.code = false;
  m_authMethods.line = false;
  m_sessionId = Utils::CreateUUID();
  kodi::Log(ADDON_LOG_DEBUG, "Current SessionID %s", m_sessionId.c_str());
  m_httpClient->SetSessionId(m_sessionId);
  m_deviceId = m_settings->GetMagentaDeviceID();
  if (m_deviceId.empty()) {
    m_deviceId = Utils::CreateUUID();
    m_settings->SetSetting("deviceid", m_deviceId);
  }
  kodi::Log(ADDON_LOG_DEBUG, "Current DeviceID %s", m_deviceId.c_str());

  if (!Bootstrap())
    return;
  if (!m_deviceTokensUrl.empty()) {
    if (!DeviceManifest())
      return;
  } else if (!m_manifestBaseUrl.empty()) {
    if (!Manifest())
      return;
  } else
  {
    kodi::Log(ADDON_LOG_DEBUG, "No appropriate URL found");
    return;
  }

  m_channels.clear();
  if (m_allChannelStationsFeed.empty())
  {
    if (!GetParameter("mpxDefaultUrlAllChannelStationsFeed", m_allChannelStationsFeed))
      return;
  } else {
    replace(m_allChannelStationsFeed, "{MpxAccountPid}", m_accountPid);
  }
  std::string baseUrl = m_allChannelStationsFeed + "?lang=short-de";

  GetFeed(/*FEED_ALL_CHANNELS,*/ MAX_CHANNEL_ENTRIES, baseUrl/*, nullptr*/, &CPVRMagenta2::AddChannelEntry);
  GetDistributionRights();

  replace(m_entitledChannelsFeed, "{MpxAccountPid}", m_accountPid);
  baseUrl = m_entitledChannelsFeed + "?byDistributionRightId=";
  for (const auto& right : m_distributionRights)
  {
    if (baseUrl != m_entitledChannelsFeed + "?byDistributionRightId=")
      baseUrl += "%7C"; // "|"
    baseUrl += Utils::UrlEncode(right);
  }
  GetFeed(/*FEED_ENTITLED_CHANNELS,*/ MAX_CHANNEL_ENTRIES, baseUrl/*, nullptr*/, &CPVRMagenta2::AddEntitlementEntry);
  HideDuplicateChannels();
  replace(m_allChannelSchedulesFeed, "{{MpxAccountPid}}", m_accountPid);
  replace(m_allProgramsFeedUrl, "{{MpxAccountPid}}", m_accountPid);
  GetMyGenres();
}

CPVRMagenta2::~CPVRMagenta2()
{
  m_channels.clear();
}

PVR_ERROR CPVRMagenta2::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsEPGEdl(false);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(false);
  capabilities.SetSupportsChannelGroups(false);
//  capabilities.SetSupportsChannelGroups(m_settings->IsGroupsenabled());
  capabilities.SetSupportsRecordings(false);
  capabilities.SetSupportsRecordingsDelete(false);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsLastPlayedPosition(false);
  capabilities.SetSupportsTimers(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsProviders(false);
  /* PVR recording lifetime values and presentation.*/
//  std::vector<kodi::addon::PVRTypeIntValue> lifetimeValues;
//  GetLifetimeValues(lifetimeValues, true);
//  capabilities.SetRecordingsLifetimeValues(lifetimeValues);
//  capabilities.SetSupportsAsyncEPGTransfer(true);
//  capabilities.SetHandlesInputStream(true);

  return PVR_ERROR_NO_ERROR;
}

bool CPVRMagenta2::GetStreamParameters(const std::string& url, std::string& src, std::string& releasePid)
{
  tinyxml2::XMLDocument smilDoc;

  if (!GetSmil(url, smilDoc))
    return false;

  tinyxml2::XMLElement* pRootElement = smilDoc.RootElement();
//  kodi::Log(ADDON_LOG_DEBUG, "Root: %s", pRootElement->Value().c_str());
  tinyxml2::XMLElement* pElement = pRootElement->FirstChildElement("body");
  if (!pElement) {
    kodi::Log(ADDON_LOG_ERROR, "No Body found");
    return false;
  }
  tinyxml2::XMLElement* pHead = pRootElement->FirstChildElement("head");
  if (!pHead) {
    kodi::Log(ADDON_LOG_ERROR, "No Head found");
    return false;
  }
  tinyxml2::XMLElement* pMeta = pHead->FirstChildElement("meta");
  std::string name;
  std::string content;
  while (pMeta)
  {
    name = pMeta->Attribute("name");
    content = pMeta->Attribute("content");
    if (name == "concurrencyInstance")
      m_currentLock.instance = content;
    else if (name == "updateLockInterval")
      m_currentLock.updateInterval = stoi(content);
    else if (name == "concurrencyServiceUrl")
      m_currentLock.serviceUrl = content;
    else if (name == "lockId")
      m_currentLock.id = content;
    else if (name == "lockSequenceToken")
      m_currentLock.token = content;
    else if (name == "lock")
      m_currentLock.lock = content;
    else
      kodi::Log(ADDON_LOG_DEBUG, "Unknown Meta Content name: %s with content: %s", name.c_str(), content.c_str());
    pMeta = pMeta->NextSiblingElement();
  }
  tinyxml2::XMLElement* seq = pElement->FirstChildElement("seq");
  if (!seq) {
    kodi::Log(ADDON_LOG_ERROR, "No seq found");
    return false;
  }
  tinyxml2::XMLElement* reffailed = seq->FirstChildElement("ref");
  tinyxml2::XMLElement* switchgood = seq->FirstChildElement("switch");
  std::string value;
  std::string trackingData;
  releasePid.clear();
  src.clear();
  trackingData.clear();
  if (reffailed) {
    src = reffailed->Attribute("src");
    kodi::Log(ADDON_LOG_DEBUG, "SRC: %s", src);
    std::string title = reffailed->Attribute("title");
    kodi::Log(ADDON_LOG_DEBUG, "Title: %s", title.c_str());
    std::string abstract = reffailed->Attribute("abstract");
    kodi::Log(ADDON_LOG_DEBUG, "Abstract: %s", abstract.c_str());
    tinyxml2::XMLElement* pParm = reffailed->FirstChildElement("param");
    bool isException = false;
    int responseCode = 0;
    while (pParm) {
      name = pParm->Attribute("name");
      value = pParm->Attribute("value");
      kodi::Log(ADDON_LOG_DEBUG, "Param Name: %s Value: %s", name.c_str(), value.c_str());
      if ((name == "isException") && (value == "true"))
        isException = true;
      if (name == "responseCode")
      {
        try {
          responseCode = std::stoi(value);
        } catch (const std::exception& e) {}
      }
      pParm = pParm->NextSiblingElement();
    }
    if (isException)
      kodi::gui::dialogs::OK::ShowAndGetInput(title, abstract);
    return false;
  } else if (switchgood) {
    tinyxml2::XMLElement* refgood = switchgood->FirstChildElement("ref");
    src = refgood->Attribute("src");
    tinyxml2::XMLElement* ppParm = refgood->FirstChildElement("param");
    if (ppParm) {
      name = ppParm->Attribute("name");
      value = ppParm->Attribute("value");
      if (name == "trackingData") {
        trackingData = value;
        kodi::Log(ADDON_LOG_DEBUG, "Tracking Data: %s", trackingData.c_str());
        std::vector<std::string> out;
        tokenize2(trackingData, "|", out);
        for (const auto& data : out)
        {
    //      kodi::Log(ADDON_LOG_DEBUG, "Tracking Data: %s", data.c_str());
          if (data.substr(0,4) == "pid=") {
            releasePid = data;
            releasePid.erase(0,4);
          }
        }
      }
    }
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Unknown structure");
    return false;
  }
  return true;
}

bool CPVRMagenta2::ReleaseLock()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_currentLock.serviceUrl.empty())
    return false;

  std::string url = m_currentLock.serviceUrl + "/web/Concurrency/unlock?_clientId=" + m_deviceId +
                                               "&_encryptedLock=" + Utils::UrlEncode(m_currentLock.lock) +
                                               "&_id=" + Utils::UrlEncode(m_currentLock.id) +
                                               "&_sequenceToken=" + Utils::UrlEncode(m_currentLock.token) +
                                               "&form=json&schema=1.0";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  return true;
}

PVR_ERROR CPVRMagenta2::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::string& url,
                                    bool realtime, bool playTimeshiftBuffer, bool epgplayback)
{
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");
  properties.emplace_back(PVR_STREAM_PROPERTY_EPGPLAYBACKASLIVE, epgplayback ? "true" : "false");

  std::string src;
  std::string releasePid;
  GetStreamParameters(url, src, releasePid);
  if (src.empty()) {
    return PVR_ERROR_FAILED;
  }
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, src);
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", src.c_str());
  if (!releasePid.empty()) {
    std::string personaltoken = base64_decode(m_settings->GetMagenta2PersonalToken());
    kodi::Log(ADDON_LOG_DEBUG, "Personal Token: %s", personaltoken.c_str());
    personaltoken.erase(0, m_accountBaseUrl.length());
    std::string account = m_accountBaseUrl + personaltoken.substr(0, personaltoken.find(":"));
    kodi::Log(ADDON_LOG_DEBUG, "Account: %s", account.c_str());
    personaltoken.erase(0, personaltoken.find(":") + 1);
    kodi::Log(ADDON_LOG_DEBUG, "Token: %s", personaltoken.c_str());
    kodi::Log(ADDON_LOG_DEBUG, "ReleasePid: %s", releasePid.c_str());
    std::string lkey = m_widevineLicenseAcquisitionUrl + "?account=" + Utils::UrlEncode(account) +
                                                         "&releasePid=" + releasePid +
                                                         "&token=" + personaltoken +
                                                         "&schema=1.0";
    lkey += "|"
            "Origin=https://web2.magentatv.de"
            "&Referer=https://web2.magentatv.de"
//            "&User-Agent=Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.0.0.0 Safari/537.36"
            "&Content-Type= "
            "|R{SSM}|";
    kodi::Log(ADDON_LOG_DEBUG, "Licence Key: %s", lkey.c_str());
    properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
    //properties.emplace_back("inputstream.adaptive.license_key", lkey);
    std::string urlFirst;
    std::string urlSecond;
    if (lkey.length() > 1024) {
        urlFirst = lkey.substr(0,1000);
        urlSecond = lkey.substr(1000,std::string::npos);
        kodi::Log(ADDON_LOG_DEBUG, "First %s", urlFirst.c_str());
        kodi::Log(ADDON_LOG_DEBUG, "Second %s", urlSecond.c_str());
        properties.emplace_back("inputstream.adaptive.license_url", urlFirst);
        properties.emplace_back("inputstream.adaptive.license_url_append", urlSecond);
    }
    properties.emplace_back("inputstream.adaptive.play_timeshift_buffer", playTimeshiftBuffer ? "true" : "false");
//    properties.emplace_back("inputstream.adaptive.manifest_type", "mpd");
    properties.emplace_back("inputstream.adaptive.license_type", "com.widevine.alpha");
  }

//  properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetChannelsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  amount = m_channels.size();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Channels Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

std::string CPVRMagenta2::GetNgissUrl(const std::string& url, const int& width, const int& height)
{
  return m_ngiss.basicUrl + "iss/?" +
         m_ngiss.callParameter + "&ar=keep" +
         "&src=" + Utils::UrlEncode(url) +
         "&x=" + std::to_string(width) +
         "&y=" + std::to_string(height);
}

PVR_ERROR CPVRMagenta2::GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& channel : m_channels)
  {

    if ((channel.bRadio == bRadio) && (!m_settings->IsHiddenDeactivated() || !channel.isHidden))
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(static_cast<unsigned int>(channel.iUniqueId));
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(static_cast<unsigned int>(channel.iChannelNumber));
      kodiChannel.SetChannelName(channel.strChannelName);

      int pictureNo = 0;
      for (int i=0; i<channel.thumbnails.size(); i++)
      {
        if ((m_settings->UseWhiteLogos() && channel.thumbnails[i].title == "stationLogo.png") ||
            (!m_settings->UseWhiteLogos() && channel.thumbnails[i].title == "stationLogoColored.png"))
          pictureNo = i;
      }
      std::string iconUrl = GetNgissUrl(channel.thumbnails[pictureNo].url,
                                        channel.thumbnails[pictureNo].width,
                                        channel.thumbnails[pictureNo].height);
//      kodi::Log(ADDON_LOG_DEBUG, "Icon Url: %s", iconUrl.c_str());
      kodiChannel.SetIconPath(iconUrl);
      kodiChannel.SetIsHidden(channel.isHidden);
      kodiChannel.SetHasArchive(false);

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& mychannel : m_channels)
  {
    if (channel.GetUniqueId() == mychannel.iUniqueId)
    {
      ReleaseLock();
      std::string streamUrl = m_basicUrlSelectorService;
      streamUrl = "https://link.theplatform.eu/s/mdeprod/media/" + mychannel.mediaPath + "?format=SMIL&formats=MPEG-DASH&tracking=true";
      kodi::Log(ADDON_LOG_DEBUG, "Stream URL -> %s", streamUrl.c_str());

      return SetStreamProperties(properties, streamUrl, true, false, false);
    }
  }
  return PVR_ERROR_FAILED;
}

void CPVRMagenta2::AddEPGEntry(const int& channelNumber, const rapidjson::Value& epgItem, kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::addon::PVREPGTag tag;

  unsigned int epg_tag_flags = EPG_TAG_FLAG_UNDEFINED;
  int guid;
  try {
    guid = stoi(Utils::JsonStringOrEmpty(epgItem, "guid").substr(11, std::string::npos), 0, 16);
  }
  catch (const std::exception& e) {
    return;
  }
  tag.SetUniqueBroadcastId(static_cast<unsigned int>(guid));
  tag.SetUniqueChannelId(static_cast<unsigned int>(channelNumber));
  tag.SetTitle(Utils::JsonStringOrEmpty(epgItem, "title"));
  tag.SetPlot(Utils::JsonStringOrEmpty(epgItem, "description"));
  tag.SetPlotOutline(Utils::JsonStringOrEmpty(epgItem, "shortDescription"));

  if (epgItem.HasMember("thumbnails"))
  {
    const rapidjson::Value& thumbnails = epgItem["thumbnails"];
    rapidjson::Value::ConstMemberIterator itr = thumbnails.MemberBegin();
    ++itr;
    if (itr != thumbnails.MemberEnd())
    {
      const rapidjson::Value& thumbnailsItem = (itr->value);

      int width = Utils::JsonIntOrZero(thumbnailsItem, "width");
      int height = Utils::JsonIntOrZero(thumbnailsItem, "height");
      if ((width == 0) && (height == 0))
        tag.SetIconPath(Utils::JsonStringOrEmpty(thumbnailsItem, "url"));
      else
        tag.SetIconPath(GetNgissUrl(Utils::JsonStringOrEmpty(thumbnailsItem, "url"), width, height));
    }
  }

  int seasonNum = Utils::JsonIntOrZero(epgItem, "tvSeasonNumber");
  if (seasonNum != 0)
  {
    tag.SetSeriesNumber(seasonNum);
    epg_tag_flags += EPG_TAG_FLAG_IS_SERIES;
  }
  int episodeNum = Utils::JsonIntOrZero(epgItem, "tvSeasonEpisodeNumber");
  if (episodeNum != 0)
    tag.SetEpisodeNumber(episodeNum);
  tag.SetYear(Utils::JsonIntOrZero(epgItem, "year"));
  tag.SetEpisodeName(Utils::JsonStringOrEmpty(epgItem, "secondaryTitle"));
  std::string seriesId = Utils::JsonStringOrEmpty(epgItem, "seriesId");
  if (!seriesId.empty())
    tag.SetSeriesLink(seriesId);

  if (epgItem.HasMember("ratings") && epgItem["ratings"].GetType() != 0)
  {
    const rapidjson::Value& ratings = epgItem["ratings"];
    if (ratings.Size() > 0) {
      std::string ratingStr = Utils::JsonStringOrEmpty(ratings[0], "rating");
      int rating = 0;
      try {
        rating = stoi(ratingStr);
      } catch (const std::exception& e) {}
      if (rating > 0)
        tag.SetParentalRating(rating);
    }
  }

  if (epgItem.HasMember("dt$originalIds") && epgItem["dt$originalIds"].GetType() != 0)
  {
    const rapidjson::Value& originalIds = epgItem["dt$originalIds"];
    std::string imdbNumber = Utils::JsonStringOrEmpty(originalIds, "imdb");
    if (!imdbNumber.empty())
      tag.SetIMDBNumber(imdbNumber);
  }

  if (epgItem.HasMember("credits")) {
    const rapidjson::Value& credits = epgItem["credits"];
    std::string cast = "";
    std::string director = "";
    std::string writer = "";
    for (rapidjson::SizeType i = 0; i < credits.Size(); i++)
    {
      std::string creditType = Utils::JsonStringOrEmpty(credits[i], "creditType");
      if (creditType == "DIRECTOR")
      {
        if (director != "")
          director += EPG_STRING_TOKEN_SEPARATOR;
        director += Utils::JsonStringOrEmpty(credits[i], "personName");
      } else if (creditType == "SCRIPTWRITER")
      {
        if (writer != "")
          writer += EPG_STRING_TOKEN_SEPARATOR;
        writer += Utils::JsonStringOrEmpty(credits[i], "personName");
      } else if ((creditType == "ACTOR") || (creditType == "AD6"))
      {
        if (cast != "")
          cast += EPG_STRING_TOKEN_SEPARATOR;
        cast += Utils::JsonStringOrEmpty(credits[i], "personName");
      } else if (creditType == "PRODUCER")
      {

      } else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Unknown Credit Type: %s Person Name: %s", creditType.c_str(), Utils::JsonStringOrEmpty(credits[i], "personName").c_str());
      }
    }
    tag.SetCast(cast);
    tag.SetDirector(director);
    tag.SetWriter(writer);
  }
/*
  std::string programType = Utils::JsonStringOrEmpty(epgItem, "programType");
  if ((programType != "episode") && (programType != "movie"))
    kodi::Log(ADDON_LOG_DEBUG, "Unknown program type %s", programType.c_str());
*/
  std::string genre_primary = "";
  std::string genre_secondary = "";
  if (epgItem.HasMember("tags")) {
    const rapidjson::Value& tags = epgItem["tags"];
    for (rapidjson::SizeType i = 0; i < tags.Size(); i++)
    {
      std::string scheme = Utils::JsonStringOrEmpty(tags[i], "scheme");
      std::string title = Utils::JsonStringOrEmpty(tags[i], "title");
      if (scheme == "genre-primary")
      {
        genre_primary += title;
        genre_primary += EPG_STRING_TOKEN_SEPARATOR;
      } else if (scheme == "genre-secondary")
      {
        if (title != genre_primary)
        {
          genre_secondary += title;
          genre_secondary += EPG_STRING_TOKEN_SEPARATOR;          
        }
      } else if (scheme == "category")
      {
        //Todo for later
      } else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Unknown scheme type: %s with title: %s", scheme.c_str(), title.c_str());
      }
    }
    genre_primary.erase(genre_primary.end() - 1);
    genre_secondary.erase(genre_secondary.end() - 1);
    kodi::Log(ADDON_LOG_DEBUG, "Primary Genres: %s", genre_primary.c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Secondary Genres: %s", genre_secondary.c_str());
  }

  tag.SetFlags(epg_tag_flags);

  if (epgItem.HasMember("listings") && epgItem["listings"].GetType() != 0) {
    const rapidjson::Value& listings = epgItem["listings"];
    for (rapidjson::SizeType i = 0; i < listings.Size(); i++) {
      tag.SetStartTime((time_t) (Utils::JsonInt64OrZero(listings[i], "startTime") / 1000));
      tag.SetEndTime((time_t) (Utils::JsonInt64OrZero(listings[i], "endTime") / 1000));
      results.Add(tag);
    }
  }
}

bool CPVRMagenta2::GetEPGFeed(const int& channelNumber, const std::string& baseUrl, kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  rapidjson::Document doc;
  if (!GetPostJson(baseUrl, "", doc)) {
    return false;
  }

  if (!doc.HasMember("entries"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get EPG feed");
    return false;
  }

  std::string guids = "";
  int entryCount = 0;
  const rapidjson::Value& entries = doc["entries"];
  for (rapidjson::SizeType i = 0; i < entries.Size(); i++)
  {
    if (!entries[i].HasMember("listings"))
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to get EPG listings");
      return false;
    }
    const rapidjson::Value& listings = entries[i]["listings"];
    for (rapidjson::SizeType j = 0; j < listings.Size(); j++)
    {
  //    AddEPGEntry(channelNumber, listings[j], results);
      if (listings[j].HasMember("program") && listings[j]["program"].GetType() != 0) {
        const rapidjson::Value& program = listings[j]["program"];
        guids += Utils::JsonStringOrEmpty(program, "guid") + "|";
        entryCount++;
      }
      if ((entryCount == 300) || (j == listings.Size()-1))
      {
        guids.erase(guids.end() - 1);
      //  kodi::Log(ADDON_LOG_DEBUG, "Guids found %s", guids.c_str());

        std::string programsUrl = m_allProgramsFeedUrl + "?form=cjson" +
                                                         "&byGuid=" + Utils::UrlEncode(guids) +
                                                         "&range=1-" + std::to_string(entryCount) +
                                                         "&fields=guid,title,description,listings.startTime,"
                                                         "listings.endTime,thumbnails,tvSeasonNumber,tvSeasonEpisodeNumber,"
                                                         "year,secondaryTitle,seriesId,ratings,dt$originalIds,"
                                                         "credits.creditType,credits.personName,shortDescription,tags"; //programType

        rapidjson::Document doc2;
        if (!GetPostJson(programsUrl, "", doc2)) {
          return false;
        }

        if (!doc2.HasMember("entries"))
        {
          kodi::Log(ADDON_LOG_ERROR, "Failed to get Programs feed");
          return false;
        }

        const rapidjson::Value& entries2 = doc2["entries"];
        for (rapidjson::SizeType k = 0; k < entries2.Size(); k++)
        {
          AddEPGEntry(channelNumber, entries2[k], results);
        }
        guids = "";
        entryCount = 0;
      }
    }
  }
  return true;
}

PVR_ERROR CPVRMagenta2::GetEPGForChannel(int channelUid,
                                         time_t start,
                                         time_t end,
                                         kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  kodi::Log(ADDON_LOG_DEBUG, "Start %u End %u", start, end);
//  kodi::Log(ADDON_LOG_DEBUG, "EPG Request for channel %i from %s to %s", channelUid, startTime.c_str(), endTime.c_str());

  std::string baseUrl = m_allChannelSchedulesFeed + "?form=cjson&byLocationId=" + Utils::UrlEncode(m_locationIdUri) +
                                                    "&byListingTime=" + Utils::UrlEncode(Utils::TimeToString2(start) + "~" + Utils::TimeToString2(end)) +
                                                    "&byChannelNumber=" + std::to_string(channelUid) +
                                                    "&range=1-1" +
                                                    "&fields=listings.program.guid";
  GetEPGFeed(channelUid, baseUrl, results);
  return PVR_ERROR_NO_ERROR;
}
