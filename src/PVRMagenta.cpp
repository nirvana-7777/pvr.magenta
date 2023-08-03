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
#include "Base64.h"

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
    kodi::Log(ADDON_LOG_ERROR, "JSON Request return message: %s", Utils::JsonStringOrEmpty(doc, "retmsg").c_str());
  }
  if (Utils::JsonStringOrEmpty(doc, "retcode") != "0")
  {
    kodi::Log(ADDON_LOG_ERROR, "JsonRequest returned not 0 with url %s, and body %s", url.c_str(), postData.c_str());
    kodi::Log(ADDON_LOG_ERROR, "JsonRequest returned %s", result.c_str());
    return false;
  }
  return true;
}

bool CPVRMagenta::MagentaGuestLogin()
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
    m_epg_https_url = Utils::JsonStringOrEmpty(doc, "epghttpsurl");
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

bool CPVRMagenta::MagentaDTAuthenticate()
{
  int statusCode = 0;
  std::string m_device_id = m_settings->GetMagentaDeviceID();
  if (m_device_id.empty()) {
    m_device_id = Utils::CreateUUID();
    m_settings->SetSetting("deviceid", m_device_id);
  }
  kodi::Log(ADDON_LOG_DEBUG, "Current DeviceID %s", m_device_id.c_str());

  std::ostringstream convert;
  for (int i = 0; i < block_size; i++) {
      convert << (uint8_t) rand();
  }
  m_cnonce = string_to_hex(convert.str());
  std::transform(m_cnonce.begin(), m_cnonce.end(), m_cnonce.begin(), ::tolower);

  std::string url = m_epg_https_url + "/EPG/JSON/DTAuthenticate";
  std::string postData = "{\"accessToken\": \"" + m_settings->GetMagentaEPGToken() +
                          "\", \"usergroup\": \"" + USER_GROUP +
                          "\", \"connectType\": 1" +
	                        ", \"userType\": \"1\"" +
                          ", \"terminalid\": \"" + m_device_id +
	                        "\", \"mac\": \"" + m_device_id +
	                        "\", \"terminaltype\": \"" + TERMINALTYPE +
                          "\", \"utcEnable\": 1" +
                          ", \"timezone\": \"Europe/Berlin" +
                          "\", \"caDeviceInfo\": [{" +
		                           "\"caDeviceType\": 6" +
		                           ", \"caDeviceId\": \"" + m_device_id + "\"}]," +
	                        "\"terminalDetail\": [{" +
		                          "\"key\": \"GUID\"," +
		                          "\"value\": \"" + m_device_id + "\"}, {" +
                              "\"key\": \"HardwareSupplier\"," +
                              "\"value\": \"" + HARDWARESUPPLIER + "\"}, {" +
                              "\"key\": \"DeviceClass\"," +
                              "\"value\": \"TV\"}, {" +
                              "\"key\": \"DeviceStorage\"," +
                              "\"value\": \"12495872000\"}, {" +
		                          "\"key\": \"DeviceStorageSize\"," +
		                          "\"value\": \"12495872000\"}]," +
	                        "\"softwareVersion\": \"11\"," +
	                        "\"osversion\": \"7825230_3167.5736\"," +
	                        "\"terminalvendor\": \"" + TERMINALVENDOR + "\"," +
	                        "\"preSharedKeyID\": \"" + base64_decode(psk_id1) + "\"," +
	                        "\"cnonce\": \"" + m_cnonce + "\"," +
	                        "\"areaid\": \"1\"," +
	                        "\"templatename\": \"NGTV\"," +
	                        "\"subnetId\": \"4901\"}";
//  kodi::Log(ADDON_LOG_DEBUG, "PostData %s", postData.c_str());

  std::string jsonString = m_httpClient->HttpPost(url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to Authenticate");
    return false;
  }
//  kodi::Log(ADDON_LOG_DEBUG, "Magenta Authenticate returned: %s", jsonString.c_str());
  if (!doc.HasMember("retcode")) {
    kodi::Log(ADDON_LOG_DEBUG, "Failed to authenticate - np retcode received");
    return false;
  }
  std::string retcode = Utils::JsonStringOrEmpty(doc, "retcode");
  if (retcode == "0") {
    m_settings->SetSetting("csrftoken", Utils::JsonStringOrEmpty(doc, "csrfToken"));
    const rapidjson::Value& ca_verimatrix = doc["ca"]["verimatrix"];
    m_licence_url = Utils::JsonStringOrEmpty(ca_verimatrix, "multiRightsWidevine");
    const rapidjson::Value& ca_device = doc["caDeviceInfo"][0];
    m_ca_device_id = Utils::JsonStringOrEmpty(ca_device, "VUID");
    m_userID = Utils::JsonStringOrEmpty(doc, "userID");
    m_sessionID = Utils::JsonStringOrEmpty(doc, "sessionid");
    m_encryptToken = Utils::JsonStringOrEmpty(doc, "encryptToken");
    m_userContentFilter = Utils::JsonStringOrEmpty(doc, "userContentFilter");
    m_userContentListFilter = Utils::JsonStringOrEmpty(doc, "userContentListFilter");

    std::string key = base64_decode(psk_id2) + m_userID + m_encryptToken + m_cnonce;
    kodi::Log(ADDON_LOG_DEBUG, "Key: %s", key.c_str());

    SHA256 sha256;
    m_session_key  = sha256(key);
    std::transform(m_session_key.begin(), m_session_key.end(), m_session_key.begin(), ::toupper);

    kodi::Log(ADDON_LOG_DEBUG, "Session key: %s", m_session_key.c_str());
  } else {
    kodi::Log(ADDON_LOG_DEBUG, "Failed to authenticate with message: %s", Utils::JsonStringOrEmpty(doc, "retmsg").c_str());
    return false;
  }
  return true;
}

