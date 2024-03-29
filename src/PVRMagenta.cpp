/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PVRMagenta.h"

#include <algorithm>

#include <kodi/General.h>
#include <kodi/gui/dialogs/OK.h>
#include "Utils.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "sha256.h"
#include "hmac.h"
#include <kodi/Filesystem.h>

/***********************************************************
  * PVR Client AddOn specific public library functions
  ***********************************************************/
static const uint8_t block_size = 16;

void tokenize(std::string const &str, const char* delim,
            std::vector<std::string> &out)
{
    char *token = strtok(const_cast<char*>(str.c_str()), delim);
    while (token != nullptr)
    {
        out.push_back(std::string(token));
        token = strtok(nullptr, delim);
    }
}

std::string string_to_hex(const std::string& input)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    std::string output;
    output.reserve(input.length() * 2);
    for (unsigned char c : input)
    {
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
    }
    return output;
}

std::string GetPictureFromItem(const rapidjson::Value& item)
{
  if (item.HasMember("pictures")) {
    const rapidjson::Value& images = item["pictures"];
    for (rapidjson::Value::ConstValueIterator itr2 = images.Begin();
        itr2 != images.End(); ++itr2)
    {
      const rapidjson::Value& imageItem = (*itr2);

      if (Utils::JsonStringOrEmpty(imageItem, "imageType") == "17") {
        return Utils::JsonStringOrEmpty(imageItem, "href");
      }
    }
  }
  return "";
}

bool CPVRMagenta::is_better_resolution(const int alternative, const int current)
{
  if (m_settings->PreferHigherResolution()) {
    if (alternative > current) {
      return true;
    }
  } else {
    if ((alternative < current) || (current == -1)) {
      return true;
    }
  }
  return false;
}

bool CPVRMagenta::is_pvr_allowed(const rapidjson::Value& current_item)
{
  if (current_item.HasMember("npvrRecCR")) {
    const rapidjson::Value& npvr = current_item["npvrRecCR"];
    if (Utils::JsonStringOrEmpty(npvr, "e") == "1") {
      return true;
    }
  }
  return false;
}

int CPVRMagenta::SelectMediaId(const MagentaChannel channel, const bool npvr)
{

  int currentMediaId = 0;
  int currentDefinition = -1;

  for (int i = 0; i < channel.physicalChannels.size(); i++)
  {
    if (npvr) {
      if ((channel.physicalChannels[i].fileFormat == 2) &&
          (is_better_resolution(channel.physicalChannels[i].definition, currentDefinition)) &&
          (channel.physicalChannels[i].npvrenabled)) {
        currentMediaId = channel.physicalChannels[i].mediaId;
        currentDefinition = channel.physicalChannels[i].definition;
      }
    } else {
      if ((channel.physicalChannels[i].fileFormat == 4) &&
          (is_better_resolution(channel.physicalChannels[i].definition, currentDefinition)) &&
          (channel.physicalChannels[i].btvenabled)) {
        currentMediaId = channel.physicalChannels[i].mediaId;
        currentDefinition = channel.physicalChannels[i].definition;
      }
    }
  }
  return currentMediaId;
}

std::string CPVRMagenta::GetPlayUrl(const MagentaChannel channel, const int mediaId)
{
  for (int i = 0; i < channel.physicalChannels.size(); i++)
  {
    if (channel.physicalChannels[i].mediaId == mediaId) {
      return channel.physicalChannels[i].playUrl;
    }
  }
  return "";
}

bool CPVRMagenta::HasStreamingUrl(const MagentaChannel channel)
{
  for (int i = 0; i < channel.physicalChannels.size(); i++)
  {
    if (!channel.physicalChannels[i].playUrl.empty()) {
        return true;
    }
  }
  return false;
}

std::string CPVRMagenta::PrepareTime(const std::string& current)
{
  std::string prepared = current;
  prepared.insert(4,1,'-');
  prepared.insert(7,1,'-');
  prepared.insert(10,1,' ');
  prepared.insert(13,1,':');
  prepared.insert(16,1,':');
  prepared += " UTC";
  return prepared;
}

void CPVRMagenta::GenerateCNonce()
{
  std::ostringstream convert;
  for (int i = 0; i < block_size; i++) {
      convert << (uint8_t) rand();
  }
  m_cnonce = string_to_hex(convert.str());
  std::transform(m_cnonce.begin(), m_cnonce.end(), m_cnonce.begin(), ::tolower);
  kodi::Log(ADDON_LOG_DEBUG, "Generated cnonce %s", m_cnonce.c_str());
}

bool CPVRMagenta::JsonRequest(const std::string& url, const std::string& postData, rapidjson::Document& doc)
{
  int statusCode = 0;
  std::string result = m_httpClient->HttpPost(url, postData, statusCode);

  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (!doc.HasMember("retcode") || (statusCode != 200)))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed JsonRequest %s with body %s", url.c_str(), postData.c_str());
    return false;
  }
  if (Utils::JsonStringOrEmpty(doc, "retcode") == "-2") {
    kodi::Log(ADDON_LOG_DEBUG, "Retcode returned -2 from %s - need to reauthenticate", url.c_str());
    MagentaAuthenticate();
    result = m_httpClient->HttpPost(url, postData, statusCode);

    doc.Parse(result.c_str());
    if ((doc.GetParseError()) || (!doc.HasMember("retcode")))
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to do JsonRequest");
      return false;
    }
  }
  if (doc.HasMember("retmsg")) {
    kodi::Log(ADDON_LOG_DEBUG, "JSON Request return message: %s", Utils::JsonStringOrEmpty(doc, "retmsg").c_str());
  }
  if (Utils::JsonStringOrEmpty(doc, "retcode") != "0")
  {
    kodi::Log(ADDON_LOG_ERROR, "JsonRequest returned not 0 with url %s, and body %s", url.c_str(), postData.c_str());
    kodi::Log(ADDON_LOG_ERROR, "JsonRequest returned %s", result.c_str());
    return false;
  }
  return true;
}

bool CPVRMagenta::GuestLogin()
{
  std::string jsonString;
  int statusCode = 0;

  std::string url = GUEST_URL + "EDS/JSON/Login?UserID=Guest";

  jsonString = m_httpClient->HttpGet(url, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to do guest login");
    return false;
  }
  //kodi::Log(ADDON_LOG_DEBUG, "Magenta guest login returned: %s", jsonString.c_str());

  if (doc.HasMember("epghttpsurl")) {
    m_epg_https_url = Utils::JsonStringOrEmpty(doc, "epghttpsurl") + EPGDIR;
    kodi::Log(ADDON_LOG_DEBUG, "Setting EPG url to: %s", m_epg_https_url.c_str());
    if (doc.HasMember("sam3Para")) {
      const rapidjson::Value& sam3paras = doc["sam3Para"];
      for (rapidjson::Value::ConstValueIterator itr1 = sam3paras.Begin();
          itr1 != sam3paras.End(); ++itr1)
      {
        const rapidjson::Value& keyvalue = (*itr1);
        std::string key = Utils::JsonStringOrEmpty(keyvalue,"key");
        if (key == "SAM3ServiceURL") {
          m_sam_service_url = Utils::JsonStringOrEmpty(keyvalue,"value");
        }
      }
      if (m_sam_service_url.empty()) {
        kodi::Log(ADDON_LOG_ERROR, "Failed to get SAM3ServiceURL!");
        return false;
      } else {
        kodi::Log(ADDON_LOG_DEBUG, "Setting SAM3ServiceURL to: %s", m_sam_service_url.c_str());
      }
    }
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get EPG URL!");
    return false;
  }
  return true;
}

bool CPVRMagenta::GuestAuthenticate()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_epg_https_url + "Authenticate";
  std::string postData = "{\"cnonce\": \"" + m_cnonce + "\","
	                        "\"areaid\": \"" + AREAID + "\","
	                        "\"mac\": \"" + m_device_id + "\","
	                        "\"preSharedKeyID\": \"" + MagentaParameters[m_params].pskName + "\","
	                        "\"subnetId\": \"" + SUBNETID + "\","
	                        "\"templatename\": \"" + TEMPLATENAME + "\","
	                        "\"terminalid\": \"" + m_device_id + "\","
	                        "\"terminaltype\": \"" + MagentaParameters[m_params].terminaltype + "\","
	                        "\"terminalvendor\": \"" + MagentaParameters[m_params].terminalvendor + "\","
	                        "\"timezone\": \"" + TIMEZONE + "\","
	                        "\"usergroup\": \"-1\","
	                        "\"userType\": 3,"
	                        "\"utcEnable\": 1}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return false;
  }

  m_settings->SetSetting("csrftoken", Utils::JsonStringOrEmpty(doc, "csrfToken"));
  m_userGroup = Utils::JsonStringOrEmpty(doc, "usergroup");
  m_sessionID = Utils::JsonStringOrEmpty(doc, "sessionid");
  m_encryptToken = Utils::JsonStringOrEmpty(doc, "encryptToken");
  m_userContentFilter = Utils::JsonStringOrEmpty(doc, "userContentFilter");
  m_userContentListFilter = Utils::JsonStringOrEmpty(doc, "userContentListFilter");

  return true;
}

bool CPVRMagenta::MagentaDTAuthenticate()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_epg_https_url + "DTAuthenticate";
  std::string postData = "{\"accessToken\": \"" + m_settings->GetMagentaEPGToken() +
                          "\", \"usergroup\": \"" + m_userGroup +
                          "\", \"connectType\": 1" +
	                        ", \"userType\": \"1\"" +
                          ", \"terminalid\": \"" + m_device_id +
	                        "\", \"mac\": \"" + m_device_id +
	                        "\", \"terminaltype\": \"" + MagentaParameters[m_params].terminaltype +
                          "\", \"utcEnable\": 1" +
                          ", \"timezone\": \"" + TIMEZONE +
                          "\", \"caDeviceInfo\": [{" +
		                           "\"caDeviceType\": 6" +
		                           ", \"caDeviceId\": \"" + m_device_id + "\"}]," +
	                        "\"terminalDetail\": [{" +
		                          "\"key\": \"GUID\"," +
		                          "\"value\": \"" + m_device_id + "\"}, {" +
                              "\"key\": \"HardwareSupplier\"," +
                              "\"value\": \"" + MagentaParameters[m_params].hwSupplier + "\"}, {" +
                              "\"key\": \"DeviceClass\"," +
                              "\"value\": \"" + MagentaParameters[m_params].deviceClass + "\"}, {" +
                              "\"key\": \"DeviceStorage\"," +
                              "\"value\": \"12495872000\"}, {" +
		                          "\"key\": \"DeviceStorageSize\"," +
		                          "\"value\": \"12495872000\"}]," +
	                        "\"softwareVersion\": \"" + MagentaParameters[m_params].softwareVersion + "\"," +
	                        "\"osversion\": \"" + MagentaParameters[m_params].osVersion + "\"," +
	                        "\"terminalvendor\": \"" + MagentaParameters[m_params].terminalvendor + "\"," +
	                        "\"preSharedKeyID\": \"" + MagentaParameters[m_params].pskName + "\"," +
	                        "\"cnonce\": \"" + m_cnonce + "\"," +
	                        "\"areaid\": \"" + AREAID + "\"," +
	                        "\"templatename\": \"" + TEMPLATENAME + "\"," +
	                        "\"subnetId\": \"" + SUBNETID + "\"}";
