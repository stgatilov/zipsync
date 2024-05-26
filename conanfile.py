from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMake

class ZipsyncConan(ConanFile):
    name = "zipsync"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    requires = [
        "zlib/1.2.11",      # TODO: update
        "minizip/1.2.11",   # TODO: update
        "libcurl/8.6.0",
        "doctest/2.4.11",
        "blake2/20190724@zipsync",
        "args/6.4.6@zipsync",
        "libmicrohttpd/0.9.77",
    ]

    def layout(self):
        cmake_layout(self)        

    def configure(self):
        self.options["minizip"].bzip2 = False
        self.options["libcurl"].with_ssl = False
        self.options["BLAKE2"].SSE = "SSE2"

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
