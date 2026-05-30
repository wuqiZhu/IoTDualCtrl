QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    cJSON.cpp \
    dht11_thread.cpp \
    main.cpp \
    mainwindow.cpp \
    rpc_client.cpp

HEADERS += \
    cJSON.h \
    dht11_thread.h \
    mainwindow.h \
    rpc_client.h

FORMS += \
    mainwindow.ui

# Cross-compile configuration
# INCLUDEPATH += /path/to/your/toolchain/sysroot/usr/include

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
