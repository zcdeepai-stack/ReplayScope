QT += core gui widgets network printsupport

CONFIG += c++17

# 版本号唯一来源：根目录的 VERSION.txt。
# qmake / Inno Setup / 打包脚本都从这里取，避免多处手改导致版本不一致。
#
# 注意：文件名必须带扩展名。项目根目录在 -I 包含路径里，若文件名叫 VERSION，
# Windows 文件系统不区分大小写，Qt 头里的 #include <version>（C++20 标准头）
# 会命中它，导致整套 Qt 头编译崩溃。
APP_VERSION = $$cat($$PWD/VERSION.txt, lines)
APP_VERSION = $$first(APP_VERSION)
isEmpty(APP_VERSION): error("读不到 VERSION.txt，无法确定版本号")
DEFINES += APP_VERSION=\\\"$$APP_VERSION\\\"

# Windows 版本信息 + 图标：自备 .rc，不用 qmake 自动生成的那份。
#
# 原因：qmake 用系统 ANSI 代码页读取 UTF-8 的 .pro，QMAKE_TARGET_PRODUCT 里的中文
# 在读取阶段就被错误解码，写进 exe 属性后是乱码（"涓夌淮澶嶇幇..."）。
# app_resource.rc.in 保存为 UTF-8 无 BOM 并带 #pragma code_page(65001)，
# 由 windres 正确解析，中文产品名可正常显示。
win32 {
    # 拆成三个独立数字。不用 $$replace 拼 "2,2,0"：qmake 会把引号一并写进变量，
    # 后续字符串里就出现不配对的引号，报 "Missing closing quote"。
    VER_MAJOR = $$section(APP_VERSION, ., 0, 0)
    VER_MINOR = $$section(APP_VERSION, ., 1, 1)
    VER_PATCH = $$section(APP_VERSION, ., 2, 2)
    isEmpty(VER_PATCH): VER_PATCH = 0

    # 只生成纯 ASCII、不含引号的头文件：
    #   - qmake 的 write_file 按系统代码页写出，含中文的内容会被转成 GBK，
    #     与 .rc 里的 #pragma code_page(65001) 冲突，所以中文一律不经手 qmake；
    #   - qmake 对嵌套引号的转义不可靠，所以这里连引号都不写，
    #     字符串化交给 .rc 里的 RC_STR() 宏完成。
    # 必须用 $$LITERAL_HASH 而不是直接写 '#'：qmake 把 # 一律视为注释起始，
    # 即使在引号内也一样，直接写会报 "Missing closing quote"。
    VER_HEADER = $$OUT_PWD/app_version.h
    VER_LINES  = "$${LITERAL_HASH}define VER_MAJOR $$VER_MAJOR"
    VER_LINES += "$${LITERAL_HASH}define VER_MINOR $$VER_MINOR"
    VER_LINES += "$${LITERAL_HASH}define VER_PATCH $$VER_PATCH"
    write_file($$VER_HEADER, VER_LINES)|error("无法生成 $$VER_HEADER")

    RC_FILE = $$PWD/app_resource.rc
    # windres 需要能找到 app_version.h（构建目录）和 icon.ico（项目根目录）
    RC_INCLUDEPATH += $$OUT_PWD $$PWD
}

# MinGW Debug模式下QCustomPlot过大，需要big-obj支持
win32-g++: QMAKE_CXXFLAGS += -Wa,-mbig-obj

# 源码目录定义（仅开发模式；打包时注释掉以使用 exe 同级路径）
!CONFIG(deploy): DEFINES += SRCDIR=\\\"$$PWD\\\"

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    pybulletclient.cpp \
    renderview.cpp \
    controlpanel.cpp \
    playerbar.cpp \
    logwidget.cpp \
    qcustomplot.cpp \
    csvparser.cpp \
    aviwriter.cpp \
    screenrecorder.cpp \
    scopechart.cpp \
    curveviewer.cpp \
    rangeslider.cpp \
    defaultpage.cpp \
    m20jointpanel.cpp \
    x30jointpanel.cpp \
    dr02jointpanel.cpp \
    customanalyzer.cpp \
    logmanager.cpp \
    logcollectwidget.cpp \
    loganalysiswidget.cpp \
    sshclient.cpp

HEADERS += \
    mainwindow.h \
    pybulletclient.h \
    renderview.h \
    controlpanel.h \
    playerbar.h \
    logwidget.h \
    qcustomplot.h \
    csvparser.h \
    aviwriter.h \
    screenrecorder.h \
    scopechart.h \
    curveviewer.h \
    rangeslider.h \
    defaultpage.h \
    m20jointpanel.h \
    x30jointpanel.h \
    dr02jointpanel.h \
    customanalyzer.h \
    logmanager.h \
    logcollectwidget.h \
    loganalysiswidget.h \
    sshclient.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
