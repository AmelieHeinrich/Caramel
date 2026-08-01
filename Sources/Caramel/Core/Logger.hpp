/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:58:21
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <spdlog/spdlog.h>

class Logger
{
public:
    static void Initialize();

    static TShared<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
private:
    static TShared<spdlog::logger> s_CoreLogger;
};

#define CARAMEL_DEBUG(...)    ::Logger::GetCoreLogger()->debug(__VA_ARGS__)
#define CARAMEL_TRACE(...)    ::Logger::GetCoreLogger()->trace(__VA_ARGS__)
#define CARAMEL_INFO(...)     ::Logger::GetCoreLogger()->info(__VA_ARGS__)
#define CARAMEL_WARN(...)     ::Logger::GetCoreLogger()->warn(__VA_ARGS__)
#define CARAMEL_ERROR(...)    ::Logger::GetCoreLogger()->error(__VA_ARGS__)
#define CARAMEL_CRITICAL(...) ::Logger::GetCoreLogger()->critical(__VA_ARGS__)
