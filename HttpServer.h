#pragma once

#include <string>

struct MHD_Daemon;
struct MHD_Connection;

namespace ZipSync {

/**
 * Simple embedded HTTP server.
 * Used only for tests.
 */
class HttpServer {
    std::string _rootDir;
    MHD_Daemon *_daemon = nullptr;

public:
    static const int PORT = 8090;
    HttpServer();
    ~HttpServer();
    HttpServer(const HttpServer &) = delete;
    HttpServer& operator=(const HttpServer &) = delete;

    //set the root directory so serve files inside
    void SetRootDir(const std::string &root);

    void Start();
    void Stop();

private:
    static int MhdFunction(
        void *cls,
        MHD_Connection *connection,
        const char *url,
        const char *method,
        const char *version,
        const char *upload_data,
        size_t *upload_data_size,
        void **ptr
    );
};

}
