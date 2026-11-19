// XML Tree -- a dockable XML tree panel for Notepad++.
// Copyright (C) 2026 FalcoNefasto
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#pragma once
// Name search over the flat index. No document scan: every element already carries
// the offset and length of its name, so a full pass reads six bytes per element.
#include "document.h"
#include "index.h"

#include <cstddef>
#include <string_view>
#include <vector>

// Indices of the elements whose name contains `needle`, case-insensitively, in
// document order. An empty needle matches nothing. Stops after `maxMatches`.
std::vector<size_t> filterByName(const ElementIndex& idx, DocumentView doc, std::string_view needle,
                                 size_t maxMatches = 5000);
