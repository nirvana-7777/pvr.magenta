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
#include "PVRMagenta2.h"
#include "rapidjson/document.h"

#define TIMER_ONCE_EPG (PVR_TIMER_TYPE_NONE + 1)
#define TIMER_SERIES_EPG (PVR_TIMER_TYPE_NONE + 2)
static const int IPTV_STB = 0;
static const int PC = 1;
static const int OTT = 2;
static const int MOBILE = 3;
static const int OTT_STB = 5;
static const int HYBRID_STB = 17;
static const long KBM = 150000; // 150 MB
static const uint64_t TIMEBUFFER = 4 * 60 * 60; //4h time buffer
static const std::string GUEST_URL      = "https://slbedmfk11100.prod.sngtv.t-online.de:33428/";
static const std::string SUBNETID = "4901";
static const std::string AREAID = "1";
static const std::string TEMPLATENAME = "NGTV";
static const std::string TIMEZONE = "Europe/Berlin";
static const std::string DEVICENAME = "Kodi PVR";
static const std::string EPGDIR = "/EPG/JSON/";
static const std::string DEFAULT_CATEGORY_ID = "2000000142";
static const int MAGENTA_BOOKMARK_RECORDING = 2;
static const int MAGENTA_CAST_ACTOR = 0;
static const int MAGENTA_CAST_DIRECTOR = 1;
static const int MAGENTA_CAST_PRODUCER = 4;
static const int MAGENTA_CAST_WRITER = 5;
static const int MAGENTA_CAST_MODERATOR = 7;
static const int MAGENTA_RECORDING_TYPE_RECEIVER = 1;
static const int MAGENTA_RECORDING_TYPE_CLOUD = 2;
static const int MAGENTA_RECORDING_DELETE_MODE_WHEN_FULL = 0;
static const int MAGENTA_RECORDING_DELETE_MODE_MANUAL = 1;
static const int MAGENTA_RECORDING_DELETE_MODE_KEEP_FIVE = 2;
static const int MAGENTA_RECORDING_DELETE_MODE_KEEP_TEN = 3;
static const int MAGENTA_TIMER_TIMEMODE_ANY = 0;
static const int MAGENTA_TIMER_TIMEMODE_START = 2;

/*
urls: {
    authenticate: "/EPG/JSON/Authenticate",
    bcAuthStart: "/bc-auth/start",
    categoryList: "/EPG/JSON/CategoryList",
    channelInfo: "/EPG/JSON/AllChannelDynamic",
    channelList: "/EPG/JSON/AllChannel",
    dfcc: "/EPG/JSON/QueryDFCC",
    dtAuthenticate: "/EPG/JSON/DTAuthenticate",
    getCustomChannelNumbers: "/EPG/JSON/GetCustomChanNo",
    getDataVersion: "/EPG/JSON/GetDataVersion",
    getDeviceList: "/EPG/JSON/GetDeviceList",
    getFavorite: "/EPG/JSON/GetFavorite",
    getGenreList: "/EPG/JSON/GetGenreList",
    getUserSettingValue: "/EPG/JSON/GetUserSettingValue",
    heartbit: "/EPG/JSON/HeartBit",
    isoCodeTable: "/EPG/JSON/GetISOCodeTable",
    login: "/JSON/Login",
    logout: "/EPG/JSON/Logout",
    modifyDeviceName: "/EPG/JSON/ModifyDeviceName",
    program: {
        details: "/EPG/JSON/ContentDetail",
        reruns: "/EPG/JSON/QueryPlaybillByFilter",
        vodRecos: "/EPG/recommendations/ngtv/default/i2i-vod",
        liveRecos: "/EPG/recommendations/ngtv/default/i2i-tv"
    },
    programs: "/EPG/JSON/PlayBillList",
    pvr: {
        addBookmark: "/EPG/JSON/AddBookmark",
        addPvr: "/EPG/JSON/AddPVR",
        authorizeAndPlay: "/EPG/JSON/AuthorizeAndPlay",
        deleteBookmark: "/EPG/JSON/DeleteBookmark",
        deletePvr: "/EPG/JSON/DeletePVR",
        getAll: "/EPG/JSON/QueryPVR",
        queryPvrById: "/EPG/JSON/QueryPVRById",
        queryBookmark: "/EPG/JSON/QueryBookmark",
        queryPVRSpace: "/EPG/JSON/QueryPVRSpace",
        periodPvrMgmt: "/EPG/JSON/PeriodPVRMgmt",
        updatePvr: "/EPG/JSON/UpdatePVR",
        updatePvrList: "/EPG/JSON/UpdatePVRList"
    },
    replaceDevice: "/EPG/JSON/ReplaceDevice",
    search: "/EPG/search/ngtv/select",
    setCustomChannelNumber: "/EPG/JSON/SetCustomChanNo",
    setTdsFlags: "/EPG/JSON/TDSSet",
    setUserSettingValue: "/EPG/JSON/SetUserSettingValue",
    streamManagement: {
        dcp: {
            streams: "/streams",
            terminals: "/terminals"
        },
        huawei: {
            acquireStream: "/EPG/JSON/Play",
            heartbit: "/EPG/JSON/PlayHeartbit",
            releaseStream: "/EPG/JSON/ReleasePlaySession"
        }
    },
    token: "/tokens",
    updateFavorite: "/EPG/JSON/FavoriteManagement",
    userSettings: "/EPG/JSON/QuerySubscriberEx"
},
*/
struct MagentaParameter
{
  std::string clientId;
  std::string pskName;
  std::string pskValue;
  std::string terminaltype;
  std::string terminalvendor;
  std::string hwSupplier;
  std::string deviceClass;
  std::string softwareVersion;
  std::string osVersion;
};

