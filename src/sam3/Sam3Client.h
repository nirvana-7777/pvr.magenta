/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "../Settings.h"
#include "../http/HttpClient.h"

static const std::string SSO_URL = "https://ssom.magentatv.de/";

struct Sam3KV
{
  std::string name;
  std::string value;
};

class Sam3Client
{
public:
  Sam3Client(CSettings* settings, HttpClient* httpclient);
  ~Sam3Client();

//  void SetSam3ClientId(const std::string& id);
  bool InitSam3(const std::string& url, const std::string& clientId);
  bool Sam3Login();

  std::string GetPersonaToken() {
    return m_personaToken;
  }

private:
  void ParseHtml(const std::string& result);
  std::string SSOLogin();
  bool SSOAuthenticate(const std::string& code, const std::string& state);

  std::vector<Sam3KV> m_attributes;

  CSettings* m_settings;
  HttpClient* m_httpClient;

  bool LineAuth();

//  std::string m_sam3Url;
  std::string m_sam3ClientId;
  std::string m_authorization_endpoint;
  std::string m_issuer;
  std::string m_personaToken;
};
