// SPDX-License-Identifier: GPL-3.0-or-later
// What the phone upload page may put into the library and name in it: file names, relative paths,
// the file types and their first bytes, the size limit. Pure text and numbers, no file system and
// no sockets: the app checks a name here before it touches a file, and a name that does not pass
// never reaches the disk.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// What an uploaded file is. Songs are the library's formats; documents and images are kept for the
// sheet-music reader.
enum class FileKind : std::uint8_t { Song, Document, Image };

// Why a name or path was refused.
enum class NameProblem : std::uint8_t {
    None,
    Empty,      // nothing, or only spaces
    TooLong,    // a part over kMaxNameBytes, a path over kMaxPathBytes, or deeper than kMaxDepth
    BadUtf8,    // not valid UTF-8 (a stray byte, an overlong form, a surrogate)
    Control,    // a NUL, a control character, or an invisible format character (bidi, zero width)
    Separator,  // a path separator, or a character that looks like one (U+2215, U+FF0F, ...)
    Dots,       // ".", "..", a leading dot (hidden), a trailing dot, or a dot lookalike (U+2024)
    NotAllowed, // a file type that is not on the list
};

// Longest single name or folder name in bytes (the file system's limit is 255), the longest
// relative path, and the most folders deep.
inline constexpr std::size_t kMaxNameBytes = 160;
inline constexpr std::size_t kMaxPathBytes = 1024;
inline constexpr std::size_t kMaxDepth = 8;

// The largest upload in bytes. A phone photo is under 15 MB and a score under a few, so 32 MiB
// holds every real file and a full disk is not one request away.
inline constexpr std::uint64_t kMaxUploadBytes = 32ull * 1024 * 1024;

// How many bytes of a file's start `header_matches` reads at most (the text formats look for their
// first line in them); `sniff_window` says how many a type needs before it can be judged.
inline constexpr std::size_t kSniffBytes = 512;
std::size_t sniff_window(std::string_view extension);

struct CleanName {
    NameProblem problem = NameProblem::None;
    std::string name; // the name to use; empty when `problem` is set
    bool ok() const { return problem == NameProblem::None; }
};

// A file name from a request, made safe to create: spaces at either end are cut, and the
// characters Windows and the network share cannot hold (: * ? " < > |) become '_'. It refuses
// (never repairs) a path separator or a lookalike of one, a control or invisible character, bad
// UTF-8, a name starting or ending with a dot and one that is too long, so "../a.mid",
// "/etc/a.mid", "a\\b.mid" and "a\0.mid" all fail whole. The file type is not judged here
// (`check_upload_type`).
CleanName clean_file_name(std::string_view raw);

// The extension of a name, lower case with its dot (".mid"), or empty.
std::string lower_extension(std::string_view name);

// True for the song formats the library lists (.mid .midi .kar .smf .rmi .musicxml .xml .mxl .mscz
// .mscx .abc); `extension` is lower case with its dot.
bool is_song_extension(std::string_view extension);

// The kind of a file from its name, or none when the type is not on the list: the song formats,
// .pdf, and the images .png .jpg .jpeg .heic .heif.
std::optional<FileKind> kind_of_name(std::string_view name);

// A clean name whose type is on the list, or NotAllowed.
CleanName check_upload_type(std::string_view raw);

// Whether a file's first bytes fit its type: a MIDI file starts "MThd" (or is RIFF for .rmi), a PDF
// "%PDF", a PNG, JPEG or HEIC has its signature, .mxl and .mscz are zips, a MusicXML file starts
// (after a byte order mark and spaces) with "<?xml" or "<score-partwise" or "<score-timewise", a
// MuseScore .mscx with "<?xml" or "<museScore", and an ABC file is plain text with an "X:" or "K:"
// header line among its first bytes. `head` is up to kSniffBytes; fewer than `sniff_window` bytes
// are judged as they are at the end of the file, so a file shorter than the window can pass.
bool header_matches(std::string_view extension, std::span<const std::uint8_t> head);

// A path inside the library as a request names it: parts joined by '/', no leading '/'.
struct RelPath {
    NameProblem problem = NameProblem::None;
    std::string path; // the parts joined by '/'; empty is the library's top folder
    std::vector<std::string> parts;
    bool ok() const { return problem == NameProblem::None; }
};

// Checks every part with the name rules above, except that ": * ? \" < > |" stay as they are (a
// folder the player made on the Mac may hold them) and an empty `raw` is the top folder when
// `allow_root`. An absolute path, an empty part ("a//b"), "." and "..", a hidden part and more than
// kMaxDepth parts all fail.
RelPath clean_relative_path(std::string_view raw, bool allow_root);

// Text for a JSON string value (without the quotes): quotes, backslashes, control characters and
// < > & escaped, so a file name can never break out of the page or its data.
std::string json_escape(std::string_view text);

} // namespace etude
