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

  const int& GetTerminalType() const { return m_terminaltype; }
  const int& GetDeleteMode() const { return m_deletemode; }
  const int& GetDeleteModeSeries() const { return m_deletemodeseries; }
  const std::string& GetMagentaEPGToken() const { return m_epgToken; }
  const std::string& GetMagentaOpenIDToken() const { return m_openidToken; }
  const std::string& GetMagentaTVToken() const { return m_tvToken; }
  const std::string& GetMagentaRefreshToken() const { return m_refreshToken; }
  const std::string& GetMagentaCSRFToken() const { return m_csrfToken; }
  const std::string& GetMagenta2PersonalToken() const { return m_personalToken; }
  const std::string& GetMagentaDeviceID() const { return m_magentaDeviceID; }
  const std::string& GetMagentaUsername() const { return m_userName; }
  const std::string& GetMagentaPassword() const { return m_password; }
  const int& GetStartNum() const { return m_start_num; }
  const bool HideUnsubscribed() const { return m_hideunsubscribed; }
  const bool UseCustomChannels() const { return m_usecustomchannels; }
  const bool UseWhiteLogos() const { return m_whitelogos; }
  const bool IsOnlyCloud() const { return m_onlycloud; }
  const bool IsHiddenDeactivated() const { return m_deactivatehidden; }
  const bool PreferHigherResolution() const { return m_higherresolution; }
  const bool IsGroupsenabled() const  { return m_enablegroups; }
  const bool IsMagenta2() const { return m_ismagenta2; }

private:
  std::string m_epgToken;
  std::string m_openidToken;
  std::string m_tvToken;
  std::string m_refreshToken;
  std::string m_csrfToken;
  std::string m_personalToken;
  std::string m_magentaDeviceID;
  std::string m_userName;
  std::string m_password;
  int m_start_num;
  int m_terminaltype;
  int m_deletemode;
  int m_deletemodeseries;
  bool m_hideunsubscribed;
  bool m_usecustomchannels;
  bool m_whitelogos;
  bool m_onlycloud;
  bool m_deactivatehidden;
  bool m_higherresolution;
  bool m_enablegroups;
  bool m_ismagenta2;
};
