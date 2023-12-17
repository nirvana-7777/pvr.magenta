/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */
#include <string>
#include <vector>

#include <kodi/addon-instance/PVR.h>
#include "Settings.h"
#include "http/HttpClient.h"
#include "sam3/Sam3Client.h"
#include "rapidjson/document.h"
#include <tinyxml2.h>

static const std::string BOOTSTRAP_URL = "https://prod.dcm.telekom-dienste.de/v1/settings/{configGroupId}/bootstrap?";
static const std::string WINDOWS_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
//static const std::string SSO_URL = "https://ssom.magentatv.de/authenticate";
//static const std::string CONFIG_GROUP_ID = "web-mtv";
static const std::string CONFIG_GROUP_ID = "atv-androidtv";
static const std::string DEVICEMODEL = "DT:ATV-AndroidTV";
static const std::string APPNAME = "MagentaTV";
static const std::string APPVERSION = "104180";
static const std::string FIRMWARE = "API level 30";
static const std::string RUNTIMEVERSION = "1";

static const int MAX_CHANNEL_ENTRIES = 100;

static const std::vector<std::string> Magenta2StationThumbnailTypes
                  = { "stationBackground", "stationBarker", "stationLogo", "stationLogoColored" };
/*
static const int FEED_ALL_CHANNELS = 0;
static const int FEED_ENTITLED_CHANNELS = 1;
static const int FEED_CHANNEL_SCHEDULE = 2;
*/

struct Magenta2SubGenre
{
  std::string secondaryGenre;
  int secondaryGenreType;
};

struct Magenta2Genre
{
  std::string primaryGenre;
  int genreType;
  std::vector<Magenta2SubGenre> secondaryGenres;
  int genreSubType;
};

struct Magenta2Lock
{
  std::string instance;
  std::string token;
  std::string id;
  std::string lock;
  std::string sequenceToken;
  std::string serviceUrl;
  int updateInterval;
};

struct Magenta2KV
{
  std::string key;
  std::string value;
};

struct Magenta2STS
{
  std::string authorizeTokensUrl;
  std::string deviceToken;
};

struct Magenta2AuthMethods
{
  bool password;
  bool code;
  bool line;
};

struct Magenta2Ngiss
{
  std::string basicUrl;
  std::string callParameter;
};

struct Magenta2Picture
{
  std::string title;
  std::string url;
  int width;
  int height;
};

struct Magenta2Channel
{
  bool bRadio;
  std::string id;
  std::string stationsId;
  int iUniqueId;
  int iChannelNumber; //position
  std::string title;
  std::string strChannelName;
  std::string strIconPath;
  std::string mediaPath;
  std::vector<std::string> distributionRights;
  bool isHidden;
  bool isHd;
  std::vector<Magenta2Picture> thumbnails;
//  bool isEntitled;
};

class CPVRMagenta2
{
public:
  CPVRMagenta2(CSettings* settings, HttpClient* httpclient);
  ~CPVRMagenta2();

  typedef void (CPVRMagenta2::*handleentry_t)(const rapidjson::Value& entry);

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities);
  PVR_ERROR GetChannelsAmount(int& amount);
  PVR_ERROR GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results);
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      std::vector<kodi::addon::PVRStreamProperty>& properties);
  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results);

private:
  PVR_ERROR SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                const std::string& url,
                                bool realtime, bool playTimeshiftBuffer, bool epgplayback);

  std::vector<Magenta2Channel> m_channels;
  std::vector<std::string> m_distributionRights;
  std::vector<Magenta2KV> m_parameters;
  std::vector<Magenta2Genre> m_genres;

  HttpClient* m_httpClient;
  CSettings* m_settings;
  Sam3Client* m_sam3Client;

  bool XMLGetString(const tinyxml2::XMLNode* pRootNode,
                              const std::string& strTag,
                              std::string& strStringValue);

  bool GetMyGenres();
  bool GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc);
  bool GetSmil(const std::string& url, tinyxml2::XMLDocument& smilDoc);
  bool GetStreamParameters(const std::string& url, std::string& src, std::string& releasePid);
  bool GetParameter(const std::string& key, std::string& value);
  bool Bootstrap();
  bool DeviceManifest();
  bool Manifest();
  bool GetDistributionRights();
  std::string GetNgissUrl(const std::string& url, const int& width, const int& height);
  void AddChannelEntry(const rapidjson::Value& entry);
  void AddEntitlementEntry(const rapidjson::Value& entry);
  bool GetFeed(/*const int& feed,*/ const int& maxEntries, /*const std::string& params,*/ std::string& baseUrl/*, kodi::addon::PVREPGTagsResultSet& results*/,
                handleentry_t HandleEntry);
  bool GetGenre(int& primaryType, int& secondaryType, const std::string& primaryGenre, const std::string& secondaryGenre);
  void AddEPGEntry(const int& channelNumber, const rapidjson::Value& entry, kodi::addon::PVREPGTagsResultSet& results);
  bool GetEPGFeed(const int& channelNumber, const std::string& baseUrl, kodi::addon::PVREPGTagsResultSet& results);
  bool GetChannelByNumber(const unsigned int number, Magenta2Channel& myChannel);
  bool AddDistributionRight(const unsigned int number, const std::string& right);
//  bool IsChannelNumberExist(const unsigned int number);
  bool HideDuplicateChannels();
//  bool SingleSignOn();
  bool ReleaseLock();
  bool GetAuthMethods();

  std::string m_deviceId;
  std::string m_sessionId;
  std::string m_clientModel;
  std::string m_deviceModel;
  std::string m_manifestBaseUrl;
  std::string m_accountBaseUrl;
  std::string m_deviceTokensUrl;
  std::string m_lineAuthUrl;
//  std::string m_sam3ClientId;
  std::string m_entitledChannelsFeed;
  std::string m_allChannelSchedulesFeed;
  std::string m_allChannelStationsFeed;
  std::string m_allListingsFeedUrl;
  std::string m_allProgramsFeedUrl;
  std::string m_liveTvCategoryFeed;
  std::string m_accountPid;
  std::string m_pvrBaseUrl;
  std::string m_basicUrlGetApplicableDistributionRights;
  std::string m_basicUrlSelectorService;
  std::string m_widevineLicenseAcquisitionUrl;
  std::string m_userId;
  std::string m_displayName;
  std::string m_accountId;
  std::string m_locationIdUri;
  Magenta2Lock m_currentLock;
  Magenta2Ngiss m_ngiss;
  Magenta2STS m_sts;
  Magenta2AuthMethods m_authMethods;
};
