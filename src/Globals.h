/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <string>

struct Magenta2Parameter
{
  std::string config_group_id;
  std::string device_model;
  std::string app_name;
  std::string app_version;
  std::string firmware;
  std::string runtime;
};

static const Magenta2Parameter Magenta2Parameters[2] = {{
                                                //Web Client
                                                "web-mtv",
                                                "",
                                                "",
                                                "",
                                                "",
                                                ""
                                              },
                                              {
                                                //Android TV
                                                "atv-androidtv",
                                                "DT:ATV-AndroidTV",
                                                "MagentaTV",
                                                "104180",
                                                "API level 30",
                                                "1"
                                              }
                                            };