//  kodi::Log(ADDON_LOG_DEBUG, "PostData %s", postData.c_str());

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    if (!doc.GetParseError()) {
      m_userID = Utils::JsonStringOrEmpty(doc, "userID");
      m_userGroup = Utils::JsonStringOrEmpty(doc, "usergroup");
      if (m_userID.empty()) {
        return false;
      }
      if (PlaceDevice()) {
        if (!JsonRequest(url, postData, doc)) {
          return false;
        }
      }
    }
    else {
      return false;
    }
  }

  m_settings->SetSetting("csrftoken", Utils::JsonStringOrEmpty(doc, "csrfToken"));
  const rapidjson::Value& ca_verimatrix = doc["ca"]["verimatrix"];
  m_licence_url = Utils::JsonStringOrEmpty(ca_verimatrix, "multiRightsWidevine");
  const rapidjson::Value& ca_device = doc["caDeviceInfo"][0];
  m_ca_device_id = Utils::JsonStringOrEmpty(ca_device, "VUID");
  m_userID = Utils::JsonStringOrEmpty(doc, "userID");
  m_userGroup = Utils::JsonStringOrEmpty(doc, "usergroup");
  m_sessionID = Utils::JsonStringOrEmpty(doc, "sessionid");
  m_encryptToken = Utils::JsonStringOrEmpty(doc, "encryptToken");
  m_userContentFilter = Utils::JsonStringOrEmpty(doc, "userContentFilter");
  m_userContentListFilter = Utils::JsonStringOrEmpty(doc, "userContentListFilter");

  if (doc.HasMember("configurations")) {
    const rapidjson::Value& configurations = doc["configurations"];
    for (rapidjson::SizeType j = 0; j < configurations.Size(); j++)
    {
      if (configurations[j].HasMember("extensionInfo")) {
        const rapidjson::Value& extensionInfo = configurations[j]["extensionInfo"];
        for (rapidjson::SizeType i = 0; i < extensionInfo.Size(); i++)
        {
          if (Utils::JsonStringOrEmpty(extensionInfo[i], "key") == "ChannelCategoryID")
          {
            m_ChannelCategoryID = Utils::JsonStringOrEmpty(extensionInfo[i], "value");
          }
        }
      }
    }
  }

  std::string key = MagentaParameters[m_params].pskValue + m_userID + m_encryptToken + m_cnonce;
  kodi::Log(ADDON_LOG_DEBUG, "Key: %s", key.c_str());

  SHA256 sha256;
  m_session_key  = sha256(key);
  std::transform(m_session_key.begin(), m_session_key.end(), m_session_key.begin(), ::toupper);

  kodi::Log(ADDON_LOG_DEBUG, "Session key: %s", m_session_key.c_str());
  return true;
}

bool CPVRMagenta::MagentaAuthenticate()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (!MagentaDTAuthenticate()) {
    int statusCode = 0;
    std::string url = m_sam_service_url + "/oauth2/tokens";
    std::string postData = "client_id=" + MagentaParameters[m_params].clientId + "&scope=ngtvepg offline_access&grant_type=refresh_token&refresh_token=" + m_settings->GetMagentaRefreshToken();

    std::string jsonString = m_httpClient->HttpPost(url, postData, statusCode);

    rapidjson::Document doc;
    doc.Parse(jsonString.c_str());
    if ((doc.GetParseError()) || (statusCode != 200))
    {
      url = m_sam_service_url + "/oauth2/bc-auth/start";
      postData = "client_id=" + MagentaParameters[m_params].clientId + "&scope=openid offline_access&claims={\"id_token\":{\"urn:telekom.com:all\":{\"essential\":false}}}";

      jsonString = m_httpClient->HttpPost(url, postData, statusCode);

      doc.Parse(jsonString.c_str());
      if ((doc.GetParseError()) || (statusCode != 200))
      {
        kodi::Log(ADDON_LOG_ERROR, "Failed to start bc-auth");
        return false;
      }
      std::string otp = Utils::JsonStringOrEmpty(doc, "initial_login_code");
      std::string auth_req_id = Utils::JsonStringOrEmpty(doc, "auth_req_id");
      std::string auth_req_sec = Utils::JsonStringOrEmpty(doc, "auth_req_sec");
      std::string expires = Utils::JsonStringOrEmpty(doc, "expires_in");
      std::string interval = Utils::JsonStringOrEmpty(doc, "interval");

      std::string text = "\n" + otp + "\n\n" + kodi::addon::GetLocalizedString(30047) + std::to_string((stoi(expires)/60)) + kodi::addon::GetLocalizedString(30048) + "\n";
      kodi::gui::dialogs::OK::ShowAndGetInput(kodi::addon::GetLocalizedString(30046), text);

      url = m_sam_service_url + "/oauth2/tokens";
      postData = "client_id=" + MagentaParameters[m_params].clientId + "&scope=openid offline_access&grant_type=urn:telekom:com:grant-type:remote-login&auth_req_id=" +
                auth_req_id + "&auth_req_sec=" + auth_req_sec + "&claims={\"id_token\":{\"urn:telekom.com:all\":{\"essential\":false}}}";

      jsonString = m_httpClient->HttpPost(url, postData, statusCode);

      doc.Parse(jsonString.c_str());
      if ((doc.GetParseError()) || (statusCode != 200))
      {
        kodi::Log(ADDON_LOG_ERROR, "Failed to get openid token");
        return false;
      }
      m_settings->SetSetting("openid_token", Utils::JsonStringOrEmpty(doc, "access_token"));
      postData = "client_id=" + MagentaParameters[m_params].clientId + "&scope=ngtvepg offline_access&grant_type=refresh_token&refresh_token=" + Utils::JsonStringOrEmpty(doc, "refresh_token");

      jsonString = m_httpClient->HttpPost(url, postData, statusCode);

      doc.Parse(jsonString.c_str());
      if ((doc.GetParseError()) || (statusCode != 200))
      {
        kodi::Log(ADDON_LOG_ERROR, "Failed to get ngtvepg token");
        return false;
      }
    }
    if (statusCode == 200) {
      m_settings->SetSetting("epg_token", Utils::JsonStringOrEmpty(doc, "access_token"));
      m_settings->SetSetting("refresh_token", Utils::JsonStringOrEmpty(doc, "refresh_token"));
      return MagentaDTAuthenticate();
    }
    //kodi::Log(ADDON_LOG_DEBUG, "Magenta Token returned: %s", jsonString.c_str());
  }

  return true;
}

bool CPVRMagenta::GetCategories()
{
  m_categories.clear();
  std::string jsonString;
  int statusCode = 0;

  std::string url = m_epg_https_url + "CategoryList?userContentListFilter=" + m_userContentListFilter;
  std::string postData = "{\"offset\": 0, \"count\": 1000,"
	                       "\"type\": \"VOD;AUDIO_VOD;VIDEO_VOD;CHANNEL;AUDIO_CHANNEL;VIDEO_CHANNEL;MIX;VAS;PROGRAM\","
	                       "\"categoryid\": \"" + m_ChannelCategoryID + "\"}";

  jsonString = m_httpClient->HttpPost(url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get categories");
    return false;
  }
  if (!doc.HasMember("categorylist")) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get categorylist");
    return false;
  }
  const rapidjson::Value& categories = doc["categorylist"];

  int position = 0;
  for (rapidjson::Value::ConstValueIterator itr1 = categories.Begin();
      itr1 != categories.End(); ++itr1)
  {
    const rapidjson::Value& categoryItem = (*itr1);

    MagentaCategory magenta_category;

    magenta_category.position = ++position;
    magenta_category.name = Utils::JsonStringOrEmpty(categoryItem, "name");
    magenta_category.id = std::stol(Utils::JsonStringOrEmpty(categoryItem, "id"));
    magenta_category.isRadio = false;

    m_categories.emplace_back(magenta_category);
  }
  return true;
}

void CPVRMagenta::FillRecording(const rapidjson::Value& recordingItem, MagentaRecording& magenta_recording, const int& index)
{
  magenta_recording.index = index;
  magenta_recording.pvrId = Utils::JsonStringOrEmpty(recordingItem, "pvrId");
  std::string channelId = Utils::JsonStringOrEmpty(recordingItem, "channelId");
  if (channelId != "") {
    magenta_recording.channelId = std::stoi(channelId);
  }
  std::string mediaId = Utils::JsonStringOrEmpty(recordingItem, "mediaId");
  if (mediaId != "") {
    magenta_recording.mediaId = std::stoi(mediaId);
  }
  magenta_recording.introduce = Utils::JsonStringOrEmpty(recordingItem, "introduce");
  magenta_recording.beginTime = Utils::JsonStringOrEmpty(recordingItem, "beginTime");
  magenta_recording.endTime = Utils::JsonStringOrEmpty(recordingItem, "endTime");
  std::string beginoffset = Utils::JsonStringOrEmpty(recordingItem, "beginOffset");
  //kodi::Log(ADDON_LOG_DEBUG, "PVRID: %s, BeginOffSet: %s", magenta_recording.pvrId.c_str(), beginoffset.c_str());
  if (!beginoffset.empty()) {
    magenta_recording.beginOffset = stoi(beginoffset);
  }
  std::string endoffset = Utils::JsonStringOrEmpty(recordingItem, "endOffset");
  if (!endoffset.empty()) {
    magenta_recording.endOffset = stoi(endoffset);
  }
  magenta_recording.pvrName = Utils::JsonStringOrEmpty(recordingItem, "pvrName");
  magenta_recording.channelName = Utils::JsonStringOrEmpty(recordingItem, "channelName");
  magenta_recording.picture = GetPictureFromItem(recordingItem);
  std::string realRecordLength = Utils::JsonStringOrEmpty(recordingItem, "realRecordLength");
  if (realRecordLength != "") {
    magenta_recording.realRecordLength = std::stoi(realRecordLength);
  } else {
    magenta_recording.realRecordLength = 0;
  }
  std::string bookmarkTime = Utils::JsonStringOrEmpty(recordingItem, "bookmarkTime");
  if (bookmarkTime != "") {
    magenta_recording.bookmarkTime = std::stoi(bookmarkTime);
  } else {
    magenta_recording.bookmarkTime = 0;
  }
  magenta_recording.isWatched = (Utils::JsonStringOrEmpty(recordingItem, "isWatched") == "0") ? false : true;
  magenta_recording.ratingId = stoi(Utils::JsonStringOrEmpty(recordingItem, "ratingId"));
  std::string deleteMode = Utils::JsonStringOrEmpty(recordingItem, "deleteMode");
  if (deleteMode != "") {
    magenta_recording.deleteMode = std::stoi(deleteMode);
  } else {
    magenta_recording.deleteMode = 0;
  }
  if (recordingItem.HasMember("genreIds")) {
    const rapidjson::Value& genres = recordingItem["genreIds"];

    for (rapidjson::SizeType i = 0; i < genres.Size(); i++)
    {
      std::string genre = genres[i].GetString();
      magenta_recording.genres.emplace_back(stoi(genre));
    }
  }
  if (recordingItem.HasMember("seriesId")) {
    magenta_recording.seriesId = stoi(Utils::JsonStringOrEmpty(recordingItem, "seriesId"));
  }
  if (recordingItem.HasMember("programId")) {
    magenta_recording.programId = Utils::JsonStringOrEmpty(recordingItem, "programId");
  }
}

