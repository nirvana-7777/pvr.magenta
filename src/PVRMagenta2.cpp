/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PVRMagenta2.h"

#include <algorithm>

#include "Globals.h"
#include <kodi/General.h>
#include <kodi/gui/dialogs/OK.h>
#include "Utils.h"
#include "Base64.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <tinyxml2.h>
#include <kodi/Filesystem.h>
#include "auth/AuthClient.h"

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

void PrepareTime(std::string& timestr)
{
  if (timestr.size() != 15)
    return;
  timestr.insert(4, "-");
  timestr.insert(7, "-");
  timestr.insert(13, ":");
  timestr.insert(16, ":");
  timestr += "Z";
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
    Magenta2Genre genre;
    genre.primaryGenre = Utils::JsonStringOrEmpty(genres[i], "primaryGenre");
    genre.genreType = Utils::JsonIntOrZero(genres[i], "genreType");
    if (genres[i].HasMember("genreSubType"))
      genre.genreSubType = Utils::JsonIntOrZero(genres[i], "genreSubType");
    else
      genre.genreSubType = -1;
    if (genres[i].HasMember("secondaryGenres"))
    {
      const rapidjson::Value& secondaryGenres = genres[i]["secondaryGenres"];
      for (rapidjson::SizeType j = 0; j < secondaryGenres.Size(); j++)
      {
        Magenta2SubGenre subGenre;
        subGenre.secondaryGenre = Utils::JsonStringOrEmpty(secondaryGenres[j], "secondaryGenre");
        subGenre.secondaryGenreType = Utils::JsonIntOrZero(secondaryGenres[j], "genreSubType");
        genre.secondaryGenres.emplace_back(subGenre);
      }
    }
    m_genres.emplace_back(genre);
    kodi::Log(ADDON_LOG_DEBUG, "Added genre: %s %i %i", genre.primaryGenre.c_str(), genre.genreType, genre.genreSubType);
  }

  return true;
}

bool CPVRMagenta2::GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc)
{
  int statusCode = 0;
  std::string result;

  if (body.empty()) {
    if (url.find(BOOTSTRAP_URL) != std::string::npos)// ||
//        (url.find(m_deviceTokensUrl) != std::string::npos) ||
//        (url.find(m_manifestBaseUrl) != std::string::npos))
      result = m_httpClient->HttpGetCached(url, 60 * 60 * 24 * 10, statusCode);
    else if ((url.find(m_allChannelStationsFeed) != std::string::npos) ||
             (url.find(m_liveTvCategoryFeed) != std::string::npos))
//             (url.find(m_entitledChannelsFeed) != std::string::npos))
      result = m_httpClient->HttpGetCached(url, 60 * 60 * 24 * 3, statusCode);
    else if (url.find(m_pvrBaseUrl) != std::string::npos)
      result = m_httpClient->HttpGetCached(url, 60, statusCode);
    else
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
      /*
      if (!m_authMethods.password && !m_authMethods.code && !m_authMethods.line)
        m_sam3Client->GetAuthMethods();
      if (m_authMethods.line)
      {
        kodi::Log(ADDON_LOG_DEBUG, "LineAuth");
//        LineAuth();
      }
      */
      if (m_authClient->ReLogin()) {
        kodi::Log(ADDON_LOG_DEBUG, "Reauth successful");
        if (body.empty()) {
          result = m_httpClient->HttpGet(url, statusCode);
        } else
        {
          //  kodi::Log(ADDON_LOG_DEBUG, "Body: %s", body.c_str());
          result = m_httpClient->HttpPost(url, body, statusCode);
          doc.Parse(result.c_str());
          if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
          {
            kodi::Log(ADDON_LOG_ERROR, "Failed to get JSON %s after reauth status code: %i", url.c_str(), statusCode);
            return false;
          }
        }
      } else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Reauth failed");
        kodi::gui::dialogs::OK::ShowAndGetInput("Reauth failed", "Reauth failed");
        return false;
      }
      //SingleSignOn();
    }
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "Get Json for %s answered response code: %i and title %s",
                                          url.c_str(),
                                          Utils::JsonIntOrZero(doc, "responseCode"),
                                          Utils::JsonStringOrEmpty(doc, "title").c_str());
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
      thisChannel.isHidden = false;
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

