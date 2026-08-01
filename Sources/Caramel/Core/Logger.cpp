/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:59:42
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

TShared<spdlog::logger> Logger::s_CoreLogger;

void Logger::Initialize()
{
    TArray<spdlog::sink_ptr> sinks;
    sinks.PushBack(MakeShared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.PushBack(MakeShared<spdlog::sinks::basic_file_sink_mt>("Caramel.log", true));

    sinks[0]->set_pattern("%^[%T] %n: %v%$");
    sinks[1]->set_pattern("[%T] [%l] %n: %v");

    s_CoreLogger = MakeShared<spdlog::logger>("CARAMEL", sinks.begin(), sinks.end());
    spdlog::register_logger(s_CoreLogger);
}