bool CPVRMagenta::GetTimersRecordings(const bool isRecording)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (isRecording) {
    m_recordings.clear();
    m_recGroups.clear();
  } else {
    m_timers.clear();
    m_timerGroups.clear();
  }

  std::string url = m_epg_https_url + "QueryPVR";
  std::string postData = "{\"count\": -1,"
	                       "\"expandSubTask\": 2,"
	                       "\"isFilter\": 0,"
	                       "\"offset\": 0,"
	                       "\"orderType\": 1,";
  if (m_settings->IsOnlyCloud()) {
    postData += "\"pvrType\": 2,";
  }
  postData += "\"type\": 0,"
	            "\"DTQueryType\": ";
  postData += (isRecording ? "0" : "1");
  postData += "}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return PVR_ERROR_FAILED;
  }

  if (doc.HasMember("pvrlist"))
  {
    const rapidjson::Value& recordings = doc["pvrlist"];

    unsigned int index = 1;
    for (rapidjson::Value::ConstValueIterator itr1 = recordings.Begin();
        itr1 != recordings.End(); ++itr1)
    {
      const rapidjson::Value& recordingItem = (*itr1);

      if (recordingItem.HasMember("pvrId")) {
        MagentaRecording magenta_recording;
        FillRecording(recordingItem, magenta_recording, index++);

        if (isRecording) {
          m_recordings.emplace_back(magenta_recording);
          kodi::Log(ADDON_LOG_DEBUG, "Found recording: [%s]", magenta_recording.pvrName.c_str());
        } else {
          m_timers.emplace_back(magenta_recording);
          kodi::Log(ADDON_LOG_DEBUG, "Found timer: [%s]", magenta_recording.pvrName.c_str());
        }
      } else if (recordingItem.HasMember("periodPVRTaskName") && (recordingItem.HasMember("periodPVRTaskId")) && (recordingItem.HasMember("pvrList"))) {
        MagentaRecordingGroup recording_group;

        recording_group.index = index++;
        recording_group.periodPVRTaskId = Utils::JsonStringOrEmpty(recordingItem, "periodPVRTaskId");
        recording_group.periodPVRTaskName = Utils::JsonStringOrEmpty(recordingItem, "periodPVRTaskName");
        recording_group.channelName = Utils::JsonStringOrEmpty(recordingItem, "channelName");
        recording_group.seriesType = stoi(Utils::JsonStringOrEmpty(recordingItem, "seriesType"));
        recording_group.seriesId = stoi(Utils::JsonStringOrEmpty(recordingItem, "seriesId"));
        recording_group.mediaId = stoi(Utils::JsonStringOrEmpty(recordingItem, "mediaId"));
        recording_group.channelId = stoi(Utils::JsonStringOrEmpty(recordingItem, "channelId"));
        recording_group.type = stoi(Utils::JsonStringOrEmpty(recordingItem, "type"));
        recording_group.beginOffset = stoi(Utils::JsonStringOrEmpty(recordingItem, "beginOffset"));
        recording_group.endOffset = stoi(Utils::JsonStringOrEmpty(recordingItem, "endOffset"));
        recording_group.deleteMode = stoi(Utils::JsonStringOrEmpty(recordingItem, "deleteMode"));
        recording_group.latestSeriesNum = stoi(Utils::JsonStringOrEmpty(recordingItem, "latestSeriesNum"));
        recording_group.timeMode = stoi(Utils::JsonStringOrEmpty(recordingItem, "timeMode"));
        recording_group.selectedBeginTime = Utils::JsonStringOrEmpty(recordingItem, "selectedBeginTime");

        const rapidjson::Value& groupmembers = recordingItem["pvrList"];

        for (rapidjson::Value::ConstValueIterator itr2 = groupmembers.Begin();
              itr2 != groupmembers.End(); ++itr2)
        {
          const rapidjson::Value& groupItem = (*itr2);
          MagentaRecording magenta_recording;
          FillRecording(groupItem, magenta_recording, index++);

          //recording_group.groupRecordings.emplace_back(magenta_recording);
          magenta_recording.periodPVRTaskName = Utils::JsonStringOrEmpty(recordingItem, "periodPVRTaskName");
          magenta_recording.groupIndex = recording_group.index;
          if (isRecording) {
            m_recordings.emplace_back(magenta_recording);
            kodi::Log(ADDON_LOG_DEBUG, "Added recording to group: %s with seriesId: %i, ID: %s", magenta_recording.pvrName.c_str(), magenta_recording.seriesId, magenta_recording.pvrId.c_str());
          } else {
            m_timers.emplace_back(magenta_recording);
            kodi::Log(ADDON_LOG_DEBUG, "Added timer to group: %s with seriesId: %i, ID: %s", magenta_recording.pvrName.c_str(), magenta_recording.seriesId, magenta_recording.pvrId.c_str());
          }
        }
        if (isRecording) {
          m_recGroups.emplace_back(recording_group);
          kodi::Log(ADDON_LOG_DEBUG, "Added recording group: %s Index: %i", recording_group.periodPVRTaskName.c_str(), recording_group.seriesId);
        } else {
          m_timerGroups.emplace_back(recording_group);
          kodi::Log(ADDON_LOG_DEBUG, "Added timer group: %s Index: %i", recording_group.periodPVRTaskName.c_str(), recording_group.seriesId);
        }
      }
    }
  }
  return true;
}

bool CPVRMagenta::GetGenreIds()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  m_genres.clear();
  std::string url = m_epg_https_url + "GetGenreList";
  std::string postData = "{}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return false;
  }

  if (doc.HasMember("genres")) {
    const rapidjson::Value& genres = doc["genres"];

    for (rapidjson::SizeType i = 0; i < genres.Size(); i++)
    {
      MagentaGenre magenta_genre;

      magenta_genre.genreId = stoi(Utils::JsonStringOrEmpty(genres[i], "genreId"));
      magenta_genre.genreType = stoi(Utils::JsonStringOrEmpty(genres[i], "genreType"));
      magenta_genre.genreName = Utils::JsonStringOrEmpty(genres[i], "genreName");
      magenta_genre.kodiGenre.genreType = 0;
      magenta_genre.kodiGenre.genreSubType = 0;

      m_genres.emplace_back(magenta_genre);
    }
  }
  return true;
}

bool CPVRMagenta::GetMyGenres()
{
  std::string content;
  kodi::vfs::CFile myFile;
  std::string file = kodi::addon::GetAddonPath() + "mygenres.json";
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
  int currentGenreId;

  for (rapidjson::SizeType i = 0; i < genres.Size(); i++)
  {
    currentGenreId = stoi(Utils::JsonStringOrEmpty(genres[i], "genreId"));
    for (auto& genre : m_genres) {
      if (genre.genreId == currentGenreId) {
        genre.kodiGenre.genreType = stoi(Utils::JsonStringOrEmpty(genres[i], "genreType"));
        genre.kodiGenre.genreSubType = stoi(Utils::JsonStringOrEmpty(genres[i], "genreSubType"));
        kodi::Log(ADDON_LOG_DEBUG, "Added mapped genre for Magenta GenreID: %i, Kodi Genre Type: %i, Kodi Genre Subtype: %i",
                                    genre.genreId, genre.kodiGenre.genreType, genre.kodiGenre.genreSubType);
      }
    }
  }

  return true;
}

bool CPVRMagenta::GetDeviceList()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  m_devices.clear();
  std::string url = m_epg_https_url + "GetDeviceList";
  std::string postData = "{\"userid\": \"" + m_userID + "\","
	                        "\"deviceType\": \"" + std::to_string(IPTV_STB) + ";" +
                                                 std::to_string(OTT) + ";" +
                                                 std::to_string(OTT_STB) + "\"}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return false;
  }

  if (!doc.HasMember("deviceList")) {
    return false;
  }

  const rapidjson::Value& devices = doc["deviceList"];

  for (rapidjson::SizeType i = 0; i < devices.Size(); i++)
  {
    MagentaDevice magenta_device;

    magenta_device.deviceName = Utils::JsonStringOrEmpty(devices[i], "deviceName");
    magenta_device.deviceId = Utils::JsonStringOrEmpty(devices[i], "deviceId");
    magenta_device.deviceType = stoi(Utils::JsonStringOrEmpty(devices[i], "deviceType"));
    magenta_device.physicalDeviceId = Utils::JsonStringOrEmpty(devices[i], "physicalDeviceId");
    magenta_device.lastOfflineTime = Utils::JsonStringOrEmpty(devices[i], "lastOfflineTime");
    magenta_device.terminalType = Utils::JsonStringOrEmpty(devices[i], "terminalType");
    magenta_device.channelNamespace = Utils::JsonStringOrEmpty(devices[i], "channelNamespace");
    magenta_device.channelNamespaceName = Utils::JsonStringOrEmpty(devices[i], "channelNamespaceName");
    magenta_device.status = stoi(Utils::JsonStringOrEmpty(devices[i], "status"));

    kodi::Log(ADDON_LOG_DEBUG, "Found device %s, device ID: %s, device type %i, physical ID: %s, last online: %s",
                                                            magenta_device.deviceName.c_str(),
                                                            magenta_device.deviceId.c_str(),
                                                            magenta_device.deviceType,
                                                            magenta_device.physicalDeviceId.c_str(),
                                                            magenta_device.lastOfflineTime.c_str());

    m_devices.emplace_back(magenta_device);
  }

/*
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  std::string s(buffer.GetString(), buffer.GetSize());
//  kodi::Log(ADDON_LOG_DEBUG, "Hier: %s", s.c_str());

  kodi::vfs::CFile myFile;
  std::string file = kodi::addon::GetUserPath() + "/myDevices.json";
  myFile.OpenFileForWrite(file, true);
  myFile.Write(s.c_str(), s.length());
*/
  return true;
}

bool CPVRMagenta::IsDeviceInList()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& device : m_devices)
  {
    if (m_device_id == device.physicalDeviceId)
    {
      return true;
    }
  }
  return false;
}

bool CPVRMagenta::ReplaceDevice(const std::string& orgDeviceId)
{
  kodi::Log(ADDON_LOG_DEBUG, "Replace device with ID: [%s]", orgDeviceId.c_str());

  if ((m_userID.empty()) || (m_device_id.empty()) || (orgDeviceId.empty()))
    return false;

  std::string url = m_epg_https_url + "ReplaceDevice";
  std::string postData = "{\"destDeviceId\": \"" + m_device_id + "\","
	                        "\"orgDeviceId\": \"" + orgDeviceId + "\","
	                        "\"userid\": \"" + m_userID + "\"}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return false;
  }

  return true;
}

bool CPVRMagenta::ModifyDeviceName(const std::string& deviceId)
{
  kodi::Log(ADDON_LOG_DEBUG, "Modify device name for ID: [%s]", deviceId.c_str());

  std::string url = m_epg_https_url + "ModifyDeviceName";
  std::string postData = "{\"deviceid\": \"" + deviceId + "\","
	                        "\"deviceName\": \"" + DEVICENAME + "\"}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return false;
  }

  return true;
}

bool CPVRMagenta::ReplaceOldestDevice()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_devices.size() == 0) {
    return false;
  }
  std::string oldest_id = "";
  time_t oldest_time;
  for (const auto& device : m_devices)
  {
    if (device.deviceType != OTT)
      continue;

    if (oldest_id == "") {
      oldest_id = device.deviceId;
      oldest_time = Utils::StringToTime(PrepareTime(device.lastOfflineTime));
    } else {
//      kodi::Log(ADDON_LOG_DEBUG, "Checking %s", device.deviceId.c_str());
      if ((!device.lastOfflineTime.empty()) && (Utils::StringToTime(PrepareTime(device.lastOfflineTime)) < oldest_time)) {
        oldest_id = device.deviceId;
        oldest_time = Utils::StringToTime(PrepareTime(device.lastOfflineTime));
      }
    }
//    kodi::Log(ADDON_LOG_DEBUG, "Current oldest_id: %s time %s", oldest_id.c_str(), Utils::TimeToString(oldest_time).c_str());
  }
  if (oldest_id != "") {
    if (ReplaceDevice(oldest_id)) {
      ModifyDeviceName(oldest_id);
      return true;
    }
  }
  return false;
}

bool CPVRMagenta::PlaceDevice()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (!GetDeviceList()) {
    return false;
  }
  if (!IsDeviceInList()) {
    if (!ReplaceOldestDevice()) {
      return false;
    }
    GetDeviceList();
  }

  return true;
}

CPVRMagenta::CPVRMagenta() :
  m_settings(new CSettings())
{
  m_settings->Load();
  m_httpClient = new HttpClient(m_settings);

  m_isMagenta2 = m_settings->IsMagenta2();
  if (m_isMagenta2) {
    m_magenta2 = new CPVRMagenta2(m_settings, m_httpClient);
    return;
  }

  m_params = m_settings->GetTerminalType();

  srand(time(nullptr));
  GenerateCNonce();
  m_userGroup = "-1";
  m_currentMediaId = -1;
  m_currentChannelId = -1;
  m_ChannelCategoryID = DEFAULT_CATEGORY_ID;

  m_device_id = m_settings->GetMagentaDeviceID();
  if (m_device_id.empty()) {
    m_device_id = Utils::CreateUUID();
    m_settings->SetSetting("deviceid", m_device_id);
  }
  kodi::Log(ADDON_LOG_DEBUG, "Current DeviceID %s", m_device_id.c_str());

  if (!GuestLogin()) {
    return;
  }
  if (m_settings->GetMagentaCSRFToken().empty()) {
    GuestAuthenticate();
  }
  if (!MagentaAuthenticate()) {
    return;
  }
  if (m_settings->IsGroupsenabled()) {
    if (!GetCategories()) {
      return;
    }
  }
  GetDeviceList();
  GetGenreIds();
  GetMyGenres();
  GetTimersRecordings(true); //recordings
  GetTimersRecordings(false); //timers
  UpdateBookmarks();
  LoadChannels();
}

bool CPVRMagenta::AddGroupChannel(const long groupid, const unsigned int channelid)
{
  for (auto& cgroup : m_categories)
  {
    if (cgroup.id != groupid)
      continue;

    cgroup.channelids.emplace_back(channelid);
    return true;
  }
  return false;
}

CPVRMagenta::~CPVRMagenta()
{
  m_channels.clear();
}

ADDON_STATUS CPVRMagenta::SetSetting(const std::string& settingName, const std::string& settingValue)
{
  ADDON_STATUS result = m_settings->SetSetting(settingName, settingValue);
  if (!m_settings->VerifySettings()) {
    return ADDON_STATUS_NEED_SETTINGS;
  }
  return result;
}