bool CPVRMagenta2::HideDuplicateChannels()
{
  for (auto it = m_channels.begin(); it!=m_channels.end(); ++it)
  {
    for (auto it2 = it+1; it2!=m_channels.end(); ++it2)
    {
      if ((*it).iChannelNumber == (*it2).iChannelNumber)
      {
        if ((*it).isHd)
        {
          if (m_settings->PreferHigherResolution()) {
            (*it2).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it2).strChannelName.c_str(), (*it2).iChannelNumber, (*it2).iUniqueId);
          } else {
            (*it).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it).strChannelName.c_str(), (*it).iChannelNumber, (*it).iUniqueId);
          }
        } else {
          if (m_settings->PreferHigherResolution()) {
            (*it).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it).strChannelName.c_str(), (*it).iChannelNumber, (*it).iUniqueId);
          } else {
            (*it2).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it2).strChannelName.c_str(), (*it2).iChannelNumber, (*it2).iUniqueId);
          }
        }
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
  replace(url, "{configGroupId}", Magenta2Parameters[m_platform].config_group_id);
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
    m_deviceTokensUrl = Utils::JsonStringOrEmpty(baseSettings, "deviceTokensUrl");
    m_authClient->SetSam3Url(Utils::JsonStringOrEmpty(baseSettings, "sam3Url"));
    m_authClient->SetSam3ClientId(Utils::JsonStringOrEmpty(baseSettings, "sam3ClientId"));
    m_authClient->SetLineAuthUrl(Utils::JsonStringOrEmpty(baseSettings, "lineAuthUrl"));
    m_authClient->SetTaaUrl(Utils::JsonStringOrEmpty(baseSettings, "taaUrl"));
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

bool CPVRMagenta2::GetCategories()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  replace(m_liveTvCategoryFeed, "{MpxAccountPid}", m_accountPid);

  std::string url = m_liveTvCategoryFeed;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("entries"))
    return false;

  const rapidjson::Value& entries = doc["entries"];
  for (rapidjson::SizeType i = 0; i < entries.Size(); i++)
  {
    Magenta2Category category;
    category.id = Utils::JsonStringOrEmpty(entries[i], "id");
    category.description = Utils::JsonStringOrEmpty(entries[i], "description");
    category.parentId = Utils::JsonStringOrEmpty(entries[i], "parentId");
    category.order = Utils::JsonIntOrZero(entries[i], "order");
    category.scheme = Utils::JsonStringOrEmpty(entries[i], "scheme");
    category.level = Utils::JsonIntOrZero(entries[i], "level");
    m_categories.emplace_back(category);
    kodi::Log(ADDON_LOG_DEBUG, "Adding category %s", category.description.c_str());
  }

  return true;
}

