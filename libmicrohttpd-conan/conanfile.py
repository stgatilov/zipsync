#!/usr/bin/env python
# -*- coding: utf-8 -*-

from conans import ConanFile, AutoToolsBuildEnvironment, tools
import os


class LibmicrohttpdConan(ConanFile):
    name = "libmicrohttpd"
    version = "0.9.59"
    description = "A small C library that is supposed to make it easy to run an HTTP server"
    url = "https://github.com/bincrafters/conan-libmicrohttpd"
    homepage = "https://www.gnu.org/software/libmicrohttpd/"
    license = "LGPL-2.1"
    author = "Bincrafters <bincrafters@gmail.com>"
    exports = ["LICENSE.md"]
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = "shared=False", "fPIC=True"
    source_subfolder = "source_subfolder"
    autotools = None

    def config_options(self):
        if self.settings.os == 'Windows':
            del self.options.fPIC

    def configure(self):
        del self.settings.compiler.libcxx

    def source(self):
        source_url = "https://ftp.gnu.org/gnu/libmicrohttpd"
        tools.get("{0}/libmicrohttpd-{1}.tar.gz".format(source_url, self.version))
        extracted_dir = self.name + "-" + self.version
        os.rename(extracted_dir, self.source_subfolder)

    def configure_autotools(self):
        if self.autotools is None:
            self.autotools = AutoToolsBuildEnvironment(self, win_bash=tools.os_info.is_windows)
            args = ['--disable-static' if self.options.shared else '--disable-shared']
            args.append('--disable-doc')
            args.append('--disable-examples')
            args.append('--disable-curl')
            self.autotools.configure(args=args)
        return self.autotools

    def build(self):
        with tools.chdir(self.source_subfolder):
            autotools = self.configure_autotools()
            autotools.make()

    def package(self):
        self.copy(pattern="COPYING", dst="licenses", src=self.source_subfolder)
        with tools.chdir(self.source_subfolder):
            autotools = self.configure_autotools()
            autotools.install()

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        if self.settings.os == "Linux":
            self.cpp_info.libs.append("pthread")
