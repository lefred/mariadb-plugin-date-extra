# Copyright (c) 2019, MariaDB corporation. All rights reserved.
# Copyright (c) 2026, lefred (Frédéric Descamps). All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.

MYSQL_ADD_PLUGIN(date_extra date_extra.cc MODULE_ONLY
                 RECOMPILE_FOR_EMBEDDED)
