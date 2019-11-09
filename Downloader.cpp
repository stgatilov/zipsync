#include "Downloader.h"
#include <curl/curl.h>
#include <algorithm>
#include "ZSAssert.h"
#undef min
#undef max


//try to avoid CURL requests of total size less than this
static const int DESIRED_REQUEST_SIZE = 10<<20;
//forbid multipart requests with more that this number of chunks
static const int MAX_PARTS_PER_REQUEST = 20;
//overhead per download in bytes --- for progress callback only
static const int ESTIMATED_DOWNLOAD_OVERHEAD = 100;

namespace ZipSync {

void Downloader::EnqueueDownload(const DownloadSource &source, const DownloadFinishedCallback &finishedCallback) {
    _downloads.push_back(Download{source, finishedCallback});
}
void Downloader::SetProgressCallback(const GlobalProgressCallback &progressCallback) {
    _progressCallback = progressCallback;
}

void Downloader::DownloadAll() {
    _progressCallback(0.0, "Downloading started");

    for (int i = 0; i <  _downloads.size(); i++)
        _urlStates[_downloads[i].src.url].downloadsIds.push_back(i);
    for (auto &pKV : _urlStates) {
        std::string url = pKV.first;
        std::vector<int> &ids = pKV.second.downloadsIds;
        std::sort(ids.begin(), ids.end(), [this](int a, int b) {
           return _downloads[a].src.byterange[0] < _downloads[b].src.byterange[0];
        });
    }

    for (const auto &pKV : _urlStates) {
        std::string url = pKV.first;
        DownloadAllForUrl(url);
    }

    _progressCallback(1.0, "Downloading finished");
}

void Downloader::DownloadAllForUrl(const std::string &url) {
    UrlState &state = _urlStates.find(url)->second;
    int n = state.downloadsIds.size();

    while (state.doneCnt < n) {
        uint32_t totalSize = 0;
        int rangesCnt = 0;
        int end = state.doneCnt;
        uint32_t last = UINT_MAX;
        std::vector<int> ids;
        do {
            int idx = state.downloadsIds[end++];
            const Download &down = _downloads[idx];
            ids.push_back(idx);
            totalSize += down.src.byterange[1] - down.src.byterange[0];
            rangesCnt += (last != down.src.byterange[0]);
            last = down.src.byterange[1];
        } while (end < n && rangesCnt < MAX_PARTS_PER_REQUEST && totalSize < DESIRED_REQUEST_SIZE);

        DownloadOneRequest(url, ids);

        state.doneCnt = end;
    }
}

void Downloader::DownloadOneRequest(const std::string &url, const std::vector<int> &downloadIds) {
    if (downloadIds.empty())
        return;

    std::vector<std::pair<uint32_t, uint32_t>> coaslescedRanges;
    for (int idx : downloadIds) {
        const auto &down = _downloads[idx];
        if (!coaslescedRanges.empty() && coaslescedRanges.back().second == down.src.byterange[0])
            coaslescedRanges.back().second = down.src.byterange[1];
        else
            coaslescedRanges.push_back(std::make_pair(down.src.byterange[0], down.src.byterange[1]));
    }
    std::string byterangeStr;
    for (auto rng : coaslescedRanges) {
        if (!byterangeStr.empty())
            byterangeStr += ",";
        byterangeStr += std::to_string(rng.first) + "-" + std::to_string(rng.second - 1);
    }

    int64_t totalEstimate = 0;
    int64_t thisEstimate = 0;
    for (int idx : downloadIds)
        thisEstimate += BytesToTransfer(_downloads[idx]);
    for (const auto &down : _downloads)
        totalEstimate += BytesToTransfer(down);

    auto header_callback = [](char *buffer, size_t size, size_t nitems, void *userdata) {
        size *= nitems;
        auto &resp = *((Downloader*)userdata)->_currResponse;
        std::string str(buffer, buffer + size);
        size_t from, to, all;
        if (sscanf(str.c_str(), "Content-Range: bytes %zu-%zu/%zu", &from, &to, &all) == 3) {
            resp.onerange[0] = from;
            resp.onerange[1] = to + 1;
        }
        char boundary[128] = {0};
        if (sscanf(str.c_str(), "Content-Type: multipart/byteranges; boundary=%s", boundary) == 1) {
            resp.boundary = std::string("\r\n--") + boundary;// + "\r\n";
        }
        return size;
    };
    auto write_callback = [](char *buffer, size_t size, size_t nitems, void *userdata) -> size_t {
        size *= nitems;
        auto &resp = *((Downloader*)userdata)->_currResponse;
        if (resp.onerange[0] == resp.onerange[1] && resp.boundary.empty())
            return 0;  //neither range not multipart response -> halt
        resp.data.insert(resp.data.end(), buffer, buffer + size);
        return size;
    };
    auto xferinfo_callback = [](void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
        auto &resp = *((Downloader*)userdata)->_currResponse;
        if (dltotal > 0 && dlnow > 0) {
            resp.progressRatio = double(dlnow) / std::max(dltotal, dlnow);
            resp.bytesDownloaded = dlnow;
            ((Downloader*)userdata)->UpdateProgress();
        }
        return 0;
    };
    _currResponse.reset(new CurlResponse());
    _currResponse->url = url;
    _currResponse->progressWeight = double(thisEstimate) / totalEstimate;
    std::unique_ptr<CURL, void (*)(CURL*)> curl(curl_easy_init(), curl_easy_cleanup);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_RANGE, byterangeStr.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, (curl_write_callback)write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, (curl_write_callback)header_callback);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, this);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, (curl_xferinfo_callback)xferinfo_callback);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0);
    UpdateProgress();
    CURLcode ret = curl_easy_perform(curl.get());
    curl.reset();
    ZipSyncAssertF(ret != CURLE_WRITE_ERROR, "Response without byteranges for URL %s", url.c_str());
    ZipSyncAssertF(ret == CURLE_OK, "Unexpected CURL error %d on URL %s", ret, url.c_str());
    _currResponse->progressRatio = 1.0;
    UpdateProgress();

    std::vector<CurlResponse> results;
    if (_currResponse->boundary.empty())
        results.push_back(std::move(*_currResponse));
    else
        BreakMultipartResponse(*_currResponse, results);

    _totalBytesDownloaded += _currResponse->bytesDownloaded;
    _totalProgress += _currResponse->progressWeight;
    _currResponse.reset();

    std::sort(results.begin(), results.end(), [](const CurlResponse &a, const CurlResponse &b) {
        return a.onerange[0] < b.onerange[0];
    });
    for (int idx : downloadIds) {
        const auto &downSrc = _downloads[idx].src;
        uint32_t totalSize = downSrc.byterange[1] - downSrc.byterange[0];
        std::vector<uint8_t> answer;
        answer.reserve(totalSize);
        for (const auto &resp : results) {
            uint32_t currPos = downSrc.byterange[0] + (uint32_t)answer.size();
            uint32_t left = std::max(currPos, resp.onerange[0]);
            uint32_t right = std::min(downSrc.byterange[1], resp.onerange[1]);
            if (right <= left)
                continue;
            ZipSyncAssertF(left == currPos, "Missing chunk %u..%u (%u bytes) after downloading URL %s", left, currPos, currPos - left, url.c_str());
            answer.insert(answer.end(),
                resp.data.data() + (left - resp.onerange[0]),
                resp.data.data() + (right - resp.onerange[0])
            );
        }
        ZipSyncAssertF(answer.size() == totalSize, "Missing end chunk %zu..%u (%u bytes) after downloading URL %s", answer.size(), totalSize, totalSize - (uint32_t)answer.size(), url.c_str());
        _downloads[idx].finishedCallback(answer.data(), answer.size());
    }
}

