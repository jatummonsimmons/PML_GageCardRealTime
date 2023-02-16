QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

CONFIG += c++11
CONFIG += console;

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ConfigSystem.c \
    CsAppSupport.c \
    dataProcessingThread.cpp \
    gageControlThread.cpp \
    main.cpp \
    mainwindow.cpp \
    qcustomplot.cpp

HEADERS += \
    acquisitionConfig.h \
    dataProcessingThread.h \
    gageControlThread.h \
    mainwindow.h \
    qcustomplot.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    Stream2Analysis.ini \
    Stream2Analysis.ini

win32: LIBS += -L$$PWD/'../../../../../Program Files (x86)/Gage/CompuScope/lib64/' -lCsSsm

INCLUDEPATH += $$PWD/'../../../../../Program Files (x86)/Gage/CompuScope/include'
DEPENDPATH += $$PWD/'../../../../../Program Files (x86)/Gage/CompuScope/include'

win32: LIBS += -L$$PWD/'../../../../../Program Files (x86)/Gage/CompuScope/lib64/' -lCsAppSupport

INCLUDEPATH += $$PWD/'../../../../../Program Files (x86)/Gage/CompuScope/CompuScope C SDK/C Common'
DEPENDPATH += $$PWD/'../../../../../Program Files (x86)/Gage/CompuScope/CompuScope C SDK/C Common'
