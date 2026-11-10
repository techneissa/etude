// SPDX-License-Identifier: GPL-3.0-or-later
// Every failure text the update code can show, for the tests of the words (update_status_test.cpp)
// and the audit of the room they need.
#pragma once

#include "../app/update_status.hpp"

#include <vector>

namespace etude::app {

inline FetchResult fetched(int status, const char* reason, const char* detail = "") {
    FetchResult r;
    r.status = status;
    r.reason = reason;
    r.detail = detail;
    return r;
}

inline std::vector<FailureText> every_failure_text() {
    std::vector<FailureText> all;
    for (const int status : {400, 401, 403, 404, 410, 429, 500, 502, 503}) {
        all.push_back(fetch_failure_text(fetched(status, "")));
    }
    for (const char* reason :
         {"", "No connection", "The site took too long", "The file is too large",
          "The site's certificate could not be checked", "Cancelled", "Something else"}) {
        all.push_back(fetch_failure_text(fetched(0, reason, "a cause")));
        all.push_back(fetch_failure_text(fetched(0, reason)));
    }
    for (const SignatureError e :
         {SignatureError::KeyNotSet, SignatureError::BadKey, SignatureError::BadSignatureFile,
          SignatureError::UnsupportedAlgorithm, SignatureError::WrongKeyId,
          SignatureError::BadSignature, SignatureError::BadTrustedCommentSignature,
          SignatureError::TrustedCommentMismatch}) {
        all.push_back(signature_failure_text(e));
    }
    all.push_back(space_failure_text(1));
    all.push_back(space_failure_text(24ull * 1024 * 1024));
    for (const char* reason :
         {"", "something odd", "No space left on device", "the signature does not match",
          "data could not be backed up", "the new version did not reach its first frame",
          "the app is still running"}) {
        all.push_back(helper_failure_text("1.2.0", reason));
        all.push_back(helper_failure_text("10.20.30", reason));
    }
    all.push_back(helper_failure_text("", ""));
    // What the app itself says in the install path.
    all.push_back({"The download is not the size the release said", "Wrong download size"});
    all.push_back({"The download could not be unpacked", "Download damaged"});
    all.push_back({"The download was refused: x", "Download refused"});
    all.push_back({"The update request could not be written", "Couldn't start the update"});
    all.push_back({"The installer did not pick the update up", "Installer not responding"});
    all.push_back({"The download could not be saved for the installer: x", "Couldn't save the download"});
    all.push_back({"GitHub's answer could not be read", "Unreadable answer"});
    all.push_back({"The release's version could not be read", "Unreadable version"});
    return all;
}

} // namespace etude::app
