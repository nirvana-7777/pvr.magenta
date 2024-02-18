/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "JWT.h"
#include "kodi/tools/StringUtils.h"
#include "rapidjson/document.h"
#include "../Base64.h"
#include "../Utils.h"


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

bool IsJWTexpired(const std::string& token)
{
//  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string header;
  std::string payload;
  std::string signature;

//  kodi::Log(ADDON_LOG_DEBUG, "[Taa] Token to verify: %s", token.c_str());

  if (!ParseToken(token, header, payload, signature))
  {
//    kodi::Log(ADDON_LOG_ERROR, "[Auth] Token Parse error");
    return true;
  }

  std::string decPayload = base64_decode(payload);

//  kodi::Log(ADDON_LOG_DEBUG, "[Taa] payload: %s decoded %s", payload.c_str(), decPayload.c_str());

  rapidjson::Document doc;
  doc.Parse(decPayload.c_str());
  if (doc.GetParseError())
  {
//    kodi::Log(ADDON_LOG_ERROR, "[Auth] JWTexpired JSON parse error for %s", decPayload.c_str());
    return true;
  }

  time_t tokenExp = Utils::JsonIntOrZero(doc, "exp");

//  kodi::Log(ADDON_LOG_DEBUG, "[Auth] Test if expired: %u", tokenExp);

  bool isExpired = (time(NULL) > tokenExp);
//  if (isExpired)
//    kodi::Log(ADDON_LOG_DEBUG, "[Auth] JWT is expired!");

  return isExpired;
}
