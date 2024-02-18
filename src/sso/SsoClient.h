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

class SsoClient
{
public:
  SsoClient(CSettings* settings, HttpClient* httpclient);
  ~SsoClient();

//  bool GetPersonaToken(std::string& personaToken);
  std::string SSOLogin();
  bool SSOAuthenticate(const std::string& code, const std::string& state, std::string& personaToken);

private:

  CSettings* m_settings;
  HttpClient* m_httpClient;

//  std::string m_personaToken;
};
