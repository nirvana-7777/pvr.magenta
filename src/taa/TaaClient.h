/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "../Settings.h"
#include "../http/HttpClient.h"
#include "../sam3/Sam3Client.h"

static const std::string APPVERSION2 = "3.134.4462";
static const std::string IDM = "TDGIDM";

class TaaClient
{
public:
  TaaClient(CSettings* settings, HttpClient* httpclient, Sam3Client* sam3Client);
  ~TaaClient();

  void SetTaaUrl(const std::string& url);
//  void SetAccountUri(const std::string& accountUri);
  bool UpdateTaa(std::string& dcCtsPersonaToken);

private:
  bool ParseJWT(const std::string& jwt);
//  bool IsTokenValid();

  CSettings* m_settings;
  HttpClient* m_httpClient;
  Sam3Client* m_sam3Client;

  std::string m_sam3AccessToken;
  std::string m_deviceId;
  std::string m_taaUrl;
  std::string m_TaaAccessToken;
  std::string m_TaaRefreshToken;
  time_t m_tokenIat;
  time_t m_tokenExp;
  std::string m_accountToken;
//  std::string m_personaToken;
  std::string m_dcCtsPersonaToken;
  std::string m_personaId;
  std::string m_consumerId;
  std::string m_tvAccountId;
  std::string m_accountId;
  int m_platform;
//  std::string m_accountUri;
};
