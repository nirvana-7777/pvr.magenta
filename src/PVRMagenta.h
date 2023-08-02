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
#include "rapidjson/document.h"

#define TIMER_ONCE_EPG (PVR_TIMER_TYPE_NONE + 1)

static const std::string GUEST_URL = "https://slbedmfk11100.prod.sngtv.t-online.de:33428/";
static const std::string CLIENT_ID = "10LIVESAM30000004901NGTVANDROIDTV0000000";
static const std::string psk_id1 = "TkdUVjAwMDAwMQ==";
static const std::string psk_id2 = "QjRENUI3Q0M0RDhEOTFCRTVDRkQ1NjhFQ0VCQ0ZDMDk1RUVDN0U5RTFBRDYwNzYyODJBMDUzNjVGNkUxNkMyQw==";

struct MagentaGenre
{
  int genreId;
  int genreType;
  std::string genreName;
};

struct MagentaRecording
{
  int index;
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
};

struct MagentaCategory
{
  int position;
  std::string name;
  long id;
  bool isRadio;
  std::vector<int> channels;
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
  int iUniqueId;
//  int mediaId;
//  int pvrMediaId;
  int iChannelNumber; //position
  std::vector<PhysicalChannel> physicalChannels;
  std::string strChannelName;
  std::string strIconPath;
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

  PVR_ERROR CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                            const kodi::addon::PVREPGTag& item) override;
  PVR_ERROR CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                const kodi::addon::PVRChannel& item) override;
  PVR_ERROR CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                              const kodi::addon::PVRTimer& item) override;
  PVR_ERROR CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                  const kodi::addon::PVRRecording& item) override;
  PVR_ERROR CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook) override;

  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable) override;
  PVR_ERROR GetEPGTagStreamProperties(
      const kodi::addon::PVREPGTag& tag,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;
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
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;
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
  bool GetChannel(const kodi::addon::PVRChannel& channel, MagentaChannel& myChannel);

private:
  PVR_ERROR CallMenuHook(const kodi::addon::PVRMenuhook& menuhook);

  void SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                           const std::string& url,
                           bool realtime, bool playTimeshiftBuffer, bool epgplayback);

  std::vector<MagentaChannel> m_channels;
  std::vector<MagentaCategory> m_categories;
  std::vector<MagentaRecording> m_recordings;
  std::vector<MagentaRecording> m_timers;
  std::vector<MagentaGenre> m_genres;

  HttpClient *m_httpClient;
  CSettings* m_settings;

  bool JsonRequest(const std::string& url, const std::string& postData, rapidjson::Document& doc);
  bool is_better_resolution(const int alternative, const int current);
  bool is_pvr_allowed(const rapidjson::Value& current_item);
  int SelectMediaId(const MagentaChannel channel, const bool npvr);
  std::string GetPlayUrl(const MagentaChannel channel, const int mediaId);
  bool HasStreamingUrl(const MagentaChannel channel);
  bool MagentaGuestLogin();
  bool MagentaDTAuthenticate();
  bool MagentaAuthenticate();
  bool AddGroupChannel(const long groupid, const int channelid);
  bool ReleaseCurrentMedia();
  bool GetCategories();
  bool GetTimersRecordings(const bool isRecording);
  bool GetTimers();
  bool GetGenreIds();
  bool LoadChannels();

  std::string m_licence_url;
  std::string m_ca_device_id;
  std::string m_epg_https_url;
  std::string m_sam_service_url;
  std::string m_cnonce;
  std::string m_userContentListFilter;
  std::string m_userContentFilter;
  std::string m_encryptToken;
  std::string m_userID;
  std::string m_sessionID;
  std::string m_session_key;
  int m_currentChannelId;
  int m_currentMediaId;
};