bool CPVRMagenta::LoadChannels()
{
  m_channels.clear();
  int pictureNo = m_settings->UseWhiteLogos() ? 15:14;
  kodi::Log(ADDON_LOG_DEBUG, "Load Magenta Channels");
  std::string url = m_epg_https_url + "AllChannel?userContentListFilter=" + m_userContentListFilter;
  std::string jsonString;
  int statusCode = 0;
  std::string postData = "{\"properties\":[{"
                          "\"name\":\"logicalChannel\","
                          "\"include\":\"/channellist/logicalChannel/contentId,"
                                        "/channellist/logicalChannel/name,"
                                        "/channellist/logicalChannel/chanNo,"
                                        "/channellist/logicalChannel/externalCode,"
                                        "/channellist/logicalChannel/categoryIds,"
                                        "/channellist/logicalChannel/introduce,"
                                        "/channellist/logicalChannel/extensionInfo,"
                                        "/channellist/logicalChannel/pictures/picture/href,"
                                        "/channellist/logicalChannel/pictures/picture/imageType,"
                                        "/channellist/logicalChannel/physicalChannels/physicalChannel/mediaId,"
                                        "/channellist/logicalChannel/physicalChannels/physicalChannel/definition,"
                                        "/channellist/logicalChannel/physicalChannels/physicalChannel/externalCode,"
                                        "/channellist/logicalChannel/physicalChannels/physicalChannel/fileFormat\"}],"
                          "\"metaDataVer\":\"Channel/1.1\","
                          "\"channelNamespace\":\"5\","
                          "\"filterlist\":[{\"key\":\"IsHide\",\"value\":\"-1\"}],"
                          "\"returnSatChannel\":0}";

  jsonString = m_httpClient->HttpPost(url, postData, statusCode);

//  kodi::Log(ADDON_LOG_DEBUG, "Raw Channels: %s", jsonString.c_str());

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    return false;
  }

  if (!doc.HasMember("channellist")) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get channellist");
    return false;
  }
  const rapidjson::Value& channels = doc["channellist"];

  int startnum = m_settings->GetStartNum()-1;
  for (rapidjson::Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    const rapidjson::Value& channelItem = (*itr1);

    MagentaChannel magenta_channel;

    magenta_channel.bRadio = false;
//    magenta_channel.bArchive = false;
    magenta_channel.iUniqueId = stoi(Utils::JsonStringOrEmpty(channelItem, "contentId"));

    int chanNo = stoi(Utils::JsonStringOrEmpty(channelItem, "chanNo"));
    magenta_channel.isHidden = (chanNo < 0) ? true : false;
    magenta_channel.iChannelNumber = (chanNo < 0) ? 0 : startnum + chanNo;

    magenta_channel.strChannelName = Utils::JsonStringOrEmpty(channelItem, "name");

    const rapidjson::Value& pictures = channelItem["pictures"];
    for (rapidjson::Value::ConstValueIterator itr2 = pictures.Begin();
        itr2 != pictures.End(); ++itr2)
    {
      const rapidjson::Value& pictureItem = (*itr2);

      if (stoi(Utils::JsonStringOrEmpty(pictureItem, "imageType")) == pictureNo) {
        magenta_channel.strIconPath = Utils::JsonStringOrEmpty(pictureItem, "href");
      }
    }

    if (channelItem.HasMember("physicalChannels")) {

      const rapidjson::Value& physicals = channelItem["physicalChannels"];
      for (rapidjson::Value::ConstValueIterator itr2 = physicals.Begin();
          itr2 != physicals.End(); ++itr2)
      {
        const rapidjson::Value& physicalItem = (*itr2);

        PhysicalChannel physicalChannel;

        physicalChannel.mediaId = stoi(Utils::JsonStringOrEmpty(physicalItem, "mediaId"));
        physicalChannel.externalCode = Utils::JsonStringOrEmpty(physicalItem, "externalCode");
        physicalChannel.fileFormat = stoi(Utils::JsonStringOrEmpty(physicalItem, "fileFormat"));
        physicalChannel.definition = stoi(Utils::JsonStringOrEmpty(physicalItem, "definition"));

        magenta_channel.physicalChannels.emplace_back(physicalChannel);
        kodi::Log(ADDON_LOG_DEBUG, "%i. Channel Name: %s ID: %i MediaID %i", magenta_channel.iChannelNumber, magenta_channel.strChannelName.c_str(), magenta_channel.iUniqueId, physicalChannel.mediaId);
      }
    }

    if (channelItem.HasMember("categoryIds")) {
      const rapidjson::Value& categories = channelItem["categoryIds"];
      for (rapidjson::SizeType i = 0; i < categories.Size(); i++)
      {
        AddGroupChannel(std::stol(categories[i].GetString()), magenta_channel.iUniqueId);
      }
    }

    m_channels.emplace_back(magenta_channel);
  }

  url = m_epg_https_url + "AllChannelDynamic";

  statusCode = 0;
  postData = "{\"channelIdList\":[";

  for (const auto& channel : m_channels)
  {
    postData += "{\"channelId\":\"" + std::to_string(channel.iUniqueId) + "\",\"type\":\"VIDEO_CHANNEL\"},";
  }
  postData.erase(std::prev(postData.end()));
  postData += "],\"properties\":[{\"name\":\"logicalChannelDynamic\",";
  postData += "\"include\":\"/channelDynamicList/logicalChannelDynamic/contentId,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/mediaId,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/playurl,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/btvBR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/btvCR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/pltvCR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/pltvBR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/irCR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/irBR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/npvrRecCR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/npvrRecBR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/npvrCR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/npvrOnlinePlayCR,"
                            "/channelDynamicList/logicalChannelDynamic/physicalChannels/physicalChannelDynamic/npvrOnlinePlayBR\"}],";
  postData += "\"channelNamespace\":\"5\",";
  postData += "\"filterlist\":[{\"key\":\"IsHide\",\"value\":\"-1\"}]}";

//  kodi::Log(ADDON_LOG_DEBUG, "PostData: %s", postData.c_str());

  jsonString = m_httpClient->HttpPost(url, postData, statusCode);

//  kodi::Log(ADDON_LOG_DEBUG, "Raw dynamic Channels: %s", jsonString.c_str());

  rapidjson::Document doc2;
  doc2.Parse(jsonString.c_str());
  if (doc2.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load dynamic channels");
    return false;
  }

  const rapidjson::Value& dynamics = doc2["channelDynamicList"];

  int i=0;
  for (rapidjson::Value::ConstValueIterator itr1 = dynamics.Begin();
      itr1 != dynamics.End(); ++itr1)
  {
    const rapidjson::Value& dynamicItem = (*itr1);

    const rapidjson::Value& physicals2 = dynamicItem["physicalChannels"];
    for (rapidjson::Value::ConstValueIterator itr2 = physicals2.Begin();
        itr2 != physicals2.End(); ++itr2)
    {
      const rapidjson::Value& physItem = (*itr2);
      for (int j = 0; j < m_channels[i].physicalChannels.size(); j++)
      {
        if (m_channels[i].physicalChannels[j].mediaId == stoi(Utils::JsonStringOrEmpty(physItem, "mediaId"))) {
          m_channels[i].physicalChannels[j].playUrl = Utils::JsonStringOrEmpty(physItem, "playurl");
          const rapidjson::Value& btv = physItem["btvCR"];
          const rapidjson::Value& pltv = physItem["pltvCR"];
          const rapidjson::Value& npvr = physItem["npvrCR"];
          m_channels[i].physicalChannels[j].btvenabled = Utils::JsonStringOrEmpty(btv, "e") == "1" ? true : false;
          m_channels[i].physicalChannels[j].pltvenabled = Utils::JsonStringOrEmpty(pltv, "e") == "1" ? true : false;
          m_channels[i].physicalChannels[j].npvrenabled = Utils::JsonStringOrEmpty(npvr, "e") == "1" ? true : false;
        }
      }
    }
    i++;
  }

  if (!m_settings->UseCustomChannels()) {
    return true;
  }

  url = m_epg_https_url + "GetCustomChanNo";
  postData = "{\"queryType\": 1}";

  rapidjson::Document doc3;
  if (JsonRequest(url, postData, doc3)) {
    if (doc3.HasMember("customChanNo")) {
      const rapidjson::Value& customlist = doc3["customChanNo"];

      for (rapidjson::Value::ConstValueIterator itr4 = customlist.Begin();
          itr4 != customlist.End(); ++itr4)
      {
        const rapidjson::Value& customChanNo = (*itr4);
        int uniqueId = stoi(Utils::JsonStringOrEmpty(customChanNo, "key"));
        for (auto& channel : m_channels)
        {
          if (channel.iUniqueId == uniqueId) {
            channel.iChannelNumber = startnum + stoi(Utils::JsonStringOrEmpty(customChanNo, "value"));
          }
        }
      }
    }
  }

  return true;
}

void CPVRMagenta::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::string& url,
                                    bool realtime, bool playTimeshiftBuffer, bool epgplayback)
{
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", url.c_str());

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");
  properties.emplace_back(PVR_STREAM_PROPERTY_EPGPLAYBACKASLIVE, epgplayback ? "true" : "false");

  // MPEG DASH
//  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] dash");
  properties.emplace_back("inputstream.adaptive.play_timeshift_buffer", playTimeshiftBuffer ? "true" : "false");
  properties.emplace_back("inputstream.adaptive.manifest_type", "mpd");
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");

  properties.emplace_back("inputstream.adaptive.license_type", "com.widevine.alpha");

  std::string lkey = m_licence_url + "|deviceId=" + m_ca_device_id + "|R{SSM}|";
  kodi::Log(ADDON_LOG_DEBUG, "Licence Key: %s", lkey.c_str());
  properties.emplace_back("inputstream.adaptive.license_key", lkey);

//  properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
}


PVR_ERROR CPVRMagenta::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetCapabilities(capabilities);

  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsEPGEdl(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(false);
  capabilities.SetSupportsChannelGroups(m_settings->IsGroupsenabled());
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsLastPlayedPosition(true);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsProviders(false);
  /* PVR recording lifetime values and presentation.*/
  std::vector<kodi::addon::PVRTypeIntValue> lifetimeValues;
  GetLifetimeValues(lifetimeValues, true);
  capabilities.SetRecordingsLifetimeValues(lifetimeValues);
//  capabilities.SetSupportsAsyncEPGTransfer(true);
//  capabilities.SetHandlesInputStream(true);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetBackendName(std::string& name)
{
  name = "Magenta PVR";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetBackendVersion(std::string& version)
{
  version = STR(MAGENTA_VERSION);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetBackendHostname(std::string& hostname)
{
  hostname = STR(m_epg_https_url);
  return PVR_ERROR_NO_ERROR;
}

uint64_t CPVRMagenta::GetPVRSpace(const int type)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  std::string url = m_epg_https_url + "QueryPVRSpace";
  std::string postData = "{\"type\": " + std::to_string(type) + "}";

  rapidjson::Document doc;
  if ((!JsonRequest(url, postData, doc)) || (!doc.HasMember("space"))) {
    return 0;
  }
  kodi::Log(ADDON_LOG_DEBUG, "finished: [%s]", __FUNCTION__);
  return stol(Utils::JsonStringOrEmpty(doc, "space"));
}

PVR_ERROR CPVRMagenta::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetDriveSpace(total, used);

  total = GetPVRSpace(0) * KBM;
  used = GetPVRSpace(1) * KBM;
  kodi::Log(ADDON_LOG_DEBUG, "Reported %llu/%llu used/total", used, total);
  return PVR_ERROR_NO_ERROR;
}

int CPVRMagenta::GetGenreIdFromName(const std::string& genreName)
{
  for (const auto& genre : m_genres)
  {
    if (genre.genreName == genreName)
    {
      return genre.genreId;
    }
  }
  return 0;
}

KodiGenre CPVRMagenta::GetKodiGenreFromId(const int& genreId)
{
  for (const auto& genre : m_genres)
  {
    if (genre.genreId == genreId) {
      return genre.kodiGenre;
    }
  }
  KodiGenre nullgenre;
  nullgenre.genreType = 0;
  nullgenre.genreSubType = 0;
  return nullgenre;
}

bool CPVRMagenta::GetEPGDetails(std::string& contentCode, rapidjson::Document& epgDoc)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string jsonEpg;
  int statusCode = 0;

  std::string postData = "{\"filterType\": \"1\","
                         "\"idType\": \"2\","
                         "\"properties\": [{"
                                 "\"name\": \"playbill\","
                                             "\"include\": \"audioAttribute,casts,channelid,contentRight,country,endtime,episodeInformation,"
                                             "externalContentCode,externalIds,genres,id,introduce,issubscribed,lifetimeId,name,pictures,producedate,"
                                             "ratingid,seasonNum,seriesID,starttime,subName,subNum,tipType,type,videoAttribute\"}],"
                         "\"metaDataVer\": \"Channel/1.1\","
                         "\"playbill\": \"" + contentCode + "\"}";

  std::string url = m_epg_https_url + "ContentDetail?userContentFilter=" + m_userContentFilter;

  jsonEpg = m_httpClient->HttpPost(url, postData, statusCode);
//    kodi::Log(ADDON_LOG_DEBUG, "GetProgramme returned: code: %i %s", statusCode, jsonEpg.c_str());

  epgDoc.Parse(jsonEpg.c_str());
  if (epgDoc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "[GetEPGDetails] ERROR: error while parsing json");
    return false;
  }

  if (epgDoc.HasMember("retcode") || !epgDoc.HasMember("playbilllist")) {
      MagentaAuthenticate();
      jsonEpg = m_httpClient->HttpPost(url, postData, statusCode);
      epgDoc.Parse(jsonEpg.c_str());
      if (epgDoc.HasMember("retcode") || !epgDoc.HasMember("playbilllist")) {
        return false;
      }
  }

  return true;
}

