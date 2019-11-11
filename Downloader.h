#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>


typedef void CURL;

namespace ZipSync {

struct DownloadSource {
    //URL to download file from
    std::string url;
    //the range of bytes to be downloaded
    uint32_t byterange[2];
};

//called when download is complete
typedef std::function<void(const void*, uint32_t)> DownloadFinishedCallback;
typedef std::function<void(double, const char*)> GlobalProgressCallback;

class Downloader {
    struct Download {
        DownloadSource src;
        DownloadFinishedCallback finishedCallback;
    };
    std::vector<Download> _downloads;
    GlobalProgressCallback _progressCallback;
    struct UrlState {
        int doneCnt = 0;
        std::vector<int> downloadsIds;      //sorted by starting offset
    };
    std::map<std::string, UrlState> _urlStates;

    struct CurlResponse {
        std::string url;

        std::vector<uint8_t> data;
        uint32_t onerange[2] = {UINT_MAX, UINT_MAX};
        std::string boundary;

        double progressRatio = 0.0;         //which portion of this CURL request is done
        int64_t bytesDownloaded = 0;
        double progressWeight = 0.0;        //this request size / total size of all downloads
    };
    std::unique_ptr<CurlResponse> _currResponse;
    double _totalProgress = 0.0;            //which portion of DownloadAll is complete (without current request)
    int64_t _totalBytesDownloaded = 0;      //how many bytes downloaded in total (without current request)

public:
    void EnqueueDownload(const DownloadSource &source, const DownloadFinishedCallback &finishedCallback);
    void SetProgressCallback(const GlobalProgressCallback &progressCallback);
    void DownloadAll();

    int64_t TotalBytesDownloaded() const { return _totalBytesDownloaded; }

private:
    void DownloadAllForUrl(const std::string &url);
    void DownloadOneRequest(const std::string &url, const std::vector<int> &downloadIds);
    void BreakMultipartResponse(const CurlResponse &response, std::vector<CurlResponse> &parts);
    void UpdateProgress();
    size_t BytesToTransfer(const Download &download);
};

}