MagentaParameter MagentaParameters[3] = {{
                                          "10LIVESAM30000004901NGTVANDROIDTV0000000",
                                          "NGTV000001",
                                          "B4D5B7CC4D8D91BE5CFD568ECEBCFC095EEC7E9E1AD6076282A05365F6E16C2C",
                                          "TV_AndroidTV",
                                          "SHIELD Android TV",
                                          "AndroidTV SHIELD Android TV",
                                          "TV",
                                          "11",
                                          "7825230_3167.5736"
                                         },
                                         {
                                          "10LIVESAM30000004901NGTVSTICK00000000000",
                                          "MS03P00002",
                                          "2959F9E62ED1000FD4539AB5F38E9F6BCCBCB58480F459DC976A8DA07694C569",
                                          "ATV_STICK",
                                          "NVIDIA",
                                          "SHIELD Android TV",
                                          "Android",
                                          "1.67.1",
                                          "11"
                                        },
                                         {
                                          "10LIVESAM30000004901NGTVMAGENTA000000000",
                                          "NGTV000001",
                                          "B4D5B7CC4D8D91BE5CFD568ECEBCFC095EEC7E9E1AD6076282A05365F6E16C2C",
                                          "WEBTV",
                                          "Unknown",
                                          "WEB-MTV",
                                          "TV",
                                          "1.63.2",
                                          "Windows 10"
                                         }
                                        };

struct MagentaDevice
{
  std::string deviceName;
  std::string deviceId;
  int deviceType;
  bool isonline;
  std::string physicalDeviceId;
  std::string lastOfflineTime;
  std::string terminalType;
  bool isSupportPVR;
  std::string channelNamespace;
  std::string channelNamespaceName;
  int status;
};

struct KodiGenre
{
  int genreType;
  int genreSubType;
};

struct MagentaGenre
{
  int genreId;
  int genreType;
  std::string genreName;
  KodiGenre kodiGenre;
};

struct MagentaRecording
{
  unsigned int index;
  std::string pvrId;
  int channelId;
	int mediaId;
  std::string introduce;
  std::string beginTime;
  std::string endTime;
  int beginOffset;
  int endOffset;
  std::string pvrName;
  std::string channelName;
  int realRecordLength;
  std::string picture;
  int bookmarkTime;
  std::vector<int> genres;
  int deleteMode;
  int seriesId;
  bool isWatched;
  int ratingId;
  std::string programId;
  std::string periodPVRTaskName;
  unsigned int groupIndex;
};

struct MagentaRecordingGroup
{
  unsigned int index;
  int seriesType;
  int seriesId;
  int mediaId;
  int channelId;
  std::string periodPVRTaskId;
  std::string periodPVRTaskName;
  int beginOffset;
  int endOffset;
  int type;
  int deleteMode;
  int latestSeriesNum;
  int timeMode;
  std::string selectedBeginTime;
  std::string channelName;
  std::vector<MagentaRecording> groupRecordings;
};

struct MagentaCategory
{
  unsigned int position;
  std::string name;
  long id;
  bool isRadio;
  std::vector<unsigned int> channelids;
};

struct PhysicalChannel
{
  int mediaId;
  std::string externalCode;
  int fileFormat;
  int definition;
  std::string playUrl;
  bool btvenabled;
  bool pltvenabled;
  bool npvrenabled;
};

struct MagentaChannel
{

  bool bRadio;
//  bool bArchive;
  unsigned int iUniqueId;
//  int mediaId;
//  int pvrMediaId;
  unsigned int iChannelNumber; //position
  std::vector<PhysicalChannel> physicalChannels;
  std::string strChannelName;
  std::string strIconPath;
  bool isHidden;
//  std::string strStreamURL;
//  std::vector<long> categories;
};

class ATTR_DLL_LOCAL CPVRMagenta : public kodi::addon::CAddonBase,
                                public kodi::addon::CInstancePVRClient
{
public:
  CPVRMagenta();
  ~CPVRMagenta() override;

  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;
  PVR_ERROR GetBackendHostname(std::string& hostname) override;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;
  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable) override;
  PVR_ERROR GetEPGTagStreamProperties(
      const kodi::addon::PVREPGTag& tag,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetEPGTagEdl(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVREDLEntry>& edl) override;
  PVR_ERROR GetProvidersAmount(int& amount) override;
  PVR_ERROR GetProviders(kodi::addon::PVRProvidersResultSet& results) override;
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                   kodi::addon::PVRChannelGroupMembersResultSet& results) override;
  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeletePVR(const std::string pvrId, const bool isRecording);
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording,
                                         int lastplayedposition) override;
  PVR_ERROR GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position) override;
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;
  //PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetRecordingStreamProperties(
      const kodi::addon::PVRRecording& recording,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;

  ADDON_STATUS SetSetting(const std::string& settingName,
                        const std::string& settingValue);