bool CPVRMagenta::GetEPGPlaybill(const int& channelId, const time_t& start, const time_t& end, rapidjson::Document& epgDoc)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string startTime = Utils::TimeToString(start);
  std::string endTime = Utils::TimeToString(end);

  std::string jsonEpg;
  int statusCode = 0;

  kodi::Log(ADDON_LOG_DEBUG, "Start %u End %u", start, end);
  kodi::Log(ADDON_LOG_DEBUG, "EPG Request for channel %i from %s to %s", channelId, startTime.c_str(), endTime.c_str());

  std::string postData = "{\"channelid\": \"" + std::to_string(channelId) + "\"," +
                         "\"type\": 2," +
                         "\"offset\": 0," +
                         "\"isFillProgram\": 1," +
                         "\"properties\": [{" +
                                 "\"name\": \"playbill\"," +
                                             "\"include\": \"audioAttribute,casts,channelid,contentRight,endtime,externalContentCode,episodeInformation," +
                                                            "externalIds,genres,id,introduce,name,pictures,producedate,ratingid,seasonNum,seriesID,starttime," +
                                                            "subName,subNum,tipType,videoAttribute\"}]," +
                         "\"count\": 1000," +
                         "\"begintime\": \"" + startTime + "\"," +
                         "\"endtime\": \"" + endTime + "\"}";

//    kodi::Log(ADDON_LOG_DEBUG, "PostData %s", postData.c_str());

  std::string url = m_epg_https_url + "PlayBillList?userContentFilter=" + m_userContentFilter;

  jsonEpg = m_httpClient->HttpPost(url, postData, statusCode);
//    kodi::Log(ADDON_LOG_DEBUG, "GetProgramme returned: code: %i %s", statusCode, jsonEpg.c_str());

  epgDoc.Parse(jsonEpg.c_str());
  if (epgDoc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing json");
    return false;
  }

  if ((epgDoc.HasMember("retcode")) || (!epgDoc.HasMember("playbilllist"))) {
      kodi::Log(ADDON_LOG_ERROR, "EPG Request returned %s - need to reauthenticate", jsonEpg.c_str());
      MagentaAuthenticate();
      jsonEpg = m_httpClient->HttpPost(url, postData, statusCode);
      epgDoc.Parse(jsonEpg.c_str());
      if ((epgDoc.HasMember("retcode")) || (!epgDoc.HasMember("playbilllist"))) {
        return false;
      }
  }

  return true;
}

bool CPVRMagenta::FillEPGTag(const rapidjson::Value& epgItem, kodi::addon::PVREPGTag& tag)
{
//  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  std::string id = Utils::JsonStringOrEmpty(epgItem,"id");
  std::string channelid = Utils::JsonStringOrEmpty(epgItem,"channelid");
  std::string name = Utils::JsonStringOrEmpty(epgItem,"name");
  std::string epgstart = Utils::JsonStringOrEmpty(epgItem,"starttime");
  std::string epgend = Utils::JsonStringOrEmpty(epgItem,"endtime");
  unsigned int epg_tag_flags = EPG_TAG_FLAG_UNDEFINED;

  if (id.empty() || channelid.empty() || name.empty() || epgstart.empty() || epgend.empty())
    return false;
  try {
    tag.SetUniqueBroadcastId(std::stoi(id));
  } catch (const std::exception& e) {}
  try {
    tag.SetUniqueChannelId(std::stoi(channelid));
  } catch (const std::exception& e) {}
  tag.SetTitle(name);
//      kodi::Log(ADDON_LOG_DEBUG, "EPG Name %s", tag.GetTitle().c_str());
  tag.SetPlot(Utils::JsonStringOrEmpty(epgItem,"introduce"));

  tag.SetStartTime(Utils::StringToTime(epgstart));
  tag.SetEndTime(Utils::StringToTime(epgend));

  std::string image = GetPictureFromItem(epgItem);
  if (!image.empty()) {
    tag.SetIconPath(image);
  }
  std::string genres = Utils::JsonStringOrEmpty(epgItem,"genres");
  if (genres != "") {
    std::vector<std::string> out;
    tokenize(genres, ",", out);
    int genreId = GetGenreIdFromName(out[0]);
    KodiGenre myGenre = GetKodiGenreFromId(genreId);
    if (myGenre.genreType != 0) {
//          kodi::Log(ADDON_LOG_DEBUG, "Setting genre for id: %i to type: %i subtype: %i", genreId, myGenre.genreType, myGenre.genreSubType);
      tag.SetGenreType(myGenre.genreType);
      tag.SetGenreSubType(myGenre.genreSubType);
    } else {
      tag.SetGenreType(EPG_GENRE_USE_STRING);
      tag.SetGenreDescription(genres);
    }
  }
  std::string subname = Utils::JsonStringOrEmpty(epgItem,"subName");
  if (subname != "") {
    tag.SetEpisodeName(subname);
  }
  std::string season = Utils::JsonStringOrEmpty(epgItem, "seasonNum");
  if (season != "") {
    try {
      tag.SetSeriesNumber(std::stoi(season));
    } catch (const std::exception& e) {}
  }
  std::string episode = Utils::JsonStringOrEmpty(epgItem, "subNum");
  if (episode != "") {
    try {
      tag.SetEpisodeNumber(std::stoi(episode));
    } catch (const std::exception& e) {}
    epg_tag_flags += EPG_TAG_FLAG_IS_SERIES;
  }
  std::string rating = Utils::JsonStringOrEmpty(epgItem, "ratingid");
  if (rating != "-1") {
    try {
      tag.SetParentalRating(std::stoi(rating));
    } catch (const std::exception& e) {}
  }
  if (epgItem.HasMember("casts")) {
    const rapidjson::Value& casts = epgItem["casts"];
    std::string cast = "";
    std::string director = "";
    std::string writer = "";
    for (rapidjson::SizeType i = 0; i < casts.Size(); i++)
    {
      switch (stoi(Utils::JsonStringOrEmpty(casts[i], "roleType"))) {
        case MAGENTA_CAST_ACTOR:
          if (cast != "")
            cast += EPG_STRING_TOKEN_SEPARATOR;
          cast += Utils::JsonStringOrEmpty(casts[i], "castName");
          break;
        case MAGENTA_CAST_DIRECTOR:
          if (director != "")
            director += EPG_STRING_TOKEN_SEPARATOR;
          director += Utils::JsonStringOrEmpty(casts[i], "castName");
          break;
        case MAGENTA_CAST_PRODUCER:
          break;
        case MAGENTA_CAST_WRITER:
          if (writer != "")
            writer += EPG_STRING_TOKEN_SEPARATOR;
          writer += Utils::JsonStringOrEmpty(casts[i], "castName");
          break;
        case MAGENTA_CAST_MODERATOR:
          if (cast != "")
            cast += EPG_STRING_TOKEN_SEPARATOR;
          cast += Utils::JsonStringOrEmpty(casts[i], "castName");
          break;
        default:
          kodi::Log(ADDON_LOG_DEBUG, "Unknown Cast Type: %i CastName: %s", stoi(Utils::JsonStringOrEmpty(casts[i], "roleType")), Utils::JsonStringOrEmpty(casts[i], "castName").c_str());
      }
    }
    tag.SetCast(cast);
    tag.SetDirector(director);
    tag.SetWriter(writer);
  }
  if (epgItem.HasMember("externalIds")) {
    std::string externalIds = Utils::JsonStringOrEmpty(epgItem, "externalIds");

    rapidjson::Document externalId;
    externalId.Parse(externalIds.c_str());
    if (externalId.GetParseError())
    {
      kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing externalIds");
    } else {
      for (rapidjson::SizeType i = 0; i < externalId.Size(); i++)
      {
        if (Utils::JsonStringOrEmpty(externalId[i], "type") == "imdb")
        {
  //        kodi::Log(ADDON_LOG_DEBUG, "Setting IMDB ID: %s", Utils::JsonStringOrEmpty(externalId[i], "id").c_str());
          tag.SetIMDBNumber(Utils::JsonStringOrEmpty(externalId[i], "id"));
        }
      }
    }
  }
  if (epgItem.HasMember("producedate"))
  {
    std::string producedate = Utils::JsonStringOrEmpty(epgItem, "producedate");
    try {
      tag.SetYear(std::stoi(producedate.substr(0,4)));
    } catch (const std::exception& e) {}
//    kodi::Log(ADDON_LOG_DEBUG, "Produce Date %s Sub %s Setting Year to %i", producedate.c_str(), producedate.substr(0,4).c_str(), std::stoi(producedate.substr(0,4)));
  }
  if (epgItem.HasMember("episodeInformation")) {
    std::string episodeInformation = Utils::JsonStringOrEmpty(epgItem, "episodeInformation");

    rapidjson::Document episodeInfo;
    episodeInfo.Parse(episodeInformation.c_str());
    if (episodeInfo.GetParseError())
    {
      kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing episodeInformation");
    } else {
      bool isNew = (Utils::JsonStringOrEmpty(episodeInfo, "seriesPremiere") == "1") ? true : false;
      bool isPremiere = (Utils::JsonStringOrEmpty(episodeInfo, "seasonPremiere") == "1") ? true : false;
      bool isFinale = (Utils::JsonStringOrEmpty(episodeInfo, "seasonFinale") == "1") ? true : false;
      if (isNew)
        epg_tag_flags += EPG_TAG_FLAG_IS_NEW;
      if (isPremiere)
        epg_tag_flags += EPG_TAG_FLAG_IS_PREMIERE;
      if (isFinale)
        epg_tag_flags += EPG_TAG_FLAG_IS_FINALE;
    }
  }
  if (epgItem.HasMember("seriesID")) {
    tag.SetSeriesLink(Utils::JsonStringOrEmpty(epgItem, "seriesID"));
  }
  tag.SetFlags(epg_tag_flags);
  //  kodi::Log(ADDON_LOG_DEBUG, "finished: [%s]", __FUNCTION__);
  return true;
}

void CPVRMagenta::FillEPGDetails(const rapidjson::Value& epgItem, kodi::addon::PVREPGTag& tag)
{

}

PVR_ERROR CPVRMagenta::GetEPGForChannel(int channelUid,
                                     time_t start,
                                     time_t end,
                                     kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_isMagenta2)
    return m_magenta2->GetEPGForChannel(channelUid, start, end, results);
/*
  std::string url2 = "https://appepmfk10088.prod.sngtv.t-online.de:33227/EPG/JSON/GetDataVersion";
  int statusCode2 = 0;
  std::string postData2 = "{\"contentType\": \"PROGRAM\","
	                           "\"contentIds\":\"373,408,404,381,374,385,405,382,403,387,54,392,375,376,371,48,391,4375,407,386,370,377,"
                             "368,384,379,398,393,397,5170,5171,5169,5175,5173,5174,5078,3757,389,5027,5026,401,394,402,452,4777,50,"
                             "5394,429,5206,3583,5476,5477,396,395,542,5329,338,39,380,383,601,3930,5399,479,378,485,5077,5840,482,"
                             "487,573,5295,553,400,60,5505,25,28,5312,5331,5387,5388,5390,5389,5391,5397,5398,5400,4724,5176,451,446,"
                             "5172,486,5315,5330,399,5601,5602,6002,6003,5316,5386,5314,616,617,255,110,157,137,259,216,133,181,184,"
                             "53,185,148,126,221,117,661,209,260,227,224,188,571,105,106,23,153,162,35,569,45,4406,186,108,18,604,44,"
                             "191,112,32,228,34,218,5075,550,548,549,648,645,647,646,623,626,624,625,627,629,628,367,366,369,3686,194,"
                             "637,5507,5508,483,475,472,476,474,473,4377,136,21,29,3826,440,4378,215,3827,439,236,121,43,46,210,5577,"
                             "576,201,264,4925,3756,4924,5143,5142,465,471,462,458,461,467,468,5141,3843,4379,4405,4512,195,263,4356,"
                             "40,572,3844,235,200,540,190,31,42,4889,5555,5556,198,5558,206,211,199,232,234,231,258,219,237,5557,192,"
                             "212,189,111,114,113,127,261,187,5554,115\","
	                           "\fromDate\": \"20230714171153\","
	                           "\toDate\": \"20230715215959\"}";

  std::string jsonEpg2 = m_httpClient->HttpPost(url2, postData2, statusCode2);
  kodi::Log(ADDON_LOG_DEBUG, "GetDataVersion returned: code: %i %s", statusCode2, jsonEpg2.c_str());
*/
  rapidjson::Document epgDoc;

  if (!GetEPGPlaybill(channelUid, start, end, epgDoc))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& epgitems = epgDoc["playbilllist"];

  kodi::Log(ADDON_LOG_DEBUG, "[epg] iterate entries");
  for (rapidjson::SizeType i = 0; i < epgitems.Size(); i++)
