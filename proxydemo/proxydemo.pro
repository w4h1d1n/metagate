#-------------------------------------------------
#
# Project created by QtCreator 2018-11-20T18:49:51
#
#-------------------------------------------------

QT       += core gui widgets network  xml

TARGET = proxydemo
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS
CONFIG += c++14

INCLUDEPATH = ./ ./proxy
INCLUDEPATH = ../src

QMAKE_CFLAGS += -std=c99 -Wunused-parameter
SOURCES +=  proxy/http_parser.c

SOURCES += \
        main.cpp \
        mainwindow.cpp \
        \
        ../src/Log.cpp \
        ../src/utils.cpp \
        ../src/Paths.cpp \
        ../src/btctx/Base58.cpp \
        \
        proxy/UPnPDevices.cpp \
        proxy/UPnPRouter.cpp \
    proxy/ProxyServer.cpp \
    proxy/ConnectionRunnable.cpp \
    proxy/ProxyClient.cpp

HEADERS += \
        mainwindow.h \
        ../src/Log.h \
        ../src/utils.h \
        ../src/Paths.h \
        ../src/btctx/Base58.h \
        \
        proxy/UPnPDevices.h \
        proxy/UPnPRouter.h \
    proxy/ProxyServer.h \
    proxy/ConnectionRunnable.h \
    proxy/ProxyClient.h

FORMS += \
        mainwindow.ui
