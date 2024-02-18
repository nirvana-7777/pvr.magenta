/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <string>

static const int TERMINAL_ANDROIDTV = 0;
static const int TERMINAL_MAGENTASTICK = 1;
static const int TERMINAL_WEB = 2;
static const int TERMINAL_ANDROIDMOBILE = 3;

struct Magenta2Parameter
{
  std::string config_group_id;
  std::string device_model;
  std::string device_name;
  std::string app_name;
  std::string app_version;
  std::string firmware;
  std::string runtime;
  std::string user_agent;
};

static const Magenta2Parameter Magenta2Parameters[4] = {{
                                                //Android TV
                                                "atv-androidtv",
                                                "DT:ATV-AndroidTV",
                                                "SHIELD Android TV",
                                                "MagentaTV",
                                                "104180",
                                                "API level 30",
                                                "1",
                                                "Dalvik/2.1.0 (Linux; U; Android 11; SHIELD Android TV Build/RQ1A.210105.003) ((2.00T_ATV::3.134.4462::mdarcy::FTV_OTT_DT))"
                                              },
                                              {
                                                //Magenta TV Stick
                                                "atv-launcher",
                                                "DT:ATV-Launcher",
                                                "MagentaTV Stick",
                                                "MagentaTV",
                                                "104682",
                                                "API level 30",
                                                "1",
                                                "okhttp/5.0.0-alpha.11"
                                              },
                                              {
                                                //Web Client
                                                "web-mtv",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                ""
                                              },
                                              {
                                                //Android Mobile
                                                "android-mobile",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                ""
                                              }
                                            };
