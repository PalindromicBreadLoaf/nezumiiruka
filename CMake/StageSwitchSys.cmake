# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

file(REMOVE_RECURSE "${DEST}")
file(COPY "${SRC}/" DESTINATION "${DEST}" PATTERN "Themes" EXCLUDE)
file(TOUCH "${STAMP}")