//  for (rapidjson::Value::ConstValueIterator itr1 = epgitems.Begin();
//        itr1 != epgitems.End(); ++itr1)
  {
    kodi::addon::PVREPGTag tag;
//    if (FillEPGTag((*itr1), tag))
    if (FillEPGTag(epgitems[i], tag))
      results.Add(tag);
  }
  return PVR_ERROR_NO_ERROR;
}

bool CPVRMagenta::UpdateEPGEvent(const kodi::addon::PVREPGTag& oldtag)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  rapidjson::Document epgDoc;
  kodi::addon::PVREPGTag newtag = oldtag;

  if (!GetEPGPlaybill(oldtag.GetUniqueChannelId(), oldtag.GetStartTime(), oldtag.GetEndTime(), epgDoc))
    return false;

  const rapidjson::Value& epgitems = epgDoc["playbilllist"];

  for (rapidjson::Value::ConstValueIterator itr1 = epgitems.Begin();
        itr1 != epgitems.End(); ++itr1)
  {
    const rapidjson::Value& epgItem = (*itr1);

    std::string externalContentCode = Utils::JsonStringOrEmpty(epgItem, "externalContentCode");

    kodi::Log(ADDON_LOG_DEBUG, "Content Code: %s", externalContentCode.c_str());

    rapidjson::Document epgDoc2;

    if (!GetEPGDetails(externalContentCode, epgDoc2))
      return false;

    const rapidjson::Value& detailitems = epgDoc2["playbilllist"];
    for (rapidjson::Value::ConstValueIterator itr2 = detailitems.Begin();
          itr2 != detailitems.End(); ++itr2)
    {
      FillEPGDetails((*itr2), newtag);
    }
  }

  kodi::addon::CInstancePVRClient::EpgEventStateChange(newtag, EPG_EVENT_UPDATED);
  return true;
}

PVR_ERROR CPVRMagenta::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_isMagenta2)
    return m_magenta2->IsEPGTagPlayable(tag, bIsPlayable);

  bIsPlayable = false;

//  UpdateEPGEvent(tag);

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      auto current_time = time(NULL);
      time_t startTime = tag.GetStartTime();
      if ((current_time > startTime) && (current_time - TIMEBUFFER < startTime))
      {
        bIsPlayable = true;
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetEPGTagEdl(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVREDLEntry>& edl)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  //cut before
  uint64_t elapsedTime = (time(NULL) - tag.GetStartTime());
  uint64_t edBreak = TIMEBUFFER > elapsedTime ? TIMEBUFFER - elapsedTime : 0;

  kodi::addon::PVREDLEntry entry;
  entry.SetStart(0);
  entry.SetEnd(edBreak * 1000);
  entry.SetType(PVR_EDL_TYPE_COMBREAK);
  edl.emplace_back(entry);

  //cut after
  if (time(NULL) > tag.GetEndTime()) {
      elapsedTime = (time(NULL) - tag.GetEndTime());
      edBreak = TIMEBUFFER > elapsedTime ? TIMEBUFFER - elapsedTime : TIMEBUFFER;
      entry.SetStart(edBreak * 1000);
      entry.SetEnd(TIMEBUFFER * 1000);
      entry.SetType(PVR_EDL_TYPE_COMBREAK);
      edl.emplace_back(entry);
  }

  return PVR_ERROR_NO_ERROR;
}

bool CPVRMagenta::ReleaseCurrentMedia()
{
  if ((m_currentMediaId == -1) || (m_currentChannelId == -1))
    return true;

  std::string url = m_epg_https_url + "ReleasePlaySession";
  std::string postData = "{\"mediaId\": \"" + std::to_string(m_currentMediaId) + "\","
                           "\"contentId\": \"" + std::to_string(m_currentChannelId) + "\","
                           "\"contentType\": \"VIDEO_CHANNEL\"}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return false;
  }
  return true;
}

std::string CPVRMagenta::GetPlay(const int& chanId, const int& mediaId, const bool isTimeshift)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  ReleaseCurrentMedia();

  std::string checksum = hmac<SHA256>(std::to_string(chanId), m_session_key);
  kodi::Log(ADDON_LOG_DEBUG, "Checksum: %s", checksum.c_str());

  int statusCode = 0;
  std::string url = m_epg_https_url + "Play";
  std::string postData = "{\"contentid\": \"" + std::to_string(chanId) + "\","
                          "\"mediaid\": \"" + std::to_string(mediaId) + "\","
                          "\"playtype\": 2,"
                          "\"checksum\": \"" + checksum + "\"}";
//      kodi::Log(ADDON_LOG_DEBUG, "postData %s", postData.c_str());

  std::string jsonString = m_httpClient->HttpPost(url, postData, statusCode);

//      kodi::Log(ADDON_LOG_DEBUG, "Answer Play %s", jsonString.c_str());

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if ((doc.GetParseError()) || (!doc.HasMember("retcode"))) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to Play");
    return "";
  }
  if (Utils::JsonStringOrEmpty(doc, "retcode") != "0") {
    if (Utils::JsonStringOrEmpty(doc, "retcode") == "-2") {
      MagentaAuthenticate();
      checksum = hmac<SHA256>(std::to_string(chanId), m_session_key);
      std::string postData = "{\"contentid\": \"" + std::to_string(chanId) + "\","
                              "\"mediaid\": \"" + std::to_string(mediaId) + "\","
                              "\"playtype\": 2,"
                              "\"checksum\": \"" + checksum + "\"}";

      jsonString = m_httpClient->HttpPost(url, postData, statusCode);
      doc.Parse(jsonString.c_str());
      if ((doc.GetParseError()) || (!doc.HasMember("retcode"))) {
        kodi::Log(ADDON_LOG_ERROR, "Failed to Play");
        return "";
      }
      if (Utils::JsonStringOrEmpty(doc, "retcode") != "0") {
        kodi::Log(ADDON_LOG_ERROR, "Play returned not 0! %s", jsonString.c_str());
        return "";
      }
    } else {
      kodi::Log(ADDON_LOG_ERROR, "Play returned not 0! %s", jsonString.c_str());
      return "";
    }
  }
  std::string playUrl = Utils::JsonStringOrEmpty(doc, "url");
//      std::string playUrl = GetPlayUrl(channel, mediaId);
  if (playUrl.empty()) {
    kodi::Log(ADDON_LOG_ERROR, "Play url was empty");
    return "";
  }
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY url: %s", playUrl.c_str());
  std::vector<std::string> out;
  tokenize(playUrl, "|", out);
  std::string spliturl = out[isTimeshift ? 1 : 0];
/*
  for (auto &s: out) {
    kodi::Log(ADDON_LOG_DEBUG, "[PLAY Timeshifted] url: %s", s.c_str());
  }
*/
  std::string appendix = "&uid=" + m_userID + "&sid=" + m_sessionID + "&i=" + (isTimeshift ? "0" : "4") + "&dp=0";
  spliturl += appendix;

  return spliturl;
}

