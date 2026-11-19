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
#include "filter.h"

#include <cctype>

namespace {

inline char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

std::vector<size_t> filterByName(const ElementIndex& idx, DocumentView doc, std::string_view needle,
                                 size_t maxMatches)
{
    std::vector<size_t> matches;
    if (needle.empty() || !doc.data) return matches;

    for (size_t i = 0; i < idx.size(); ++i) {
        const ElementPos& e = idx.at(i);
        if (e.nameLen < needle.size()) continue;
        if (e.nameOff + e.nameLen > doc.length) continue;

        const char* name = doc.data + e.nameOff;
        const size_t limit = static_cast<size_t>(e.nameLen) - needle.size();
        for (size_t at = 0; at <= limit; ++at) {
            size_t k = 0;
            while (k < needle.size() && lower(name[at + k]) == lower(needle[k])) ++k;
            if (k == needle.size()) {
                matches.push_back(i);
                break;
            }
        }
        if (matches.size() >= maxMatches) break;
    }
    return matches;
}
