/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/AddonBase.h>

class ATTR_DLL_LOCAL CSettings
{
public:
  CSettings() = default;

  bool Load();
  ADDON_STATUS SetSetting(const std::string& settingName, const std::string& settingValue);
  bool VerifySettings();

//  const std::string& GetMagentaUsername() const { return m_magentaUsername; }
//  const std::string& GetMagentaPassword() const { return m_magentaPassword; }
  const int& GetTerminalType() const { return m_terminaltype; }
  const std::string& GetMagentaEPGToken() const { return m_epgToken; }
  const std::string& GetMagentaOpenIDToken() const { return m_openidToken; }
  const std::string& GetMagentaTVToken() const { return m_tvToken; }
  const std::string& GetMagentaRefreshToken() const { return m_refreshToken; }
  const std::string& GetMagentaCSRFToken() const { return m_csrfToken; }
  const std::string& GetMagentaDeviceID() const { return m_magentaDeviceID; }
  const int& GetStartNum() const { return m_start_num; }
  const bool HideUnsubscribed() const { return m_hideunsubscribed; }
  const bool PreferHigherResolution() const { return m_higherresolution; }
  const bool IsGroupsenabled() const  { return m_enablegroups; }

private:
//  std::string m_magentaUsername;
//  std::string m_magentaPassword;
  std::string m_epgToken;
  std::string m_openidToken;
  std::string m_tvToken;
  std::string m_refreshToken;
  std::string m_csrfToken;
  std::string m_magentaDeviceID;
  int m_start_num;
  int m_terminaltype;
  bool m_hideunsubscribed;
  bool m_higherresolution;
  bool m_enablegroups;
};
