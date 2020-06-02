#include "HttpServer.h"
#include "microhttpd.h"
#include "ZSAssert.h"
#include "Path.h"
#include "Utils.h"

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
    FileDownload() : file(NULL) {}
};

ssize_t HttpServer::FileReaderCallback(void *cls, uint64_t pos, char *buf, size_t max) {
    auto *down = (FileDownload*)cls;
    fseek(down->file, SEEK_SET, pos);
    size_t readBytes = fread(buf, 1, max, down->file);
    return readBytes;
}

void HttpServer::FileReaderFinalize(void *cls) {
    auto *down = (FileDownload*)cls;
    delete down;
}

#define NOT_FOUND_PAGE "<html><head><title>File not found</title></head><body>File not found</body></html>"

int HttpServer::AcceptCallback(
    MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version
) const {

    std::string filepath = _rootDir + url;

    StdioFileHolder file(fopen(filepath.c_str(), "rb"));
    if (!file) {
        MHD_Response *response = MHD_create_response_from_buffer(
            strlen(NOT_FOUND_PAGE),
            (void*)NOT_FOUND_PAGE,
            MHD_RESPMEM_PERSISTENT
        );
        int ret = MHD_queue_response(
            connection,
            MHD_HTTP_NOT_FOUND,
            response
        );
        return ret;
    }

    fseek(file, 0, SEEK_END);
    size_t fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    FileDownload *down = new FileDownload();
    down->file = file.release();
    MHD_Response *response = MHD_create_response_from_callback(fsize, 128 * 1024, FileReaderCallback, down, FileReaderFinalize);
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
