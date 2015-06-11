#-------------------------------------------------
#
# Project created by QtCreator 2014-04-23T11:38:34
#
#-------------------------------------------------

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

QMAKE_CXXFLAGS += -std=c++0x

TARGET = DownloadToolUpdate
TEMPLATE = app

CONFIG   -= console

DEFINES += NOMINMAX #Macro max、min in QTime and limits.h conflict

SOURCES += main.cpp\
    update.cpp \
    downloadcontrol.cpp

HEADERS  += \
    update.h \
    downloadcontrol.h \
    lib/win32/curl.h \
    lib/win32/curlbuild.h \
    lib/win32/curlrules.h \
    lib/win32/curlver.h \
    lib/win32/easy.h \
    lib/win32/multi.h

FORMS    += \
    Update.ui

win32: LIBS += -L$$PWD/lib/win32 -llibcurl
INCLUDEPATH += $$PWD/lib/win32
DEPENDPATH += $$PWD/lib/win32
