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
//#include <tinyxml2.h>
#include "Utils.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Base64.h"

//using namespace tinyxml2;
//using namespace rapidjson;

/***********************************************************
  * PVR Client AddOn specific public library functions
  ***********************************************************/


std::string ltrim(const std::string &s)
{
    size_t start = s.find_first_not_of("\"");
    return (start == std::string::npos) ? "" : s.substr(start);
}

std::string rtrim(const std::string &s)
{
    size_t end = s.find_last_not_of("\"");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string trim(const std::string &s) {
    return rtrim(ltrim(s));
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

  std::string url = m_epg_https_url + "/EPG/JSON/DTAuthenticate";
  std::string postData = "{\"accessToken\": \"" + m_settings->GetMagentaEPGToken() +
                          "\", \"usergroup\": \"IPTV_OTT_DT" +
                          "\", \"connectType\": 1" +
	                        ", \"userType\": \"1\"" +
                          ", \"terminalid\": \"" + m_device_id +
	                        "\", \"mac\": \"" + m_device_id +
	                        "\", \"terminaltype\": \"TV_AndroidTV" +
                          "\", \"utcEnable\": 1" +
                          ", \"timezone\": \"Europe/Berlin" +
                          "\", \"caDeviceInfo\": [{" +
		                           "\"caDeviceType\": 6" +
		                           ", \"caDeviceId\": \"" + m_device_id + "\"}]," +
	                        "\"terminalDetail\": [{" +
		                          "\"key\": \"GUID\"," +
		                          "\"value\": \"" + m_device_id + "\"}, {" +
                              "\"key\": \"HardwareSupplier\"," +
                              "\"value\": \"AndroidTV SHIELD Android TV\"}, {" +
                              "\"key\": \"DeviceClass\"," +
                              "\"value\": \"TV\"}, {" +
                              "\"key\": \"DeviceStorage\"," +
                              "\"value\": \"12495872000\"}, {" +
		                          "\"key\": \"DeviceStorageSize\"," +
		                          "\"value\": \"12495872000\"}]," +
	                        "\"softwareVersion\": \"11\"," +
	                        "\"osversion\": \"7825230_3167.5736\"," +
	                        "\"terminalvendor\": \"SHIELD Android TV\"," +
	                        "\"preSharedKeyID\": \"NGTV000001\"," +
	                        "\"cnonce\": \"2b020af306493d4df629f8311309cde9\"," +
	                        "\"areaid\": \"1\"," +
	                        "\"templatename\": \"NGTV\"," +
	                        "\"subnetId\": \"4901\"}";
  //kodi::Log(ADDON_LOG_DEBUG, "PostData %s", postData.c_str());

  std::string jsonString = m_httpClient->HttpPost(url, postData, statusCode);

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to Authenticate");
    return false;
  }
//  kodi::Log(ADDON_LOG_DEBUG, "Magenta Authenticate returned: %s", jsonString.c_str());

  std::string retcode = Utils::JsonStringOrEmpty(doc, "retcode");
  if (retcode == "0") {
    m_settings->SetSetting("csrftoken", Utils::JsonStringOrEmpty(doc, "csrfToken"));
    const rapidjson::Value& ca_verimatrix = doc["ca"]["verimatrix"];
    m_licence_url = Utils::JsonStringOrEmpty(ca_verimatrix, "multiRightsWidevine");
    const rapidjson::Value& ca_device = doc["caDeviceInfo"][0];
    m_ca_device_id = Utils::JsonStringOrEmpty(ca_device, "VUID");
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
    std::string postData = "client_id=10LIVESAM30000004901NGTVANDROIDTV0000000&scope=ngtvepg offline_access&grant_type=refresh_token&refresh_token=" + m_settings->GetMagentaRefreshToken();

    std::string jsonString = m_httpClient->HttpPost(url, postData, statusCode);

    rapidjson::Document doc;
    doc.Parse(jsonString.c_str());
    if ((doc.GetParseError()) || (statusCode != 200))
    {
      url = m_sam_service_url + "/oauth2/bc-auth/start";
      postData = "client_id=10LIVESAM30000004901NGTVANDROIDTV0000000&scope=openid offline_access&claims={\"id_token\":{\"urn:telekom.com:all\":{\"essential\":false}}}";

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

      std::string text = "\n" + otp + "\n\n" + kodi::addon::GetLocalizedString(30047) + expires + kodi::addon::GetLocalizedString(30048) + "\n";
      kodi::gui::dialogs::OK::ShowAndGetInput(kodi::addon::GetLocalizedString(30046), text);

      url = m_sam_service_url + "/oauth2/tokens";
      postData = "client_id=10LIVESAM30000004901NGTVANDROIDTV0000000&scope=openid offline_access&grant_type=urn:telekom:com:grant-type:remote-login&auth_req_id=" +
                auth_req_id + "&auth_req_sec=" + auth_req_sec + "&claims={\"id_token\":{\"urn:telekom.com:all\":{\"essential\":false}}}";

      jsonString = m_httpClient->HttpPost(url, postData, statusCode);

      doc.Parse(jsonString.c_str());
      if ((doc.GetParseError()) || (statusCode != 200))
      {
        kodi::Log(ADDON_LOG_ERROR, "Failed to get openid token");
        return false;
      }
      m_settings->SetSetting("openid_token", Utils::JsonStringOrEmpty(doc, "access_token"));
      postData = "client_id=10LIVESAM30000004901NGTVANDROIDTV0000000&scope=ngtvepg offline_access&grant_type=refresh_token&refresh_token=" + Utils::JsonStringOrEmpty(doc, "refresh_token");

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

/*
  std::string client_id = "10LIVESAM30000004901NGTVMAGENTA000000000";
  std::string redirect_uri = "https%3A%2F%2Fweb.magentatv.de%2Fauthn%2Fidm";
  std::string response_type = "code";
  std::string scope = "openid+offline_access";

  std::string url = "https://accounts.login.idm.telekom.com/oauth2/auth";
  std::string params = "?client_id=" + client_id +
                        "&redirect_uri=" + redirect_uri +
                        "&response_type=" + response_type +
                        "&scope=" + scope;

  std::string command = url + params;
  kodi::Log(ADDON_LOG_DEBUG, "Magenta Login 1st step get: %s", command.c_str());

  std::string result = m_httpClient->HttpGet(command, statusCode);
  kodi::Log(ADDON_LOG_DEBUG, "Magenta Login 1st step root element: %s", result.c_str());
  htmlDoc.Parse(result.c_str(), 0);
  //kodi::Log(ADDON_LOG_DEBUG, "Magenta Login 1st step: %s", result.c_str());

  tinyxml2::XMLElement* script =
      htmlDoc.FirstChildElement( "body" )->FirstChildElement( "script" );

      const char* script_text = script->GetText();
  //    kodi::Log(ADDON_LOG_DEBUG, "Magenta Login 1st script text: %s", script_text);
//      printf( "Name of play (part 1): %s\n", script_text );

  //tinyxml2::XMLElement* pRootElement = htmlDoc.RootElement();
//  if (pRootElement == nullptr) {
//    kodi::Log(ADDON_LOG_DEBUG, "Magenta Login 1st step root element: error");
//  } else {
//    kodi::Log(ADDON_LOG_DEBUG, "Magenta Login 1st step root element: %s", pRootElement->Value());
//  }
*/

  return true;
}

CPVRMagenta::CPVRMagenta() :
  m_settings(new CSettings())
{
  m_settings->Load();
  m_httpClient = new HttpClient(m_settings);

  if (!MagentaGuestLogin()) {
    return;
  }
  if (!MagentaAuthenticate()) {
    return;
  }
  LoadChannels();
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
  std::string url = m_epg_https_url + "/EPG/JSON/AllChannel?userContentListFilter=546411680";
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

  int startnum = 0;
  for (rapidjson::Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    const rapidjson::Value& channelItem = (*itr1);

    MagentaChannel magenta_channel;

    magenta_channel.bRadio = false;
    magenta_channel.bArchive = false;
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

    const rapidjson::Value& physicals = channelItem["physicalChannels"];
    for (rapidjson::Value::ConstValueIterator itr2 = physicals.Begin();
        itr2 != physicals.End(); ++itr2)
    {
      const rapidjson::Value& physicalItem = (*itr2);

      if ((Utils::JsonStringOrEmpty(physicalItem, "fileFormat") == "4") && (Utils::JsonStringOrEmpty(physicalItem, "definition") == "1")) {
        magenta_channel.mediaId = stoi(Utils::JsonStringOrEmpty(physicalItem, "mediaId"));
      }
    }

    kodi::Log(ADDON_LOG_DEBUG, "%i. Channel Name: %s ID: %i MediaID %i", magenta_channel.iChannelNumber, magenta_channel.strChannelName.c_str(), magenta_channel.iUniqueId, magenta_channel.mediaId);
    m_channels.emplace_back(magenta_channel);
  }

  url = m_epg_https_url + "/EPG/JSON/AllChannelDynamic";
  jsonString;
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
      if (m_channels[i].mediaId == stoi(Utils::JsonStringOrEmpty(physItem, "mediaId"))) {
        m_channels[i].strStreamURL = Utils::JsonStringOrEmpty(physItem, "playurl");
      }
    }
    i++;
  }

  return true;
}