bool CPVRMagenta2::DeviceManifest()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_deviceTokensUrl +
                    "?model=" + Utils::UrlEncode(Magenta2Parameters[m_platform].device_model) +
                    "&deviceId=" + m_deviceId +
                    "&appname=" + Magenta2Parameters[m_platform].app_name +
                    "&appVersion=" + Magenta2Parameters[m_platform].app_version +
                    "&firmware=" + Utils::UrlEncode(Magenta2Parameters[m_platform].firmware) +
                    "&runtimeVersion=" + Magenta2Parameters[m_platform].runtime +
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
//    GetParameter("LineAuthURL", m_lineAuthUrl);
    GetParameter("mpxLocationIdUri", m_locationIdUri);
    GetParameter("mpxAccountPid", m_accountPid);
    GetParameter("mpxBasicUrlAllChannelSchedulesFeed", m_allChannelSchedulesFeed);
    GetParameter("mpxAllProgramsFeedUrl", m_allProgramsFeedUrl);
    GetParameter("widevineLicenseAcquisitionURL", m_widevineLicenseAcquisitionUrl);
    GetParameter("mpxBasicUrlEntitledChannelsFeed", m_entitledChannelsFeed);
    GetParameter("mpxBasicUrlSelectorService", m_basicUrlSelectorService);
    replace(m_entitledChannelsFeed, "{{MpxAccountPid}}", m_accountPid);
    m_liveTvCategoryFeed = "https://feed.media.theplatform.eu/f/{MpxAccountPid}/{MpxAccountPid}-livetv-categories/Category";
    GetParameter("mpxPvrBaseUrl", m_pvrBaseUrl);
    GetParameter("mpxAccountUri", m_mpxAccountUri);
    m_authClient->SetAccountUri(m_mpxAccountUri);
  }
  const rapidjson::Value& sts = doc["sts"];
  m_authClient->SetAuthorizeTokenUrl(Utils::JsonStringOrEmpty(sts, "authorizeTokensUrl"));
  std::string deviceToken = Utils::JsonStringOrEmpty(sts, "deviceToken");
  m_authClient->SetDeviceToken(deviceToken);
  m_httpClient->SetDeviceToken(deviceToken);
  m_authClient->InitSam3();

  return true;
}

bool CPVRMagenta2::Manifest()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  replace(m_manifestBaseUrl, "{configGroupId}", Magenta2Parameters[m_platform].config_group_id);
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

void CPVRMagenta2::AddGroupChannel(const std::string& id, const int& channelUid)
{
  for (auto& category : m_categories)
  {
    if (id == category.id)
    {
      category.channelUids.emplace_back(channelUid);
    }
  }
}

