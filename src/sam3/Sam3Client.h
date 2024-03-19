
/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "../Settings.h"
#include "../http/HttpClient.h"

static const std::string GRANTLINEAUTH = "urn:com:telekom:ott-app-services:access-auth";
static const std::string GRANTAUTHCODE = "authorization_code";
static const std::string GRANTPASSWORD = "password";
static const std::string GRANTREFRESHTOKEN = "refresh_token";
static const std::string GRANTREMOTELOGIN = "urn:telekom:com:grant-type:remote-login";

struct Sam3KV
{
  std::string name;
  std::string value;
};

struct Sam3AuthMethods
{
  bool password;
  bool code;
  bool line;
};

struct Sam3AccessTokens
{
  std::string taa;
  std::string tvhubs;
};

class SsoClient;

class Sam3Client
{
public:
  Sam3Client(CSettings* settings, HttpClient* httpclient, SsoClient* ssoclient);
  ~Sam3Client();

  void SetClientId(const std::string& id);
  void SetSam3Url(const std::string& url);
  void SetLineAuthUrl(const std::string& lineAuthUrl);
  void SetDeviceToken(const std::string& token);
  void SetAuthorizeTokenUrl(const std::string& url);
  bool InitSam3();
  bool Sam3Login(std::string& personaToken);
  bool ReAuthenticate(const std::string& grant);
  bool GetAccessToken(const std::string& scope, std::string& accessToken);
/*
  std::string GetPersonaToken() {
    return m_personaToken;
  }
*/
private:
  void ParseHtml(const std::string& result);
  bool GetToken(const std::string& grantType, const std::string& scope, const std::string& credential1, const std::string& credential2, std::string& accessToken);
  bool RefreshToken(const std::string& scope, std::string& accessToken);
  bool RemoteLogin(const std::string& auth_req_id, const std::string& auth_req_sec, std::string& accessToken);

  std::vector<Sam3KV> m_attributes;

  CSettings* m_settings;
  HttpClient* m_httpClient;
  SsoClient* m_ssoClient;

  bool LineAuth();
  bool BackChannelAuth();
  bool GetAuthMethods();

  std::string m_sam3Url;
  std::string m_sam3ClientId;
  std::string m_authorization_endpoint;
  std::string m_token_endpoint;
  std::string m_userinfo_endpoint;
  std::string m_issuer;
  std::string m_personaToken;
  std::string m_lineAuthUrl;
  std::string m_deviceToken;
  std::string m_authorizeTokensUrl;
  std::string m_refreshToken;
  std::string m_bcAuthStart;
  std::string m_idToken;
  Sam3AuthMethods m_authMethods;
  Sam3AccessTokens m_sam3AccessTokens;
};