bool CPVRMagenta::MagentaAuthenticate()
{
  if (!MagentaDTAuthenticate()) {
    int statusCode = 0;
    std::string url = m_sam_service_url + "/oauth2/tokens";
    std::string postData = "client_id=" + CLIENT_ID + "&scope=ngtvepg offline_access&grant_type=refresh_token&refresh_token=" + m_settings->GetMagentaRefreshToken();

    std::string jsonString = m_httpClient->HttpPost(url, postData, statusCode);

    rapidjson::Document doc;
    doc.Parse(jsonString.c_str());
    if ((doc.GetParseError()) || (statusCode != 200))
    {
      url = m_sam_service_url + "/oauth2/bc-auth/start";
      postData = "client_id=" + CLIENT_ID + "&scope=openid offline_access&claims={\"id_token\":{\"urn:telekom.com:all\":{\"essential\":false}}}";

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
      postData = "client_id=" + CLIENT_ID + "&scope=openid offline_access&grant_type=urn:telekom:com:grant-type:remote-login&auth_req_id=" +
                auth_req_id + "&auth_req_sec=" + auth_req_sec + "&claims={\"id_token\":{\"urn:telekom.com:all\":{\"essential\":false}}}";

      jsonString = m_httpClient->HttpPost(url, postData, statusCode);

      doc.Parse(jsonString.c_str());
      if ((doc.GetParseError()) || (statusCode != 200))
      {
        kodi::Log(ADDON_LOG_ERROR, "Failed to get openid token");
        return false;
      }
      m_settings->SetSetting("openid_token", Utils::JsonStringOrEmpty(doc, "access_token"));
      postData = "client_id=" + CLIENT_ID + "&scope=ngtvepg offline_access&grant_type=refresh_token&refresh_token=" + Utils::JsonStringOrEmpty(doc, "refresh_token");

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
  std::string jsonString;
  int statusCode = 0;

  std::string url = m_epg_https_url + "/EPG/JSON/CategoryList?userContentListFilter=" + m_userContentListFilter;
  std::string postData = "{\"offset\": 0, \"count\": 1000,"
	                       "\"type\": \"VOD;AUDIO_VOD;VIDEO_VOD;CHANNEL;AUDIO_CHANNEL;VIDEO_CHANNEL;MIX;VAS;PROGRAM\","
	                       "\"categoryid\": \"2000000142\"}";

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

bool CPVRMagenta::GetTimersRecordings(const bool isRecording)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (isRecording) {
    m_recordings.clear();
  } else {
    m_timers.clear();
  }

  std::string url = m_epg_https_url + "/EPG/JSON/QueryPVR";
  std::string postData = "{\"count\": -1,"
	                       "\"expandSubTask\": 2,"
	                       "\"isFilter\": 0,"
	                       "\"offset\": 0,"
	                       "\"orderType\": 1,"
	                       "\"pvrType\": 2,"
	                       "\"type\": 0,"
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

    int index = 0;
    for (rapidjson::Value::ConstValueIterator itr1 = recordings.Begin();
        itr1 != recordings.End(); ++itr1)
    {
      const rapidjson::Value& recordingItem = (*itr1);

      std::string pvrid = Utils::JsonStringOrEmpty(recordingItem, "pvrId");

      if (pvrid.empty())
        continue;

      MagentaRecording magenta_recording;

      magenta_recording.index = index++;
      magenta_recording.pvrId = pvrid;
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
      if (isRecording) {
        m_recordings.emplace_back(magenta_recording);
        kodi::Log(ADDON_LOG_DEBUG, "Found recording: [%s]", magenta_recording.pvrName.c_str());
      } else {
        m_timers.emplace_back(magenta_recording);
        kodi::Log(ADDON_LOG_DEBUG, "Found timer: [%s]", magenta_recording.pvrName.c_str());
      }
    }
  }
  return true;
}

bool CPVRMagenta::GetGenreIds()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  std::string jsonString;
  int statusCode = 0;

  std::string url = m_epg_https_url + "/EPG/JSON/GetGenreList";
  std::string postData = "{}";

  jsonString = m_httpClient->HttpPost(url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if ((doc.GetParseError()) || (!doc.HasMember("retcode")))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get GenreList");
    return false;
  }
  if (Utils::JsonStringOrEmpty(doc, "retcode") != "0")
  {
    kodi::Log(ADDON_LOG_ERROR, "GetGenreList returned not 0!");
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

      m_genres.emplace_back(magenta_genre);
    }
  }
  return true;
}

