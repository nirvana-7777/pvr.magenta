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

class TaaClient;
class SsoClient;

class AuthClient
{
public:
  AuthClient(CSettings* settings, HttpClient* httpclient);
  ~AuthClient();

  bool GetPersonaToken(std::string& personaToken);
  void SetSam3Url(const std::string& url);
  void SetSam3ClientId(const std::string& id);
  void SetLineAuthUrl(const std::string& url);
  void SetDeviceToken(const std::string& token);
  void SetAuthorizeTokenUrl(const std::string& url);
  bool InitSam3();
  void SetTaaUrl(const std::string& url);
  void SetAccountUri(const std::string& accountUri);
  bool ReLogin();

//  std::string SSOLogin();
//  bool SSOAuthenticate(const std::string& code, const std::string& state);

private:
  bool IsJWTexpired(const std::string& token);
  bool IsPersonaTokenExpired(const std::string& personaToken);
  void ComposePersonaToken(const std::string& dcCtsPersonaToken);

  CSettings* m_settings;
  HttpClient* m_httpClient;
  TaaClient* m_taaClient;
  SsoClient* m_ssoClient;
  Sam3Client* m_sam3Client;

  std::string m_personaToken;
  std::string m_accountUri;
};
