#include "HttpServer.h"
#include "microhttpd.h"
#include "ZSAssert.h"
#include "Path.h"
#include "Utils.h"
#include "StdString.h"

namespace ZipSync {

HttpServer::~HttpServer() {
    Stop();
}
HttpServer::HttpServer() {}
void HttpServer::SetRootDir(const std::string &root) {
    _rootDir = root;
}

void HttpServer::Stop() {
    if (!_daemon)
        return;
    MHD_stop_daemon(_daemon);
    _daemon = nullptr;
}

void HttpServer::Start() {
    if (_daemon)
        return;
    _daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION,
        PORT,
        NULL,
        NULL,
        &MhdFunction,
        this,
        MHD_OPTION_END
    );
    ZipSyncAssertF(_daemon, "Failed to start microhttpd on port %d", PORT);
}

int HttpServer::MhdFunction(
    void *cls,
    MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **ptr
) {
    //only accept GET requests
    if (0 != strcmp(method, "GET"))
        return MHD_NO;

    static int dummy;
    if (*ptr != &dummy) {
        //this call only shows headers
        *ptr = &dummy;
        return MHD_YES;
    }
    //clear back to initial state
    *ptr = 0;

    return ((HttpServer*)cls)->AcceptCallback(connection, url, method, version);
}

struct HttpServer::FileDownload {
    StdioFileHolder file;
    uint64_t base = 0;
    FileDownload() : file(NULL) {}
};

ssize_t HttpServer::FileReaderCallback(void *cls, uint64_t pos, char *buf, size_t max) {
    auto *down = (FileDownload*)cls;
    fseek(down->file, down->base + pos, SEEK_SET);
    size_t readBytes = fread(buf, 1, max, down->file);
    return readBytes;
}

void HttpServer::FileReaderFinalize(void *cls) {
    auto *down = (FileDownload*)cls;
    delete down;
}

#define PAGE_NOT_FOUND "<html><head><title>File not found</title></head><body>File not found</body></html>"
#define PAGE_NOT_SATISFIABLE "<html><head><title>Range error</title></head><body>Range not satisfiable</body></html>"

static int ReturnWithErrorResponse(MHD_Connection *connection, int httpCode, const char *content) {
    MHD_Response *response = MHD_create_response_from_buffer(
        strlen(content),
        (void*)content,
        MHD_RESPMEM_MUST_COPY
    );
    int ret = MHD_queue_response(
        connection,
        MHD_HTTP_NOT_FOUND,
        response
    );
    return ret;
}

int HttpServer::AcceptCallback(
    MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version
) const {

    std::string filepath = _rootDir + url;

    StdioFileHolder file(fopen(filepath.c_str(), "rb"));
    if (!file)
        return ReturnWithErrorResponse(connection, MHD_HTTP_NOT_FOUND, PAGE_NOT_FOUND);

    fseek(file, 0, SEEK_END);
    uint64_t fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint64_t flast = fsize - 1; 

    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    if (const char *rangeStr = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Range")) {
        if (strnicmp(rangeStr, "bytes=", 6) != 0)
            return ReturnWithErrorResponse(connection, MHD_HTTP_RANGE_NOT_SATISFIABLE, PAGE_NOT_SATISFIABLE);
        bool bad = false;
        std::vector<std::string> segs;
        stdext::split(segs, rangeStr + 6, ",");
        for (const std::string &s : segs) {
            int len = s.size();
            int pos = s.find('-');
            if (pos < 0 || pos == 0) {
                bad = true;
                break;
            }
            std::string fromStr = s.substr(0, pos);
            std::string toStr = s.substr(pos+1);
            uint64_t from = 0;
            uint64_t to = UINT64_MAX;
            if (sscanf(fromStr.c_str(), "%llu", &from) != 1)
                bad = true;
            if (sscanf(toStr.c_str(), "%llu", &to) != 1)
                to = fsize - 1;
            ranges.push_back(std::make_pair(from, to));
        }
        if (bad)
            return ReturnWithErrorResponse(connection, MHD_HTTP_RANGE_NOT_SATISFIABLE, PAGE_NOT_SATISFIABLE);
        for (int i = 0; i < ranges.size(); i++) {
            if (i && ranges[i-1].second >= ranges[i].first)
                bad = true;
            if (ranges[i].first > ranges[i].second)
                bad = true;
            if (ranges[i].second >= fsize)
                bad = true;
        }
        if (bad)
            return ReturnWithErrorResponse(connection, MHD_HTTP_RANGE_NOT_SATISFIABLE, PAGE_NOT_SATISFIABLE);
    }

    if (ranges.size() > 1)
        return ReturnWithErrorResponse(connection, MHD_HTTP_RANGE_NOT_SATISFIABLE, PAGE_NOT_SATISFIABLE);
    std::unique_ptr<FileDownload> down(new FileDownload());
    MHD_Response *response = nullptr;
    if (ranges.size() == 0) {
        down->file = file.release();
        response = MHD_create_response_from_callback(fsize, 128 * 1024, FileReaderCallback, down.release(), FileReaderFinalize);
    }
    else if (ranges.size() == 1) {
        down->file = file.release();
        down->base = ranges[0].first;
        response = MHD_create_response_from_callback(ranges[0].second - ranges[0].first + 1, 128 * 1024, FileReaderCallback, down.release(), FileReaderFinalize);
    }
    if (!response)
        return MHD_NO;

    int ret = MHD_queue_response(
        connection,
        MHD_HTTP_OK,
        response
    );
    MHD_destroy_response(response);

    return ret;
}

}