void Downloader::BreakMultipartResponse(const CurlResponse &response, std::vector<CurlResponse> &parts) {
    const auto &data = response.data;
    const std::string &bound = response.boundary;

    //find all occurences of boundary
    std::vector<size_t> boundaryPos;
    for (size_t pos = 0; pos + bound.size() <= data.size(); pos++)
        if (memcmp(&data[pos], &bound[0], bound.size()) == 0)
            boundaryPos.push_back(pos);

    for (size_t i = 0; i+1 < boundaryPos.size(); i++) {
        size_t left = boundaryPos[i] + bound.size() + 2;        //+2 for "\r\n" or "--"
        size_t right = boundaryPos[i+1];

        //parse header into sequence of lines
        std::vector<std::string> header;
        size_t lineStart = left;
        while (1) {
            size_t lineEnd = lineStart;
            while (strncmp((char*)&data[lineEnd], "\r\n", 2))
                lineEnd++;
            header.emplace_back((char*)&data[lineStart], (char*)&data[lineEnd]);
            lineStart = lineEnd + 2;
            if (header.back().empty())
                break;  //empty line: header has ended
        }

        //find range in headers
        CurlResponse part;
        for (const auto &h : header) {
            size_t from, to, all;
            if (sscanf(h.c_str(), "Content-Range: bytes %zu-%zu/%zu", &from, &to, &all) == 3) {
                part.onerange[0] = from;
                part.onerange[1] = to + 1;
            }
        }
        ZipSyncAssertF(part.onerange[0] != part.onerange[1], "Failed to find range in part headers");

        part.data.assign(&data[lineStart], &data[right]);
        parts.push_back(std::move(part));
    }
}

void Downloader::UpdateProgress() {
    char buffer[256] = "Downloading...";
    double progress = _totalProgress;
    if (_currResponse) {
        sprintf(buffer, "Downloading \"%s\"...", _currResponse->url.c_str());
        progress += _currResponse->progressWeight * _currResponse->progressRatio;
    }
    _progressCallback(progress, buffer);
}

size_t Downloader::BytesToTransfer(const Download &download) {
    return size_t(download.src.byterange[1] - download.src.byterange[0]) + ESTIMATED_DOWNLOAD_OVERHEAD;
}

}
