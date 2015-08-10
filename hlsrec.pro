TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.c

include(deployment.pri)
qtcAddDeployment()

LIBS += -lasound -lmp3lame

# Setup to install the target files automatically on deployment state of the qt creator
target.files = hlsrec
target.path= /usr/bin

INSTALLS = target
