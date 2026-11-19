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
#include "docdetect.h"
#include "encoding.h"

#include <cwchar>

namespace {

bool endsWithXmlExtension(const wchar_t* path)
{
    if (!path || !*path) return false;
    const wchar_t* dot = nullptr;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'.') dot = p;
        else if (*p == L'\\' || *p == L'/') dot = nullptr;
    }
    if (!dot) return false;

    const wchar_t* const extensions[] = {
        L".xml",   L".xsd",     L".xsl",   L".xslt",    L".svg",   L".config",
        L".csproj", L".vbproj", L".vcxproj", L".props", L".targets", L".resx",
        L".nuspec", L".xaml",   L".plist", L".wsdl",    L".rss",   L".atom",
        L".xhtml"};
    for (const wchar_t* ext : extensions) {
        const wchar_t* a = dot;
        const wchar_t* b = ext;
        while (*a && *b) {
            wchar_t ca = *a;
            if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca - L'A' + L'a');
            if (ca != *b) break;
            ++a;
            ++b;
        }
        if (*a == 0 && *b == 0) return true;
    }
    return false;
}

bool startsWithProlog(DocumentView doc)
{
    uint32_t i = 0;
    if (detectEncoding(doc.data, doc.length) == Encoding::Utf8Bom) i = 3;
    while (i < doc.length && (doc.data[i] == ' ' || doc.data[i] == '\t' || doc.data[i] == '\r' ||
                             doc.data[i] == '\n')) {
        ++i;
    }
    const char prolog[] = "<?xml";
    for (uint32_t k = 0; k < 5; ++k) {
        if (i + k >= doc.length || doc.data[i + k] != prolog[k]) return false;
    }
    return true;
}

} // namespace

bool looksLikeXml(DocumentView doc, const wchar_t* path)
{
    if (endsWithXmlExtension(path)) return true;
    return startsWithProlog(doc);
}
