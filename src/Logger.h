#pragma once
#include <string>

namespace logx {

void info(const std::string& msg);
void error(const std::string& msg);

}

#define LOGI(msg) ::logx::info(msg)
#define LOGE(msg) ::logx::error(msg)
