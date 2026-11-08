// SPDX-License-Identifier: GPL-3.0-or-later
// Stands in for Verovio's filereader.cpp. That file embeds its own copy of miniz (global mz_*
// symbols with an older struct layout), which collides with core's miniz at link time. Core reads
// zips and hands Verovio plain XML, so Verovio never opens one.
#include "filereader.h"

namespace vrv {
ZipFileReader::ZipFileReader() : m_file(nullptr) {}
ZipFileReader::~ZipFileReader() {}
void ZipFileReader::Reset() {}
bool ZipFileReader::Load(const std::string&) { return false; }
bool ZipFileReader::LoadBytes(const std::vector<unsigned char>&) { return false; }
bool ZipFileReader::HasFile(const std::string&) { return false; }
std::string ZipFileReader::ReadTextFile(const std::string&) { return {}; }
std::list<std::string> ZipFileReader::GetFileList() const { return {}; }
} // namespace vrv