PVR_ERROR CPVRMagenta::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetEPGTagStreamProperties(tag, properties);

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {

      int chanId = channel.iUniqueId;
      int mediaId = SelectMediaId(channel, false);

      std::string playurl = GetPlay(chanId, mediaId, true);
      if (playurl.empty())
        return PVR_ERROR_FAILED;

      m_currentMediaId = mediaId;
      m_currentChannelId = chanId;
      kodi::Log(ADDON_LOG_DEBUG, "[PLAY Timeshifted] url: %s", playurl.c_str());
      SetStreamProperties(properties, playurl, false, true, false);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetProvidersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetProviders(kodi::addon::PVRProvidersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetChannelsAmount(amount);

  amount = m_channels.size();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Channels Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetChannels(bRadio, results);

  for (const auto& channel : m_channels)
  {

    if ((channel.bRadio == bRadio) && ((!m_settings->HideUnsubscribed()) || (HasStreamingUrl(channel))) && (!m_settings->IsHiddenDeactivated() || !channel.isHidden))
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.iUniqueId);
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(channel.iChannelNumber);
      kodiChannel.SetChannelName(channel.strChannelName);
      kodiChannel.SetIconPath(channel.strIconPath);
      kodiChannel.SetIsHidden(channel.isHidden);
      kodiChannel.SetHasArchive(false);

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetChannelStreamProperties(channel, properties);

  MagentaChannel addonChannel;
  if (!GetChannel(channel.GetUniqueId(), addonChannel))
    return PVR_ERROR_FAILED;

  m_currentMediaId = SelectMediaId(addonChannel, false);
  m_currentChannelId = addonChannel.iUniqueId;
  std::string streamUrl = GetPlayUrl(addonChannel, m_currentMediaId);

  kodi::Log(ADDON_LOG_DEBUG, "Stream URL -> %s", streamUrl.c_str());
  kodi::Log(ADDON_LOG_DEBUG, "ReferenceID -> %i", m_currentChannelId);
  SetStreamProperties(properties, streamUrl, true, false, false);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelGroupsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetChannelGroupsAmount(amount);

  amount = static_cast<int>(m_categories.size());
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Groups Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetChannelGroups(bRadio, results);

  std::vector<MagentaCategory>::iterator it;
  for (it = m_categories.begin(); it != m_categories.end(); ++it)
  {
    kodi::addon::PVRChannelGroup kodiGroup;

    if (bRadio == it->isRadio) {
      kodiGroup.SetPosition(it->position);
      kodiGroup.SetIsRadio(bRadio); /* is radio group */
      kodiGroup.SetGroupName(it->name);

      results.Add(kodiGroup);
      kodi::Log(ADDON_LOG_DEBUG, "Group added: %s at position %u", it->name.c_str(), it->position);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                           kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetChannelGroupMembers(group, results);

  for (const auto& cgroup : m_categories)
  {
    if (cgroup.name != group.GetGroupName())
      continue;

    unsigned int position = 0;
    for (const unsigned int& channelid : cgroup.channelids)
    {
      kodi::addon::PVRChannelGroupMember kodiGroupMember;

      kodiGroupMember.SetGroupName(group.GetGroupName());
      kodiGroupMember.SetChannelUniqueId(channelid);
      kodiGroupMember.SetChannelNumber(++position);

      results.Add(kodiGroupMember);
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

int CPVRMagenta::GetGroupTimersAmount()
{
  int amount = 0;
  for (const auto& current_timerGroup : m_timerGroups)
  {
    amount += current_timerGroup.groupRecordings.size();
  }
  return amount;
}

int CPVRMagenta::GetGroupRecordingsAmount()
{
  int amount = 0;
  for (const auto& current_recGroup : m_recGroups)
  {
    amount += current_recGroup.groupRecordings.size();
  }
  return amount;
}

PVR_ERROR CPVRMagenta::GetRecordingsAmount(bool deleted, int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetRecordingsAmount(deleted, amount);

  amount = static_cast<int>(m_recordings.size());
  amount += GetGroupRecordingsAmount();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Recordings Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

std::string CPVRMagenta::GetGenreFromId(const int& genreId)
{
  for (const auto& current_genre : m_genres)
  {
    if (current_genre.genreId == genreId) {
      return current_genre.genreName;
    }
  }
  return "";
}

void CPVRMagenta::FillPVRRecording(kodi::addon::PVRRecording& kodiRecording, const MagentaRecording& rec)
{
  kodiRecording.SetRecordingId(rec.pvrId);
  kodiRecording.SetTitle(rec.pvrName);
  kodiRecording.SetPlot(rec.introduce);
  kodiRecording.SetChannelName(rec.channelName);
  if (!rec.picture.empty()) {
    kodiRecording.SetIconPath(rec.picture);
    kodiRecording.SetThumbnailPath(rec.picture);
    kodiRecording.SetFanartPath(rec.picture);
  }
  if (rec.realRecordLength != 0) {
    kodiRecording.SetDuration(rec.realRecordLength);
  }
  kodiRecording.SetLastPlayedPosition(rec.bookmarkTime);
  kodiRecording.SetRecordingTime(Utils::StringToTime(PrepareTime(rec.beginTime)));
  KodiGenre myGenre = GetKodiGenreFromId(rec.genres[0]);
  if (myGenre.genreType != 0) {
    kodiRecording.SetGenreType(myGenre.genreType);
    kodiRecording.SetGenreSubType(myGenre.genreSubType);
  } else {
    kodiRecording.SetGenreType(EPG_GENRE_USE_STRING);
    std::string genreNames;
    for (int i = 0; i < rec.genres.size(); i++) {
      if (i != 0) {
        genreNames += ",";
      }
      genreNames += GetGenreFromId(rec.genres[i]);
    }
    kodiRecording.SetGenreDescription(genreNames);
  }
  kodiRecording.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);
  if (!rec.periodPVRTaskName.empty()) {
    kodiRecording.SetDirectory(rec.periodPVRTaskName);
  }
}

PVR_ERROR CPVRMagenta::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetRecordings(deleted, results);

  if (!GetTimersRecordings(true)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get recordings from backend");
    return PVR_ERROR_SERVER_ERROR;
  }

  for (const auto& current_recording : m_recordings)
  {
    kodi::addon::PVRRecording kodiRecording;

    FillPVRRecording(kodiRecording, current_recording);
    if (!current_recording.periodPVRTaskName.empty())
      kodiRecording.SetDirectory(current_recording.periodPVRTaskName);

    results.Add(kodiRecording);
    kodi::Log(ADDON_LOG_DEBUG, "Recording added: %s", current_recording.pvrName.c_str());
  }

  for (const auto& current_group : m_recGroups)
  {
    for (const auto& current_recording : current_group.groupRecordings)
    {
      kodi::addon::PVRRecording kodiRecording;

      FillPVRRecording(kodiRecording, current_recording);
      kodiRecording.SetDirectory(current_group.periodPVRTaskName);

      results.Add(kodiRecording);
      kodi::Log(ADDON_LOG_DEBUG, "Recording added: %s from group", current_recording.pvrName.c_str(), current_group.periodPVRTaskName.c_str());
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_isMagenta2)
    return m_magenta2->GetRecordingStreamProperties(recording, properties);

  for (const auto& current_recording : m_recordings)
  {
    if ( current_recording.pvrId != recording.GetRecordingId())
      continue;
/*
    if (!ReleaseCurrentMedia()) {
      return PVR_ERROR_FAILED;
    }
*/
    std::string checksum = hmac<SHA256>(std::to_string(current_recording.channelId), m_session_key);
    kodi::Log(ADDON_LOG_DEBUG, "Checksum: %s", checksum.c_str());

    int statusCode = 0;
    std::string url = m_epg_https_url + "AuthorizeAndPlay";
    std::string postData = "{\"contentType\": \"CHANNEL\","
	                          "\"businessType\": 8,"
	                          "\"contentId\": \"" + std::to_string(current_recording.channelId) + "\","
	                          "\"mediaId\": \"" + std::to_string(current_recording.mediaId) + "\","
	                          "\"pvrId\": \"" + current_recording.pvrId + "\","
	                          "\"checksum\": \"" + checksum + "\"}";

    std::string jsonString = m_httpClient->HttpPost(url, postData, statusCode);

    rapidjson::Document doc;
    doc.Parse(jsonString.c_str());
    if ((doc.GetParseError()) || (!doc.HasMember("retcode"))) {
      kodi::Log(ADDON_LOG_ERROR, "Failed to AuthorizeAndPlay");
      return PVR_ERROR_SERVER_ERROR;
    }
    if (Utils::JsonStringOrEmpty(doc, "retcode") != "0") {
      if (Utils::JsonStringOrEmpty(doc, "retcode") == "-2") {
        MagentaAuthenticate();
        checksum = hmac<SHA256>(std::to_string(current_recording.channelId), m_session_key);
        std::string postData = "{\"contentType\": \"CHANNEL\","
                                "\"businessType\": 8,"
                                "\"contentId\": \"" + std::to_string(current_recording.channelId) + "\","
                                "\"mediaId\": \"" + std::to_string(current_recording.mediaId) + "\","
                                "\"pvrId\": \"" + current_recording.pvrId + "\","
                                "\"checksum\": \"" + checksum + "\"}";
        jsonString = m_httpClient->HttpPost(url, postData, statusCode);
        doc.Parse(jsonString.c_str());
        if ((doc.GetParseError()) || (!doc.HasMember("retcode"))) {
          kodi::Log(ADDON_LOG_ERROR, "Failed to AuthorizeAndPlay");
          return PVR_ERROR_SERVER_ERROR;
        }
        if (Utils::JsonStringOrEmpty(doc, "retcode") != "0") {
          kodi::Log(ADDON_LOG_ERROR, "AuthorizeAndPlay returned not 0! %s", jsonString.c_str());
          return PVR_ERROR_SERVER_ERROR;
        }
      } else {
        kodi::Log(ADDON_LOG_ERROR, "AuthorizeAndPlay returned not 0! %s", jsonString.c_str());
        return PVR_ERROR_SERVER_ERROR;
      }
    }
    std::string playUrl = Utils::JsonStringOrEmpty(doc, "playUrl");
    if (playUrl.empty()) {
      kodi::Log(ADDON_LOG_ERROR, "Recording playUrl was empty");
      return PVR_ERROR_SERVER_ERROR;
    }
    kodi::Log(ADDON_LOG_DEBUG, "[PLAY RECORDING] url: %s", playUrl.c_str());

    SetStreamProperties(properties, playUrl, false, false, false);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::DeletePVR(const std::string pvrId, const bool isRecording)
{
  kodi::Log(ADDON_LOG_DEBUG, "Request to delete ID: [%s]", pvrId.c_str());
  std::string url = m_epg_https_url + "DeletePVR";
  std::string postData = "{\"pvrId\": \"" + pvrId + "\"}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return PVR_ERROR_FAILED;
  }
  else {
    if (isRecording) {
      kodi::QueueNotification(QUEUE_INFO, "Aufnahme", "Aufnahme gelöscht");
      kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
    } else {
      kodi::QueueNotification(QUEUE_INFO, "Timer", "Timer gelöscht");
      kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  return DeletePVR(recording.GetRecordingId(), true);
}

namespace
{
struct TimerType : kodi::addon::PVRTimerType
{
  TimerType(int defaultLifetimeValue,
            unsigned int id,
            unsigned int attributes,
            const std::string& description,
  //          const std::vector<kodi::addon::PVRTypeIntValue>& priorityValues =
  //              std::vector<kodi::addon::PVRTypeIntValue>(),
            const std::vector<kodi::addon::PVRTypeIntValue>& lifetimeValues =
                std::vector<kodi::addon::PVRTypeIntValue>())
  //          const std::vector<kodi::addon::PVRTypeIntValue>& dupEpisodesValues =
  //              std::vector<kodi::addon::PVRTypeIntValue>())
  {
    SetId(id);
    SetAttributes(attributes);
    SetDescription(description);
//    SetPriorities(priorityValues, settings->GetDvrPriority());
    SetLifetimes(lifetimeValues, defaultLifetimeValue);
//    SetPreventDuplicateEpisodes(dupEpisodesValues, settings->GetDvrDupdetect());
  }
};

} // unnamed namespace

bool CPVRMagenta::UpdateBookmarks()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_epg_https_url + "QueryBookmark";
  std::string postData = "{\"bookmarkType\": " + std::to_string(MAGENTA_BOOKMARK_RECORDING) + ","
                          "\"count\": -1}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc) || !doc.HasMember("bookmarkList")) {
    return false;
  }
  else {
    const rapidjson::Value& bookmarkList = doc["bookmarkList"];

    for (rapidjson::Value::ConstValueIterator itr2 = bookmarkList.Begin();
        itr2 != bookmarkList.End(); ++itr2)
    {
      const rapidjson::Value& bookmarkItem = (*itr2);

      for (auto& recording : m_recordings)
      {
        if ( Utils::JsonStringOrEmpty(bookmarkItem, "contentId") == recording.pvrId)
        {
          recording.bookmarkTime = stoi(Utils::JsonStringOrEmpty(bookmarkItem, "rangeTime"));
          kodi::Log(ADDON_LOG_DEBUG, "Updating position %i for Recording ID: %s", recording.bookmarkTime, recording.pvrId.c_str());
        }
      }
    }
  }
  return true;
}

PVR_ERROR CPVRMagenta::SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording,
    int lastplayedposition)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  kodi::Log(ADDON_LOG_DEBUG, "Setting position %i for Recording ID: %s", lastplayedposition, recording.GetRecordingId().c_str());

  std::string url = m_epg_https_url + "AddBookmark";
  std::string postData = "{\"bookmarkList\": [{"
  		                    "\"contentId\": \"" + recording.GetRecordingId() + "\","
  		                    "\"bookmarkType\": " + std::to_string(MAGENTA_BOOKMARK_RECORDING) + ","
  		                    "\"rangeTime\": " + std::to_string(lastplayedposition) + "}]}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return PVR_ERROR_FAILED;
  }
  UpdateBookmarks();

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position)
{
  position = recording.GetLastPlayedPosition();
  kodi::Log(ADDON_LOG_DEBUG, "Returning position %i for Recording ID: %s", position, recording.GetRecordingId().c_str());

  return PVR_ERROR_NO_ERROR;
}

void CPVRMagenta::GetLifetimeValues(std::vector<kodi::addon::PVRTypeIntValue>& lifetimeValues, const bool& isSeries) const
{
  if (isSeries) {
    lifetimeValues = {
      {MAGENTA_RECORDING_DELETE_MODE_WHEN_FULL, kodi::addon::GetLocalizedString(30050)},
      {MAGENTA_RECORDING_DELETE_MODE_MANUAL, kodi::addon::GetLocalizedString(30051)},
      {MAGENTA_RECORDING_DELETE_MODE_KEEP_FIVE, kodi::addon::GetLocalizedString(30052)},
      {MAGENTA_RECORDING_DELETE_MODE_KEEP_TEN, kodi::addon::GetLocalizedString(30053)}
    };
  } else {
    lifetimeValues = {
      {MAGENTA_RECORDING_DELETE_MODE_WHEN_FULL, kodi::addon::GetLocalizedString(30050)},
      {MAGENTA_RECORDING_DELETE_MODE_MANUAL, kodi::addon::GetLocalizedString(30051)}
    };
  }
}

PVR_ERROR CPVRMagenta::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  /* PVR_Timer.iLifetime values and presentation.*/
  std::vector<kodi::addon::PVRTypeIntValue> lifetimeValues;
  GetLifetimeValues(lifetimeValues, false);

  unsigned int TIMER_ONCE_EPG_ATTRIBS =
       PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME |
       PVR_TIMER_TYPE_SUPPORTS_END_TIME | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
       PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN | PVR_TIMER_TYPE_SUPPORTS_LIFETIME;

   /* One-shot epg based */
  types.emplace_back(TimerType(
    /* Settings */
//         m_settings,
    m_settings->GetDeleteMode(),
    /* Type id. */
    TIMER_ONCE_EPG,
    /* Attributes. */
    TIMER_ONCE_EPG_ATTRIBS,
    /* Let Kodi generate the description. */
    "Einzelaufnahme",
//         /* Values definitions for priorities. */
//         priorityValues,
         /* Values definitions for lifetime. */
    lifetimeValues));

  unsigned int TIMER_SERIES_EPG_ATTRIBS =
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME |
//      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN | PVR_TIMER_TYPE_SUPPORTS_LIFETIME |
      PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME | PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME |
      PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_REQUIRES_EPG_SERIESLINK_ON_CREATE;

  GetLifetimeValues(lifetimeValues, true);

      /* Series epg based */
  types.emplace_back(TimerType(
       /* Settings */
   //         m_settings,
  m_settings->GetDeleteModeSeries(),
  /* Type id. */
  TIMER_SERIES_EPG,
  /* Attributes. */
  TIMER_SERIES_EPG_ATTRIBS,
  /* Let Kodi generate the description. */
  "Serienaufnahme",
   //         /* Values definitions for priorities. */
   //         priorityValues,
            /* Values definitions for lifetime. */
   lifetimeValues));

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetTimersAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = static_cast<int>(m_timers.size());
  amount += GetGroupTimersAmount();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Timers Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (!GetTimersRecordings(false)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get timers from backend");
    return PVR_ERROR_SERVER_ERROR;
  }

  for (const auto& timerGroup : m_timerGroups)
  {
    // add group
    kodi::addon::PVRTimer tagGroup;
    tagGroup.SetTimerType(TIMER_SERIES_EPG);
    tagGroup.SetState(PVR_TIMER_STATE_SCHEDULED);
    tagGroup.SetTitle(timerGroup.periodPVRTaskName);
    tagGroup.SetClientIndex(static_cast<unsigned int>(timerGroup.index));
    tagGroup.SetClientChannelUid(timerGroup.channelId);
    tagGroup.SetMarginStart(timerGroup.beginOffset);
    tagGroup.SetMarginEnd(timerGroup.endOffset);
    int deleteMode = timerGroup.deleteMode;
    if ((deleteMode == 2) && (timerGroup.latestSeriesNum == 10))
      deleteMode = 3;
    tagGroup.SetLifetime(deleteMode);
    if ((timerGroup.timeMode == 2) && (!timerGroup.selectedBeginTime.empty()))
    {
      std::string beginTime = timerGroup.selectedBeginTime;
      beginTime = "19700102" + beginTime;
      time_t startTime = Utils::StringToTime(PrepareTime(beginTime)) - 3600;
      tagGroup.SetStartTime(startTime);
    } else {
      tagGroup.SetStartAnyTime(true);
    }
    tagGroup.SetEndAnyTime(true);
//    tagGroup.SetRecordingGroup(static_cast<unsigned int>(timerGroup.seriesId));
    tagGroup.SetSeriesLink(std::to_string(timerGroup.seriesId));

    results.Add(tagGroup);

    kodi::Log(ADDON_LOG_DEBUG, "Timer Group added: %s for seriesId: %i with index: %i", timerGroup.periodPVRTaskName.c_str(), timerGroup.seriesId, timerGroup.index);
  }
  std::vector<MagentaRecording>::iterator it;
  for (it = m_timers.begin(); it != m_timers.end(); ++it)
  {
    kodi::addon::PVRTimer kodiTimer;

    kodiTimer.SetClientIndex(static_cast<unsigned int>(it->index));
    kodiTimer.SetState(PVR_TIMER_STATE_SCHEDULED);
    kodiTimer.SetTitle(it->pvrName);
    kodiTimer.SetStartTime(Utils::StringToTime(PrepareTime(it->beginTime)));
    kodiTimer.SetEndTime(Utils::StringToTime(PrepareTime(it->endTime)));
    kodiTimer.SetMarginStart(it->beginOffset);
    kodiTimer.SetMarginEnd(it->endOffset);
    kodiTimer.SetSummary(it->introduce);
    kodiTimer.SetLifetime(it->deleteMode);
    kodiTimer.SetEPGUid(std::stoi(it->programId));
    kodiTimer.SetClientChannelUid(it->channelId);
    kodiTimer.SetTimerType(TIMER_ONCE_EPG);
    if (!it->periodPVRTaskName.empty()) {
//      kodiTimer.SetRecordingGroup(static_cast<unsigned int>(it->seriesId));
//      kodiTimer.SetDirectory(it->periodPVRTaskName);
      kodiTimer.SetSeriesLink(std::to_string(it->seriesId));
      kodiTimer.SetParentClientIndex(it->groupIndex);
    } else {
      kodiTimer.SetParentClientIndex(PVR_TIMER_NO_PARENT);
    }

//    kodiTimer.SetPlot(it->introduce);
//    kodiTimer.SetChannelName(it->channelName);
//    kodiTimer.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);

    results.Add(kodiTimer);
    kodi::Log(ADDON_LOG_DEBUG, "Timer added: %s, ProgramID; %s", it->pvrName.c_str(), it->programId.c_str());
  }

  return PVR_ERROR_NO_ERROR;
}

std::string CPVRMagenta::GetPeriodPVRPayload(const MagentaChannel& channel, const kodi::addon::PVRTimer& timer, const std::string& periodPVRTaskId, const bool& isUpdate)
{
  std::string payload;
  int latestSeriesNum = 0;
  int deleteMode = timer.GetLifetime();
  if (deleteMode == 2)
    latestSeriesNum = 5;
  if (deleteMode == 3) {
    latestSeriesNum = 10;
    deleteMode = 2;
  }

  payload = "{\"action\": \"";
  payload += (isUpdate) ? "UPDATE" : "ADD";
  payload += "\",\"conflictCheckType\": 1,"
                "\"strategyType\": 0,"
                "\"task\": {"
                            "\"mediaId\": \"" + std::to_string(SelectMediaId(channel, true)) + "\","
                            "\"beginOffset\": " + std::to_string(timer.GetMarginStart()) + ","
                            "\"deleteMode\": " + std::to_string(deleteMode) + ","
                            "\"endOffset\": " + std::to_string(timer.GetMarginEnd()) + ","
                            "\"strategyType\": 0,"
                            "\"type\": " + std::to_string(MAGENTA_RECORDING_TYPE_CLOUD) + ",";
  if (!periodPVRTaskId.empty()) {
    payload += "\"periodPVRTaskId\": \"" + periodPVRTaskId + "\",";
  }
  if (deleteMode == 2) {
    payload += "\"latestSeriesNum\": " + std::to_string(latestSeriesNum) + ",";
  }
  int timeMode = MAGENTA_TIMER_TIMEMODE_ANY;
  if (!timer.GetStartAnyTime())
  {
    timeMode = MAGENTA_TIMER_TIMEMODE_START;
    std::string starttime = Utils::TimeToString(timer.GetStartTime()).substr(8,6);
    payload += "\"selectedBeginTime\": \"" + starttime + "\",";
  }
  payload += "\"playbillID\": \"" + std::to_string(timer.GetEPGUid()) + "\","
             "\"seriesID\": \"" + timer.GetSeriesLink() + "\","
             "\"seriesType\": \"2\","
             "\"timeMode\": \"" + std::to_string(timeMode) + "\"}}";

  return payload;
}

PVR_ERROR CPVRMagenta::AddTimer(const kodi::addon::PVRTimer& timer)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url;
  std::string postData;
  rapidjson::Document doc;

  MagentaChannel addonChannel;
  if (!GetChannel(timer.GetClientChannelUid(), addonChannel))
    return PVR_ERROR_FAILED;

  switch (timer.GetTimerType()) {
    case TIMER_ONCE_EPG:
      url = m_epg_https_url + "AddPVR";
      postData = "{\"mediaId\": \"" + std::to_string(SelectMediaId(addonChannel, true)) + "\","
                  "\"beginOffset\": " + std::to_string(timer.GetMarginStart()) + ","
                  "\"deleteMode\": " + std::to_string(m_settings->GetDeleteMode()) + ","
                  "\"endOffset\": " + std::to_string(timer.GetMarginEnd()) + ","
                  "\"strategyType\": 0,"
                  "\"type\": " + std::to_string(MAGENTA_RECORDING_TYPE_CLOUD) + ","
                  "\"programId\": \"" + std::to_string(timer.GetEPGUid()) + "\"}";
  //      kodi::Log(ADDON_LOG_DEBUG, "PostData: %s", postData.c_str());
      if (!JsonRequest(url, postData, doc)) {
        return PVR_ERROR_SERVER_ERROR;
      }
      else {
        kodi::Log(ADDON_LOG_DEBUG, "Added single timer for PVRID: %s", Utils::JsonStringOrEmpty(doc, "pvrId").c_str());
        kodi::QueueNotification(QUEUE_INFO, "Aufnahme", "Einzelaufnahme programmiert");
      }
      break;
    case TIMER_SERIES_EPG:
      kodi::Log(ADDON_LOG_DEBUG, "Add Series Timer");

      url = m_epg_https_url + "PeriodPVRMgmt";
      postData = GetPeriodPVRPayload(addonChannel, timer, "", false);

      if (!JsonRequest(url, postData, doc)) {
        return PVR_ERROR_SERVER_ERROR;
      }
      else {
        kodi::Log(ADDON_LOG_DEBUG, "Added series timer for periodPVRTaskId: %s", Utils::JsonStringOrEmpty(doc, "periodPVRTaskId").c_str());
        kodi::QueueNotification(QUEUE_INFO, "Aufnahme", "Serienaufnahme programmiert");
      }
      break;
    default:
      kodi::Log(ADDON_LOG_DEBUG, "Unknown Timer Type");
      return PVR_ERROR_FAILED;
  }

  kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
  auto current_time = time(NULL);
  if (current_time > timer.GetStartTime()) {
    kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url;
  std::string postData;
  rapidjson::Document doc;

  if (timer.GetTimerType() == TIMER_ONCE_EPG)
  {
    for (const auto& mytimer : m_timers)
    {
      if (timer.GetClientIndex() != mytimer.index)
        continue;

      url = m_epg_https_url + "UpdatePVR";
      postData = "{\"mediaId\": \"" + std::to_string(mytimer.mediaId) + "\","
                  "\"beginOffset\": " + std::to_string(timer.GetMarginStart()) + ","
                  "\"deleteMode\": " + std::to_string(timer.GetLifetime()) + ","
                  "\"endOffset\": " + std::to_string(timer.GetMarginEnd()) + ","
                  "\"strategyType\": 0,"
                  "\"type\": " + std::to_string(MAGENTA_RECORDING_TYPE_CLOUD) + ","
                  "\"pvrId\": \"" + mytimer.pvrId + "\","
                  "\"beginTime\": \"" + mytimer.beginTime + "\","
                  "\"endTime\": \"" + mytimer.endTime + "\","
                  "\"programId\": \"" + std::to_string(timer.GetEPGUid()) + "\"}";
  //      kodi::Log(ADDON_LOG_DEBUG, "PostData: %s", postData.c_str());
      if (!JsonRequest(url, postData, doc)) {
        return PVR_ERROR_SERVER_ERROR;
      } else {
        kodi::Log(ADDON_LOG_DEBUG, "Updated timer for PVRID: %s", mytimer.pvrId.c_str());
        kodi::QueueNotification(QUEUE_INFO, "Aufnahme", "Aufnahme geändert");
      }
    }
  }
  else if (timer.GetTimerType() == TIMER_SERIES_EPG) {
    for (const auto& mytimer : m_timerGroups)
    {
      if (timer.GetClientIndex() != mytimer.index)
        continue;

      MagentaChannel addonChannel;
      if (!GetChannel(timer.GetClientChannelUid(), addonChannel))
        return PVR_ERROR_FAILED;

      url = m_epg_https_url + "PeriodPVRMgmt";
      postData = GetPeriodPVRPayload(addonChannel, timer, mytimer.periodPVRTaskId, true);
      if (!JsonRequest(url, postData, doc)) {
        return PVR_ERROR_SERVER_ERROR;
      } else {
        kodi::Log(ADDON_LOG_DEBUG, "Updated series timer for periodPVRTaskId: %s", mytimer.periodPVRTaskId.c_str());
        kodi::QueueNotification(QUEUE_INFO, "Aufnahme", "Serienaufnahme geändert");
      }
    }
  }

  kodi::addon::CInstancePVRClient::TriggerTimerUpdate();

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::DeleteTimer(const kodi::addon::PVRTimer& timer, bool)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (timer.GetTimerType() == TIMER_ONCE_EPG)
  {
    for (const auto& thisTimer : m_timers)
    {
      if (thisTimer.index == timer.GetClientIndex())
      {
        return DeletePVR(thisTimer.pvrId, false);
      }
    }
  } else if (timer.GetTimerType() == TIMER_SERIES_EPG) {
    for (const auto& mytimer : m_timerGroups)
    {
      if (timer.GetClientIndex() != mytimer.index)
        continue;

      auto current_time = time(NULL);
      std::string url = m_epg_https_url + "PeriodPVRMgmt";
      std::string postData = "{\"action\": \"UPDATE\","
                              "\"task\": {"
                                          "\"overtime\": " + Utils::TimeToString(current_time) + ","
                                          "\"periodPVRTaskId\": \"" + mytimer.periodPVRTaskId + "\"}}";
      rapidjson::Document doc;
      if (!JsonRequest(url, postData, doc)) {
        return PVR_ERROR_SERVER_ERROR;
      } else {
        kodi::QueueNotification(QUEUE_INFO, "Timer", "Serientimer gelöscht");
        kodi::addon::CInstancePVRClient::TriggerTimerUpdate();

        //TODO: {"action":"DELETE","task":{"periodPVRTaskId":"xyz"}} if all recordings are deleted

        return PVR_ERROR_NO_ERROR;
      }
    }
  }

  return PVR_ERROR_FAILED;
}

bool CPVRMagenta::GetChannel(const int& channelUid, MagentaChannel& myChannel)
{
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

  return false;
}

ADDONCREATOR(CPVRMagenta)
