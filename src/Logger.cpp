#include "Logger.h"
#include <windows.h>
#include <iostream>

namespace logx {

static void out(const std::string& level, const std::string& msg) {
    std::string line = "[" + level + "] " + msg + "\n";
    // console
    std::cout << line;
    // debugger
    int wlen = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring wbuf;
        wbuf.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, &wbuf[0], wlen);
        OutputDebugStringW(wbuf.c_str());
    }
}

void info(const std::string& msg) { out("INFO", msg); }
void error(const std::string& msg) { out("ERROR", msg); }

}