void CPVRMagenta::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::string& url,
                                    bool realtime, bool playTimeshiftBuffer)
{
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", url.c_str());

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
//  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");

  // MPEG DASH
//  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] dash");
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
  capabilities.SetSupportsChannelGroups(false);
  capabilities.SetSupportsRecordings(false);
  capabilities.SetSupportsRecordingsDelete(false);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsTimers(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsProviders(false);

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

    std::string url = m_epg_https_url + "/EPG/JSON/PlayBillList?userContentFilter=-375463788";

    jsonEpg = m_httpClient->HttpPost(url, postData, statusCode);
    kodi::Log(ADDON_LOG_DEBUG, "GetProgramme returned: code: %i %s", statusCode, jsonEpg.c_str());

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

      if (epgItem.HasMember("pictures")) {
        const rapidjson::Value& images = epgItem["pictures"];
        for (rapidjson::Value::ConstValueIterator itr2 = images.Begin();
            itr2 != images.End(); ++itr2)
        {
          const rapidjson::Value& imageItem = (*itr2);

          if (Utils::JsonStringOrEmpty(imageItem, "imageType") == "17") {
            tag.SetIconPath(Utils::JsonStringOrEmpty(imageItem, "href"));
          }
        }
      }

      results.Add(tag);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  bIsPlayable = false;
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

PVR_ERROR CPVRMagenta::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
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

    if (channel.bRadio == bRadio)
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.iUniqueId);
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(channel.iChannelNumber);
      kodiChannel.SetChannelName(channel.strChannelName);
      kodiChannel.SetIconPath(channel.strIconPath);
      kodiChannel.SetIsHidden(false);
      kodiChannel.SetHasArchive(channel.bArchive);

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

  kodi::Log(ADDON_LOG_DEBUG, "Stream URL -> %s", addonChannel.strStreamURL.c_str());
  kodi::Log(ADDON_LOG_DEBUG, "ReferenceID -> %i", addonChannel.iUniqueId);
  SetStreamProperties(properties, addonChannel.strStreamURL, true, false);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelGroupsAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                           kodi::addon::PVRChannelGroupMembersResultSet& results)
{
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
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  /* TODO: Implement this to get support for the timer features introduced with PVR API 1.9.7 */
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CPVRMagenta::GetTimersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
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

bool CPVRMagenta::GetChannel(const kodi::addon::PVRChannel& channel, MagentaChannel& myChannel)
{
  for (const auto& thisChannel : m_channels)
  {

    if (thisChannel.iUniqueId == (int)channel.GetUniqueId())
    {
      myChannel.iUniqueId = thisChannel.iUniqueId;
      myChannel.mediaId = thisChannel.mediaId;
      myChannel.bRadio = thisChannel.bRadio;
      myChannel.iChannelNumber = thisChannel.iChannelNumber;
      myChannel.strChannelName = thisChannel.strChannelName;
      myChannel.strIconPath = thisChannel.strIconPath;
      myChannel.strStreamURL = thisChannel.strStreamURL;

      return true;
    }
  }

  return false;
}

std::string CPVRMagenta::GetRecordingURL(const kodi::addon::PVRRecording& recording)
{
  return "";
}

ADDONCREATOR(CPVRMagenta)
