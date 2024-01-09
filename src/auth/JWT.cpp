/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "JWT.h"
#include "kodi/tools/StringUtils.h"
#include "../Base64.h"

bool ParseToken(const std::string& token, std::string& header, std::string& payload, std::string& signature)
{
//  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::vector<std::string> token_arr = kodi::tools::StringUtils::Split(token, ".", 3);
  if (token_arr.size() != 3)
    return false;

  header = base64_addpadding(token_arr.at(0));
  payload = base64_addpadding(token_arr.at(1));
  signature = base64_addpadding(token_arr.at(2));

  return true;
}