CPVRMagenta::CPVRMagenta() :
  m_settings(new CSettings())
{
  m_settings->Load();
  m_httpClient = new HttpClient(m_settings);

  srand(time(nullptr));

  if (!MagentaGuestLogin()) {
    return;
  }
  if (!MagentaAuthenticate()) {
    return;
  }
  if (m_settings->IsGroupsenabled()) {
    if (!GetCategories()) {
      return;
    }
  }
  GetGenreIds();
  GetTimersRecordings(true); //recordings
  GetTimersRecordings(false); //timers
  LoadChannels();
}

bool CPVRMagenta::AddGroupChannel(const long groupid, const int channelid)
{
  for (auto& cgroup : m_categories)
  {
    if (cgroup.id != groupid)
      continue;

    cgroup.channels.emplace_back(channelid);
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
  kodi::Log(ADDON_LOG_DEBUG, "Load Magenta Channels");
  std::string url = m_epg_https_url + "/EPG/JSON/AllChannel?userContentListFilter=" + m_userContentListFilter;
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
    magenta_channel.iChannelNumber = startnum + stoi(Utils::JsonStringOrEmpty(channelItem, "chanNo"));
    magenta_channel.strChannelName = Utils::JsonStringOrEmpty(channelItem, "name");

    const rapidjson::Value& pictures = channelItem["pictures"];
    for (rapidjson::Value::ConstValueIterator itr2 = pictures.Begin();
        itr2 != pictures.End(); ++itr2)
    {
      const rapidjson::Value& pictureItem = (*itr2);

      if (Utils::JsonStringOrEmpty(pictureItem, "imageType") == "14") {
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
      long categoryId;
      const rapidjson::Value& categories = channelItem["categoryIds"];
      for (rapidjson::SizeType i = 0; i < categories.Size(); i++)
      {
        std::string cat = categories[i].GetString();
        categoryId = stol(cat);
        AddGroupChannel(categoryId, magenta_channel.iUniqueId);
      }
    }

    m_channels.emplace_back(magenta_channel);
  }

  url = m_epg_https_url + "/EPG/JSON/AllChannelDynamic";

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
//          m_channels[i].strStreamURL = Utils::JsonStringOrEmpty(physItem, "playurl");
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
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(false);
  capabilities.SetSupportsChannelGroups(m_settings->IsGroupsenabled());
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsProviders(false);
//  capabilities.SetHandlesInputStream(true);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetBackendName(std::string& name)
{
  name = "magenta pvr add-on";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetBackendVersion(std::string& version)
{
  version = "0.1";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetBackendHostname(std::string& hostname)
{
  hostname = "";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  total = 1024 * 1024 * 1024;
  used = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetEPGForChannel(int channelUid,
                                     time_t start,
                                     time_t end,
                                     kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
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
  for (const auto& channel : m_channels)
  {

    if (channel.iUniqueId != channelUid)
      continue;

    std::string startTime = Utils::TimeToString(start);
    std::string endTime = Utils::TimeToString(end);

    std::string jsonEpg;
    int statusCode = 0;

    kodi::Log(ADDON_LOG_DEBUG, "Start %u End %u", start, end);
    kodi::Log(ADDON_LOG_DEBUG, "EPG Request for channel x from %s to %s", startTime.c_str(), endTime.c_str());

    std::string postData = "{\"channelid\": \"" + std::to_string(channel.iUniqueId) + "\"," +
    	                     "\"type\": 2," +
    	                     "\"offset\": 0," +
    	                     "\"isFillProgram\": 1," +
    	                     "\"properties\": [{" +
    		                           "\"name\": \"playbill\"," +
    		                                       "\"include\": \"audioAttribute,channelid,contentRight,endtime,externalContentCode,episodeInformation," +
                                                              "externalIds,genres,id,introduce,name,pictures,ratingid,seasonNum,seriesID,starttime," +
                                                              "subName,subNum,tipType,videoAttribute\"}]," +
    	                     "\"count\": 1000," +
    	                     "\"begintime\": \"" + startTime + "\"," +
    	                     "\"endtime\": \"" + endTime + "\"}";

    kodi::Log(ADDON_LOG_DEBUG, "PostData %s", postData.c_str());

    std::string url = m_epg_https_url + "/EPG/JSON/PlayBillList?userContentFilter=" + m_userContentFilter;

    jsonEpg = m_httpClient->HttpPost(url, postData, statusCode);
//    kodi::Log(ADDON_LOG_DEBUG, "GetProgramme returned: code: %i %s", statusCode, jsonEpg.c_str());

    rapidjson::Document epgDoc;
    epgDoc.Parse(jsonEpg.c_str());
    if (epgDoc.GetParseError())
    {
      kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing json");
      return PVR_ERROR_SERVER_ERROR;
    }

    if (epgDoc.HasMember("retcode")) {
        MagentaAuthenticate();
        jsonEpg = m_httpClient->HttpPost(url, postData, statusCode);
        epgDoc.Parse(jsonEpg.c_str());
        if (epgDoc.HasMember("retcode")) {
          return PVR_ERROR_SERVER_ERROR;
        }
    }

    kodi::Log(ADDON_LOG_DEBUG, "[epg] iterate entries");

    const rapidjson::Value& epgitems = epgDoc["playbilllist"];
    for (rapidjson::Value::ConstValueIterator itr1 = epgitems.Begin();
          itr1 != epgitems.End(); ++itr1)
    {
      const rapidjson::Value& epgItem = (*itr1);

      kodi::addon::PVREPGTag tag;

      tag.SetUniqueBroadcastId(std::stoi(Utils::JsonStringOrEmpty(epgItem,"id")));
      tag.SetUniqueChannelId(channel.iUniqueId);
      tag.SetTitle(Utils::JsonStringOrEmpty(epgItem,"name"));
      tag.SetPlot(Utils::JsonStringOrEmpty(epgItem,"introduce"));

      std::string epgstart = Utils::JsonStringOrEmpty(epgItem,"starttime");
      std::string epgend = Utils::JsonStringOrEmpty(epgItem,"endtime");
      tag.SetStartTime(Utils::StringToTime(epgstart));
      tag.SetEndTime(Utils::StringToTime(epgend));

      std::string image = GetPictureFromItem(epgItem);
      if (!image.empty()) {
        tag.SetIconPath(image);
      }
      std::string genres = Utils::JsonStringOrEmpty(epgItem,"genres");
      if (genres != "") {
        tag.SetGenreType(EPG_GENRE_USE_STRING);
        tag.SetGenreDescription(genres);
      }
      std::string subname = Utils::JsonStringOrEmpty(epgItem,"subName");
      if (subname != "") {
        tag.SetEpisodeName(subname);
      }
      std::string season = Utils::JsonStringOrEmpty(epgItem, "seasonNum");
      if (season != "") {
        tag.SetSeriesNumber(std::stoi(season));
      }
      std::string episode = Utils::JsonStringOrEmpty(epgItem, "subNum");
      if (episode != "") {
        tag.SetEpisodeNumber(std::stoi(episode));
        tag.SetFlags(EPG_TAG_FLAG_IS_SERIES);
      }
      std::string rating = Utils::JsonStringOrEmpty(epgItem, "ratingid");
      if (episode != "-1") {
          tag.SetParentalRating(std::stoi(rating));
      }
      results.Add(tag);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  bIsPlayable = false;

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      auto current_time = time(NULL);
      if ((current_time > tag.GetStartTime()) && (current_time < tag.GetEndTime()))
      {
        bIsPlayable = true;
      }
    }
  }

/*
  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      bIsPlayable = channel.bArchive;
    }
  }
*/
  return PVR_ERROR_NO_ERROR;
}

bool CPVRMagenta::ReleaseCurrentMedia()
{
  std::string url = m_epg_https_url + "/EPG/JSON/ReleasePlaySession";
  std::string postData = "{\"mediaId\": \"" + std::to_string(m_currentMediaId) + "\","
                           "\"contentId\": \"" + std::to_string(m_currentChannelId) + "\","
                           "\"contentType\": \"VIDEO_CHANNEL\"}";

  rapidjson::Document doc;
  if (!JsonRequest(url, postData, doc)) {
    return false;
  }
  return true;
}

PVR_ERROR CPVRMagenta::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {

      int chanId = channel.iUniqueId;
      int mediaId = SelectMediaId(channel, false);

      if (!ReleaseCurrentMedia()) {
        return PVR_ERROR_FAILED;
      }

      std::string checksum = hmac<SHA256>(std::to_string(chanId), m_session_key);
      kodi::Log(ADDON_LOG_DEBUG, "Checksum: %s", checksum.c_str());

      int statusCode = 0;
      std::string url = m_epg_https_url + "/EPG/JSON/Play";
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
        return PVR_ERROR_FAILED;
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
            return PVR_ERROR_FAILED;
          }
          if (Utils::JsonStringOrEmpty(doc, "retcode") != "0") {
            kodi::Log(ADDON_LOG_ERROR, "Play returned not 0! %s", jsonString.c_str());
            return PVR_ERROR_FAILED;
          }
        } else {
          kodi::Log(ADDON_LOG_ERROR, "Play returned not 0! %s", jsonString.c_str());
          return PVR_ERROR_FAILED;
        }
      }
      std::string playUrl = Utils::JsonStringOrEmpty(doc, "url");
//      std::string playUrl = GetPlayUrl(channel, mediaId);
      if (playUrl.empty()) {
        kodi::Log(ADDON_LOG_ERROR, "Play url was empty");
        return PVR_ERROR_FAILED;
      }
      kodi::Log(ADDON_LOG_DEBUG, "[PLAY Timeshifted] url: %s", playUrl.c_str());
      std::vector<std::string> out;
      tokenize(playUrl, "|", out);
      std::string spliturl = out[1];
/*
      for (auto &s: out) {
        kodi::Log(ADDON_LOG_DEBUG, "[PLAY Timeshifted] url: %s", s.c_str());
      }
*/
      std::string appendix = "&uid=" + m_userID + "&sid=" + m_sessionID + "&i=0&dp=0";
      spliturl += appendix;

      kodi::Log(ADDON_LOG_DEBUG, "[PLAY Timeshifted] url: %s", spliturl.c_str());
      SetStreamProperties(properties, spliturl, false, true, false);
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
  amount = m_channels.size();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Channels Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {

    if ((channel.bRadio == bRadio) && ((!m_settings->HideUnsubscribed()) || (HasStreamingUrl(channel))))
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.iUniqueId);
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(channel.iChannelNumber);
      kodiChannel.SetChannelName(channel.strChannelName);
      kodiChannel.SetIconPath(channel.strIconPath);
      kodiChannel.SetIsHidden(false);
      kodiChannel.SetHasArchive(false);

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  MagentaChannel addonChannel;
  GetChannel(channel, addonChannel);

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
  amount = static_cast<int>(m_categories.size());
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Groups Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

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
  for (const auto& cgroup : m_categories)
  {
    if (cgroup.name != group.GetGroupName())
      continue;

    int position = 0;
    for (const auto& channel : cgroup.channels)
    {
      kodi::addon::PVRChannelGroupMember kodiGroupMember;

      kodiGroupMember.SetGroupName(group.GetGroupName());
      kodiGroupMember.SetChannelUniqueId(channel);
      kodiGroupMember.SetChannelNumber(++position);

      results.Add(kodiGroupMember);
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  signalStatus.SetAdapterName("pvr magenta backend");
  signalStatus.SetAdapterStatus("OK");

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetRecordingsAmount(bool deleted, int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = static_cast<int>(m_recordings.size());
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Recordings Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (!GetTimersRecordings(true)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get recordings from backend");
    return PVR_ERROR_SERVER_ERROR;
  }

  std::vector<MagentaRecording>::iterator it;
  for (it = m_recordings.begin(); it != m_recordings.end(); ++it)
  {
    kodi::addon::PVRRecording kodiRecording;

    kodiRecording.SetRecordingId(it->pvrId);
    kodiRecording.SetTitle(it->pvrName);
    kodiRecording.SetPlot(it->introduce);
    kodiRecording.SetChannelName(it->channelName);
    if (!it->picture.empty()) {
      kodiRecording.SetIconPath(it->picture);
      kodiRecording.SetThumbnailPath(it->picture);
      kodiRecording.SetFanartPath(it->picture);
    }
    if (it->realRecordLength != 0) {
      kodiRecording.SetDuration(it->realRecordLength);
    }
    kodiRecording.SetLastPlayedPosition(it->bookmarkTime);
    std::string begintime = it->beginTime;
    begintime.insert(4,1,'-');
    begintime.insert(7,1,'-');
    begintime.insert(10,1,' ');
    begintime.insert(13,1,':');
    begintime.insert(16,1,':');
    begintime += " UTC";
    kodiRecording.SetRecordingTime(Utils::StringToTime(begintime));
    kodiRecording.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);

    results.Add(kodiRecording);
    kodi::Log(ADDON_LOG_DEBUG, "Recording added: %s", it->pvrName.c_str());
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

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
    std::string url = m_epg_https_url + "/EPG/JSON/AuthorizeAndPlay";
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
  std::string url = m_epg_https_url + "/EPG/JSON/DeletePVR";
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
  TimerType(
            unsigned int id,
            unsigned int attributes,
            const std::string& description)
  //          const std::vector<kodi::addon::PVRTypeIntValue>& priorityValues =
  //              std::vector<kodi::addon::PVRTypeIntValue>(),
  //          const std::vector<kodi::addon::PVRTypeIntValue>& lifetimeValues =
  //              std::vector<kodi::addon::PVRTypeIntValue>(),
  //          const std::vector<kodi::addon::PVRTypeIntValue>& dupEpisodesValues =
  //              std::vector<kodi::addon::PVRTypeIntValue>())
  {
    SetId(id);
    SetAttributes(attributes);
    SetDescription(description);
//    SetPriorities(priorityValues, settings->GetDvrPriority());
//    SetLifetimes(lifetimeValues, LifetimeMapper::TvhToKodi(settings->GetDvrLifetime()));
//    SetPreventDuplicateEpisodes(dupEpisodesValues, settings->GetDvrDupdetect());
  }
};

} // unnamed namespace

PVR_ERROR CPVRMagenta::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  unsigned int TIMER_ONCE_EPG_ATTRIBS =
       PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME |
       PVR_TIMER_TYPE_SUPPORTS_END_TIME | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
       PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN | PVR_TIMER_TYPE_SUPPORTS_PRIORITY |
       PVR_TIMER_TYPE_SUPPORTS_LIFETIME | PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE;

       /* One-shot epg based */
     types.emplace_back(TimerType(
         /* Settings */
//         m_settings,
         /* Type id. */
         TIMER_ONCE_EPG,
         /* Attributes. */
         TIMER_ONCE_EPG_ATTRIBS,
         /* Let Kodi generate the description. */
         "Einzelaufnahme"));
//         /* Values definitions for priorities. */
//         priorityValues,
//         /* Values definitions for lifetime. */
//         lifetimeValues));

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetTimersAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = static_cast<int>(m_timers.size());
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

  std::vector<MagentaRecording>::iterator it;
  for (it = m_timers.begin(); it != m_timers.end(); ++it)
  {
    kodi::addon::PVRTimer kodiTimer;

    kodiTimer.SetClientIndex(static_cast<unsigned int>(it->index));
    kodiTimer.SetTimerType(TIMER_ONCE_EPG);
    kodiTimer.SetState(PVR_TIMER_STATE_SCHEDULED);
    kodiTimer.SetTitle(it->pvrName);
    std::string begintime = it->beginTime;
    begintime.insert(4,1,'-');
    begintime.insert(7,1,'-');
    begintime.insert(10,1,' ');
    begintime.insert(13,1,':');
    begintime.insert(16,1,':');
    begintime += " UTC";
    kodiTimer.SetStartTime(Utils::StringToTime(begintime));
    std::string endtime = it->endTime;
    endtime.insert(4,1,'-');
    endtime.insert(7,1,'-');
    endtime.insert(10,1,' ');
    endtime.insert(13,1,':');
    endtime.insert(16,1,':');
    endtime += " UTC";
    kodiTimer.SetEndTime(Utils::StringToTime(endtime));
    kodiTimer.SetSummary(it->introduce);
//    kodiTimer.SetEPGUid(static_cast<unsigned int>());
    kodiTimer.SetClientChannelUid(it->channelId);

//    kodiTimer.SetPlot(it->introduce);
//    kodiTimer.SetChannelName(it->channelName);
//    kodiTimer.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);

    results.Add(kodiTimer);
    kodi::Log(ADDON_LOG_DEBUG, "Timer added: %s", it->pvrName.c_str());
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::AddTimer(const kodi::addon::PVRTimer& timer)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == timer.GetClientChannelUid()) {
      std::string url = m_epg_https_url + "/EPG/JSON/AddPVR";
      std::string postData = "{\"mediaId\": \"" + std::to_string(SelectMediaId(channel, true)) + "\","
    	                        "\"beginOffset\": " + std::to_string(timer.GetMarginStart()) + ","
    	                        "\"deleteMode\": 1,"
    	                        "\"endOffset\": " + std::to_string(timer.GetMarginEnd()) + ","
    	                        "\"strategyType\": 0,"
    	                        "\"type\": 2,"
    	                        "\"programId\": \"" + std::to_string(timer.GetEPGUid()) + "\"}";
//      kodi::Log(ADDON_LOG_DEBUG, "PostData: %s", postData.c_str());
      rapidjson::Document doc;
      if (!JsonRequest(url, postData, doc)) {
        return PVR_ERROR_SERVER_ERROR;
      }
      else {
        kodi::Log(ADDON_LOG_DEBUG, "Added timer for PVRID: %s", Utils::JsonStringOrEmpty(doc, "pvrId").c_str());
        kodi::QueueNotification(QUEUE_INFO, "Aufnahme", "Aufnahme programmiert");
        kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
        auto current_time = time(NULL);
        if (current_time > timer.GetStartTime()) {
          kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
        }
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
/*
  if ((timer.GetTimerType() == TIMER_ONCE_MANUAL) || (timer.GetTimerType() == TIMER_ONCE_EPG))
  {
     one shot timer
  }
  */
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CPVRMagenta::DeleteTimer(const kodi::addon::PVRTimer& timer, bool)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& thisTimer : m_timers)
  {
    if (thisTimer.index == timer.GetClientIndex())
    {
      return DeletePVR(thisTimer.pvrId, false);
    }
  }
return PVR_ERROR_FAILED;
}

PVR_ERROR CPVRMagenta::CallEPGMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                    const kodi::addon::PVREPGTag& item)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);


  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRMagenta::CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                        const kodi::addon::PVRChannel& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRMagenta::CallTimerMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                      const kodi::addon::PVRTimer& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRMagenta::CallRecordingMenuHook(const kodi::addon::PVRMenuhook& menuhook,
                                          const kodi::addon::PVRRecording& item)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRMagenta::CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
  return CallMenuHook(menuhook);
}

PVR_ERROR CPVRMagenta::CallMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
  int iMsg;
  switch (menuhook.GetHookId())
  {
    case 1:
      iMsg = 30010;
      break;
    case 2:
      iMsg = 30011;
      break;
    case 3:
      iMsg = 30012;
      break;
    case 4:
      iMsg = 30012;
      break;
    default:
      return PVR_ERROR_INVALID_PARAMETERS;
  }
  kodi::QueueNotification(QUEUE_INFO, "", kodi::addon::GetLocalizedString(iMsg));

  return PVR_ERROR_NO_ERROR;
}
/*
bool CPVRMagenta::SeekTime(double time, bool backward, double& startpts)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  return true;
}

int64_t CPVRMagenta::SeekLiveStream(int64_t position, int whence)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  return 0;
}

bool CPVRMagenta::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  return false;
}

void CPVRMagenta::CloseLiveStream()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

}
*/
bool CPVRMagenta::GetChannel(const kodi::addon::PVRChannel& channel, MagentaChannel& myChannel)
{
  for (const auto& thisChannel : m_channels)
  {
    if (thisChannel.iUniqueId == (int)channel.GetUniqueId())
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

      return true;
    }
  }

  return false;
}

ADDONCREATOR(CPVRMagenta)