/*
  bool SeekTime(double time, bool backward, double& startpts) override;
  bool CanPauseStream() override { return true; }
  bool CanSeekStream() override { return true; }
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override;
  int64_t SeekLiveStream(int64_t position, int whence) override;
*/
protected:
  std::string GetRecordingURL(const kodi::addon::PVRRecording& recording);
  bool GetChannel(const int& channelUid, MagentaChannel& myChannel);

private:
//  PVR_ERROR CallMenuHook(const kodi::addon::PVRMenuhook& menuhook);

  void SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                           const std::string& url,
                           bool realtime, bool playTimeshiftBuffer, bool epgplayback);

  std::vector<MagentaChannel> m_channels;
  std::vector<MagentaCategory> m_categories;
  std::vector<MagentaRecording> m_recordings;
  std::vector<MagentaRecording> m_timers;
  std::vector<MagentaRecordingGroup> m_timerGroups;
  std::vector<MagentaRecordingGroup> m_recGroups;
  std::vector<MagentaGenre> m_genres;
  std::vector<MagentaDevice> m_devices;

  HttpClient *m_httpClient;
  CSettings* m_settings;
  CPVRMagenta2* m_magenta2;

  bool JsonRequest(const std::string& url, const std::string& postData, rapidjson::Document& doc);
  std::string PrepareTime(const std::string& current);
  bool is_better_resolution(const int alternative, const int current);
  bool is_pvr_allowed(const rapidjson::Value& current_item);
  int SelectMediaId(const MagentaChannel channel, const bool npvr);
  std::string GetPlayUrl(const MagentaChannel channel, const int mediaId);
  std::string GetPlay(const int& chanId, const int& mediaId, const bool isTimeshift);
  bool HasStreamingUrl(const MagentaChannel channel);
  bool GetEPGDetails(std::string& contentCode, rapidjson::Document& epgDoc);
  bool FillEPGTag(const rapidjson::Value& epgItem, kodi::addon::PVREPGTag& tag);
  void FillEPGDetails(const rapidjson::Value& epgItem, kodi::addon::PVREPGTag& tag);
  bool GetEPGPlaybill(const int& channelId, const time_t& start, const time_t& end, rapidjson::Document& epgDoc);
  bool UpdateEPGEvent(const kodi::addon::PVREPGTag& oldtag);
  void GetLifetimeValues(std::vector<kodi::addon::PVRTypeIntValue>& lifetimeValues, const bool& isSeries) const;
  void GenerateCNonce();
  bool GuestLogin();
  bool GuestAuthenticate();
  bool MagentaDTAuthenticate();
  bool MagentaAuthenticate();
  bool AddGroupChannel(const long groupid, const unsigned int channelid);
  bool ReleaseCurrentMedia();
  bool GetCategories();
  int GetGenreIdFromName(const std::string& genreName);
  std::string GetGenreFromId(const int& genreId);
  KodiGenre GetKodiGenreFromId(const int& genreId);
  void FillRecording(const rapidjson::Value& recordingItem, MagentaRecording& magenta_recording, const int& index);
  void FillPVRRecording(kodi::addon::PVRRecording& kodiRecording, const MagentaRecording& rec);
  bool UpdateBookmarks();
  bool GetTimersRecordings(const bool isRecording);
  bool GetTimers();
  int GetGroupTimersAmount();
  int GetGroupRecordingsAmount();
  std::string GetPeriodPVRPayload(const MagentaChannel& channel, const kodi::addon::PVRTimer& timer, const std::string& periodPVRTaskId, const bool& isUpdate);
  bool GetGenreIds();
  bool GetMyGenres();
  bool GetDeviceList();
  bool PlaceDevice();
  bool ReplaceOldestDevice();
  bool ReplaceDevice(const std::string& orgDeviceId);
  bool ModifyDeviceName(const std::string& deviceId);
  bool IsDeviceInList();
  bool LoadChannels();
  uint64_t GetPVRSpace(const int type);

  std::string m_licence_url;
  std::string m_ca_device_id;
  std::string m_device_id;
  std::string m_epg_https_url;
  std::string m_sam_service_url;
  std::string m_cnonce;
  std::string m_userContentListFilter;
  std::string m_userContentFilter;
  std::string m_encryptToken;
  std::string m_userID;
  std::string m_userGroup;
  std::string m_sessionID;
  std::string m_session_key;
  std::string m_ChannelCategoryID;
  int m_currentChannelId;
  int m_currentMediaId;
  int m_params;
  bool m_isMagenta2;
};