void CPVRMagenta2::AddChannelEntry(const rapidjson::Value& entry)
{
  Magenta2Channel channel;
  channel.iUniqueId = Utils::JsonIntOrZero(entry, "channelNumber");
  channel.title = Utils::JsonStringOrEmpty(entry, "title");
  channel.id = Utils::JsonStringOrEmpty(entry, "id");
  channel.iChannelNumber = Utils::JsonIntOrZero(entry, "dt$displayChannelNumber");
//      channel.isEntitled = false;
  if (m_settings->HideUnsubscribed())
    channel.isHidden = true;
  else
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
    if (stationItem.HasMember("dt$categoryIds"))
    {
      const rapidjson::Value& categoryIds = stationItem["dt$categoryIds"];
      for (int j=0; j<categoryIds.Size(); j++)
      {
        AddGroupChannel(categoryIds[j].GetString(), channel.iUniqueId);
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
  m_sessionId = Utils::CreateUUID();
  kodi::Log(ADDON_LOG_DEBUG, "Current SessionID %s", m_sessionId.c_str());
  m_httpClient->SetSessionId(m_sessionId);
  m_deviceId = m_settings->GetMagentaDeviceID();
  m_platform = m_settings->GetTerminalType();
  if (m_deviceId.empty()) {
    m_deviceId = Utils::CreateUUID();
    m_settings->SetSetting("deviceid", m_deviceId);
  }
  kodi::Log(ADDON_LOG_DEBUG, "Current DeviceID %s", m_deviceId.c_str());
  m_authClient = new AuthClient(m_settings, m_httpClient);
  m_httpClient->SetAuthClient(m_authClient);

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
  m_categories.clear();
  if (m_settings->IsGroupsenabled())
    GetCategories();

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

  //TODO: Remove
//  m_sam3Client->Sam3Login();

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
  capabilities.SetSupportsChannelGroups(m_settings->IsGroupsenabled());
  capabilities.SetSupportsRecordings(true);
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
    kodi::Log(ADDON_LOG_DEBUG, "SRC: %s", src.c_str());
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
  GetStreamParameters(url + "?format=SMIL&formats=MPEG-DASH&tracking=true", src, releasePid);
  if (src.empty()) {
    return PVR_ERROR_FAILED;
  }
  if (src.find("begin=") != std::string::npos)
  {
    std::string begin = src.substr(src.find("begin=") + 6, 15);
    std::string end = src.substr(src.find("end=") + 4, 15);
    PrepareTime(begin);
    PrepareTime(end);
    time_t beginTime = Utils::StringToTime2(begin);
    kodi::Log(ADDON_LOG_DEBUG, "Begin time: %s / %u end time: %s / %u", begin.c_str(), beginTime, end.c_str(), Utils::StringToTime2(end));
    if (time(NULL) - TIMEBUFFER2 > beginTime)
    {
      std::string newBegin = Utils::TimeToString3(time(NULL) - TIMEBUFFER2 + 10);
      kodi::Log(ADDON_LOG_DEBUG, "New begin time: %s", newBegin.c_str());
      src.replace(src.find("begin=") + 6, 15, newBegin);
    }
  }
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, src);
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", src.c_str());
  if (!releasePid.empty()) {
    std::string personaToken;
    if (!m_authClient->GetPersonaToken(personaToken))
      return PVR_ERROR_FAILED;

    personaToken = base64_decode(personaToken);
    kodi::Log(ADDON_LOG_DEBUG, "PersonaToken: %s", personaToken.c_str());
    personaToken.erase(0, m_accountBaseUrl.length());
    std::string account = m_accountBaseUrl + personaToken.substr(0, personaToken.find(":"));
    kodi::Log(ADDON_LOG_DEBUG, "Account: %s", account.c_str());
    personaToken.erase(0, personaToken.find(":") + 1);
    kodi::Log(ADDON_LOG_DEBUG, "Token: %s", personaToken.c_str());

    kodi::Log(ADDON_LOG_DEBUG, "ReleasePid: %s", releasePid.c_str());

    std::string lkey = m_widevineLicenseAcquisitionUrl + "?account=" + Utils::UrlEncode(account) +
                                                         "&releasePid=" + releasePid +
                                                         "&token=" + personaToken +
                                                         "&schema=1.0";

//    std::string lkey = m_widevineLicenseAcquisitionUrl + "?releasePid=" + releasePid +
//                                                         "&schema=1.0";

    lkey += "|"
//            "Origin=https://web2.magentatv.de"
//            "&Referer=https://web2.magentatv.de"
//            "&User-Agent=Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.0.0.0 Safari/537.36"
            "User-Agent=" + Magenta2Parameters[m_platform].user_agent +
//            "&Authorization=" + personaToken +
            "&Content-Type= "
            "|R{SSM}|";
    kodi::Log(ADDON_LOG_DEBUG, "Licence Key: %s", lkey.c_str());
    properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
    properties.emplace_back("inputstream.adaptive.manifest_headers", "User-Agent=" + Magenta2Parameters[m_platform].user_agent);
    properties.emplace_back("inputstream.adaptive.stream_headers", "User-Agent=" + Magenta2Parameters[m_platform].user_agent);
    //properties.emplace_back("inputstream.adaptive.license_key", lkey);
    std::string urlFirst;
    std::string urlSecond;
    if (lkey.length() > CUTOFF) {
        urlFirst = lkey.substr(0,CUTOFF);
        urlSecond = lkey.substr(CUTOFF,std::string::npos);
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

/*******************************************************************************************************************************************************
*                                                              Channels                                                                                *
*******************************************************************************************************************************************************/
bool CPVRMagenta2::GetChannelNamebyId(const std::string& id, std::string& name)
{
//  kodi::Log(ADDON_LOG_DEBUG, "Looking for station ID: %s", id.c_str());
  for (const auto& thisChannel : m_channels)
  {
    if (thisChannel.stationsId == id)
    {
//      kodi::Log(ADDON_LOG_DEBUG, "Found name %s for id %s:", name.c_str(), id.c_str());
      name = thisChannel.strChannelName;
      return true;
    }
  }
  return false;
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

  int startnum = m_settings->GetStartNum()-1;
  for (const auto& channel : m_channels)
  {

    if ((channel.bRadio == bRadio) && (!m_settings->IsHiddenDeactivated() || !channel.isHidden))
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(static_cast<unsigned int>(channel.iUniqueId));
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(static_cast<unsigned int>(startnum + channel.iChannelNumber));
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
      std::string streamUrl = m_basicUrlSelectorService + m_accountPid + "/media/" + mychannel.mediaPath;
      // + "?format=SMIL&formats=MPEG-DASH&tracking=true";
      kodi::Log(ADDON_LOG_DEBUG, "Stream URL -> %s", streamUrl.c_str());

      return SetStreamProperties(properties, streamUrl, true, false, false);
    }
  }
  return PVR_ERROR_FAILED;
}

/*******************************************************************************************************************************************************
*                                                              EPG                                                                                     *
*******************************************************************************************************************************************************/

bool CPVRMagenta2::GetGenre(int& primaryType, int& secondaryType, const std::string& primaryGenre, const std::string& secondaryGenre)
{
  for (const auto& currentgenre : m_genres)
  {
    if (currentgenre.primaryGenre == primaryGenre)
    {
      primaryType = currentgenre.genreType;
      if (currentgenre.genreSubType != -1)
        secondaryType = currentgenre.genreSubType;
      else if (currentgenre.secondaryGenres.size() > 0)
      {
        secondaryType = -1;
        for (int i=0; i < currentgenre.secondaryGenres.size(); i++)
        {
          std::vector<std::string> out;
          tokenize2(secondaryGenre, EPG_STRING_TOKEN_SEPARATOR, out);
          for (int j=0; j < out.size(); j++)
          {
            if (currentgenre.secondaryGenres[i].secondaryGenre == out[j] && secondaryType == -1)
              secondaryType = currentgenre.secondaryGenres[i].secondaryGenreType;
          }
        }
        if (secondaryType == -1)
          secondaryType = 0;
      } else
        secondaryType = 0;
      kodi::Log(ADDON_LOG_DEBUG, "Returning genre %i and subgenre %i for primary %s and secondary %s", primaryType, secondaryType, primaryGenre.c_str(), secondaryGenre.c_str());
      return true;
    }
  }
  kodi::Log(ADDON_LOG_DEBUG, "Not found primary %s and secondary %s", primaryGenre.c_str(), secondaryGenre.c_str());
  return false;
}

void CPVRMagenta2::SetGenreTypes(const rapidjson::Value& item, std::string& primary, std::string& secondary)
{
  primary = "";
  secondary = "";
  if (item.HasMember("tags"))
  {
    const rapidjson::Value& tags = item["tags"];
    for (rapidjson::SizeType i = 0; i < tags.Size(); i++)
    {
      std::string scheme = Utils::JsonStringOrEmpty(tags[i], "scheme");
      std::string title = Utils::JsonStringOrEmpty(tags[i], "title");
      if (scheme == "genre-primary")
      {
        primary += title;
        primary += EPG_STRING_TOKEN_SEPARATOR;
      } else if (scheme == "genre-secondary")
      {
        secondary += title;
        secondary += EPG_STRING_TOKEN_SEPARATOR;
      } else if (scheme == "category")
      {
        //Todo for later
      } else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Unknown scheme type: %s with title: %s", scheme.c_str(), title.c_str());
      }
    }
    if (!primary.empty())
      primary.erase(primary.end() - 1);
    if (!secondary.empty())
      secondary.erase(secondary.end() - 1);
  }
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
      if (!thumbnailsItem.IsNull())
      {
        int width = Utils::JsonIntOrZero(thumbnailsItem, "width");
        int height = Utils::JsonIntOrZero(thumbnailsItem, "height");
        if ((width == 0) && (height == 0))
          tag.SetIconPath(Utils::JsonStringOrEmpty(thumbnailsItem, "url"));
        else
          tag.SetIconPath(GetNgissUrl(Utils::JsonStringOrEmpty(thumbnailsItem, "url"), width, height));
      }
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
  SetGenreTypes(epgItem, genre_primary, genre_secondary);
  int primaryType;
  int secondaryType;
  if (GetGenre(primaryType, secondaryType, genre_primary, genre_secondary))
  {
    tag.SetGenreType(primaryType);
    tag.SetGenreSubType(secondaryType);
  } else
  {
    kodi::Log(ADDON_LOG_DEBUG, "Primary Genres: %s", genre_primary.c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Secondary Genres: %s", genre_secondary.c_str());
    tag.SetGenreType(EPG_GENRE_USE_STRING);
    tag.SetGenreDescription(genre_secondary);
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

PVR_ERROR CPVRMagenta2::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  bIsPlayable = false;

  std::stringstream ss;
  ss<< std::hex << tag.GetUniqueBroadcastId(); // int decimal_value
  std::string epgId ( ss.str() );

  while (epgId.size() < 8)
    epgId.insert(0, "0");

  kodi::Log(ADDON_LOG_DEBUG, "Checking if EPGTag with UID: %s is playable", epgId.c_str());

  std::string programsUrl = m_allProgramsFeedUrl + "?form=cjson" +
                                                   "&byGuid=" + "telekom.de-" + epgId +
                                                   "&range=1-1" +
                                                   "&fields=media.publicUrl,media.availableDate," +
                                                   "media.expirationDate"; //programType

  rapidjson::Document doc;
  if (!GetPostJson(programsUrl, "", doc)) {
    return PVR_ERROR_NO_ERROR;
  }

  if (!doc.HasMember("entries"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get Programs feed");
    return PVR_ERROR_NO_ERROR;
  }

  const rapidjson::Value& entries = doc["entries"];

  if (entries.Size() != 1)
    return PVR_ERROR_NO_ERROR;

  if (!entries[0].HasMember("media"))
    return PVR_ERROR_NO_ERROR;

  const rapidjson::Value& media = entries[0]["media"];

  if (media.Size() == 0)
    return PVR_ERROR_NO_ERROR;

  if (!media[0].HasMember("publicUrl") || !media[0].HasMember("availableDate") || !media[0].HasMember("expirationDate"))
    return PVR_ERROR_NO_ERROR;

  time_t availableDate = (time_t) (Utils::JsonInt64OrZero(media[0], "availableDate") / 1000);
  time_t expirationDate = (time_t) (Utils::JsonInt64OrZero(media[0], "expirationDate") / 1000);
  std::string publicUrl = Utils::JsonStringOrEmpty(media[0], "publicUrl");
  auto current_time = time(NULL);

  if (current_time > availableDate && current_time < expirationDate && !publicUrl.empty())
  {
    kodi::Log(ADDON_LOG_DEBUG, "Found public URL: %s", publicUrl.c_str());
    bIsPlayable = true;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::stringstream ss;
  ss<< std::hex << tag.GetUniqueBroadcastId(); // int decimal_value
  std::string epgId ( ss.str() );

  while (epgId.size() < 8)
    epgId.insert(0, "0");

  kodi::Log(ADDON_LOG_DEBUG, "Get stream parameters for %s", epgId.c_str());

  std::string programsUrl = m_allProgramsFeedUrl + "?form=cjson" +
                                                   "&byGuid=" + "telekom.de-" + epgId +
                                                   "&range=1-1" +
                                                   "&fields=media.publicUrl"; //programType

  rapidjson::Document doc;
  if (!GetPostJson(programsUrl, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("entries"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get Programs feed");
    return PVR_ERROR_FAILED;
  }

  const rapidjson::Value& entries = doc["entries"];

  if (entries.Size() != 1)
    return PVR_ERROR_FAILED;

  if (!entries[0].HasMember("media"))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& media = entries[0]["media"];

  if (media.Size() == 0)
    return PVR_ERROR_FAILED;

  if (!media[0].HasMember("publicUrl"))
    return PVR_ERROR_FAILED;

  std::string publicUrl = Utils::JsonStringOrEmpty(media[0], "publicUrl");
  if (!publicUrl.empty())
  {
    kodi::Log(ADDON_LOG_DEBUG, "Timeshift URL: %s", publicUrl.c_str());
    return SetStreamProperties(properties, publicUrl, false, true, false);
  }

  return PVR_ERROR_FAILED;
}

/*******************************************************************************************************************************************************
*                                                              Groups                                                                                  *
*******************************************************************************************************************************************************/

PVR_ERROR CPVRMagenta2::GetChannelGroupsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  amount = static_cast<int>(m_categories.size());
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Groups Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& category : m_categories)
  {
    kodi::addon::PVRChannelGroup kodiGroup;

    if (!bRadio && category.level == 0 && !category.channelUids.empty())
    {
      kodiGroup.SetPosition(category.order);
      kodiGroup.SetIsRadio(false); // is no radio group
      kodiGroup.SetGroupName(category.description);

      results.Add(kodiGroup);
      kodi::Log(ADDON_LOG_DEBUG, "Group added: %s at position %u level %u", category.description.c_str(), category.order, category.level);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                           kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& cgroup : m_categories)
  {
    if (cgroup.description != group.GetGroupName())
      continue;

    unsigned int position = 0;
    for (const int& channelid : cgroup.channelUids)
    {
      kodi::addon::PVRChannelGroupMember kodiGroupMember;

      kodiGroupMember.SetGroupName(cgroup.description);
      kodiGroupMember.SetChannelUniqueId(static_cast<unsigned int>(channelid));
      kodiGroupMember.SetChannelNumber(static_cast<unsigned int>(++position));

      results.Add(kodiGroupMember);
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

/*******************************************************************************************************************************************************
*                                                              Recordings                                                                              *
*******************************************************************************************************************************************************/

int CPVRMagenta2::CountTimersRecordings(const bool& isRecording)
{
  std::string url = m_pvrBaseUrl + "/get-recordings?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return 0;
  }

  if (!doc.HasMember("recordings"))
    return 0;

  int count = 0;
  const rapidjson::Value& recordings = doc["recordings"];

  for (rapidjson::SizeType i = 0; i < recordings.Size(); i++)
  {
    std::string recordingStatus = Utils::JsonStringOrEmpty(recordings[i], "recordingStatus");
    if (isRecording)
    {
      if (recordingStatus == "RECORDING" || recordingStatus == "GENERATED")
        count++;
    } else
    {
      if (recordingStatus == "SCHEDULED")
        count++;
    }
  }
  return count;
}

PVR_ERROR CPVRMagenta2::GetRecordingsAmount(bool deleted, int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  amount = static_cast<int>(CountTimersRecordings(true));
//  amount += GetGroupRecordingsAmount();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Recordings Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

void CPVRMagenta2::FillPVRRecording(const rapidjson::Value& recordingItem, kodi::addon::PVRRecording& kodiRecording)
{
  if (!recordingItem.HasMember("program") || !recordingItem.HasMember("listing"))
    return;
  std::string recordingStatus = Utils::JsonStringOrEmpty(recordingItem, "recordingStatus");
  if (recordingStatus == "RECORDING" || recordingStatus == "GENERATED")
  {
    const rapidjson::Value& program = recordingItem["program"];
    const rapidjson::Value& listing = recordingItem["listing"];
    kodiRecording.SetRecordingId(Utils::JsonStringOrEmpty(recordingItem, "id"));
    kodiRecording.SetTitle(Utils::JsonStringOrEmpty(program, "title"));
    kodiRecording.SetYear(Utils::JsonIntOrZero(program, "year"));
    kodiRecording.SetPlot(Utils::JsonStringOrEmpty(program, "description"));
    kodiRecording.SetPlotOutline(Utils::JsonStringOrEmpty(program, "shortDescription"));
//    kodiRecording.SetChannelName();
    kodiRecording.SetDuration(static_cast<int>(Utils::JsonDoubleOrZero(program, "runtime")));
    time_t expirationDateTime = Utils::StringToTime2(Utils::JsonStringOrEmpty(recordingItem, "expirationDateTime"));

    kodiRecording.SetLifetime(static_cast<int>((expirationDateTime - time(NULL))/(60*60*24)));
//    kodi::Log(ADDON_LOG_DEBUG, "Lifetime: %i", kodiRecording.GetLifetime());
//    kodiRecording.SetEPGUid();
    kodiRecording.SetRecordingTime(Utils::StringToTime2(Utils::JsonStringOrEmpty(recordingItem,"startDateTime")));
    kodiRecording.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);

    std::string channelName;
    if (GetChannelNamebyId(Utils::JsonStringOrEmpty(listing, "stationId"), channelName))
      kodiRecording.SetChannelName(channelName);

    std::string genre_primary = "";
    std::string genre_secondary = "";
    SetGenreTypes(program, genre_primary, genre_secondary);
    int primaryType;
    int secondaryType;
    if (GetGenre(primaryType, secondaryType, genre_primary, genre_secondary))
    {
      kodiRecording.SetGenreType(primaryType);
      kodiRecording.SetGenreSubType(secondaryType);
    } else
    {
      kodi::Log(ADDON_LOG_DEBUG, "Primary Genres: %s", genre_primary.c_str());
      kodi::Log(ADDON_LOG_DEBUG, "Secondary Genres: %s", genre_secondary.c_str());
      kodiRecording.SetGenreType(EPG_GENRE_USE_STRING);
      kodiRecording.SetGenreDescription(genre_secondary);
    }

    if (program.HasMember("thumbnails"))
    {
      const rapidjson::Value& thumbnails = program["thumbnails"];
      rapidjson::Value::ConstMemberIterator itr = thumbnails.MemberBegin();
      ++itr;
      if (itr != thumbnails.MemberEnd())
      {
        const rapidjson::Value& thumbnailsItem = (itr->value);
        if (!thumbnailsItem.IsNull())
        {
          int width = Utils::JsonIntOrZero(thumbnailsItem, "width");
          int height = Utils::JsonIntOrZero(thumbnailsItem, "height");
          std::string iconUrl = Utils::JsonStringOrEmpty(thumbnailsItem, "url");
          if ((width == 0) && (height == 0))
          {
            kodiRecording.SetIconPath(iconUrl);
            kodiRecording.SetFanartPath(iconUrl);
            kodiRecording.SetThumbnailPath(iconUrl);
          }
          else
          {
            kodiRecording.SetIconPath(GetNgissUrl(iconUrl, width, height));
            kodiRecording.SetFanartPath(GetNgissUrl(iconUrl, width, height));
            kodiRecording.SetThumbnailPath(GetNgissUrl(iconUrl, width, height));
          }
        }
      }
    }

  }
}

PVR_ERROR CPVRMagenta2::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_pvrBaseUrl + "/get-recordings?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("recordings"))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& recordings = doc["recordings"];

  for (rapidjson::SizeType i = 0; i < recordings.Size(); i++)
  {
    kodi::addon::PVRRecording kodiRecording;

    FillPVRRecording(recordings[i], kodiRecording);
//    if (!current_recording.periodPVRTaskName.empty())
//      kodiRecording.SetDirectory(current_recording.periodPVRTaskName);

    results.Add(kodiRecording);
    kodi::Log(ADDON_LOG_DEBUG, "Recording added: %s", kodiRecording.GetTitle().c_str());
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_pvrBaseUrl + "/get-recordings?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("recordings"))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& recordings = doc["recordings"];

  for (rapidjson::SizeType i = 0; i < recordings.Size(); i++)
  {
    if (recording.GetRecordingId() != Utils::JsonStringOrEmpty(recordings[i], "id"))
      continue;

    std::string playUrl = Utils::JsonStringOrEmpty(recordings[i], "playbackUrl");
    kodi::Log(ADDON_LOG_DEBUG, "[PLAY RECORDING] url: %s", playUrl.c_str());

    SetStreamProperties(properties, playUrl, false, false, false);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_pvrBaseUrl + "/get-npvr-info";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  int quotaAllocated = Utils::JsonIntOrZero(doc, "quotaAllocated");
  int quotaUsed = Utils::JsonIntOrZero(doc, "quotaUsed");

  total = quotaAllocated * KBM2; //convert hours to MB
  used = quotaUsed * KBM2; //convert hours to MB
  kodi::Log(ADDON_LOG_DEBUG, "Reported %llu/%llu used/total", used, total);
  return PVR_ERROR_NO_ERROR;
}
