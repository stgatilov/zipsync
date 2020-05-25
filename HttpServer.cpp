#include "HttpServer.h"
#include "microhttpd.h"
#include "ZSAssert.h"

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
        NULL,
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
    static int dummy;
    MHD_Response *response;
    int ret;

    if (0 != strcmp(method, "GET"))
        return MHD_NO; /* unexpected method */

    if (&dummy != *ptr) {
        // The first time only the headers are valid,
        // do not respond in the first round...
        *ptr = &dummy;
        return MHD_YES;
    }

    if (0 != *upload_data_size)
        return MHD_NO; /* upload data in a GET!? */

    *ptr = NULL; /* clear context pointer */

    const char *PAGE = "<html><head><title>libmicrohttpd demo</title></head><body>libmicrohttpd demo</body></html>";
    response = MHD_create_response_from_buffer(
        strlen(PAGE),
        (void*)PAGE,
        MHD_RESPMEM_PERSISTENT
    );
    ret = MHD_queue_response(
        connection,
        MHD_HTTP_OK,
        response
    );
    MHD_destroy_response(response);
    return ret;
}

}
