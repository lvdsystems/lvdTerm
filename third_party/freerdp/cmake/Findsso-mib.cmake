# - Find sso-mib
# Find the sso-mib library

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright 2025 Siemens

find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_SSO_MIB sso-mib>=0.5.0 REQUIRED IMPORTED_TARGET)

add_library(sso-mib ALIAS PkgConfig::PC_SSO_MIB)
