#pragma once

/*
MIT License

Copyright (c) 2018 Jarle Aase

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Home: https://github.com/jgaa/logfault
*/

#ifndef _LOGFAULT_H
#define _LOGFAULT_H

#include <array>
#include <assert.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef LOGFAULT_USE_UTCZONE
#   define LOGFAULT_USE_UTCZONE 0
#endif

#ifndef LOGFAULT_TIME_FORMAT
#   define LOGFAULT_TIME_FORMAT "%Y-%m-%d %H:%M:%S."
#endif

#ifndef LOGFAULT_TIME_PRINT_MILLISECONDS
#   define LOGFAULT_TIME_PRINT_MILLISECONDS 1
#endif

#ifndef LOGFAULT_TIME_PRINT_TIMEZONE
#   define LOGFAULT_TIME_PRINT_TIMEZONE 1
#endif

#ifdef LOGFAULT_USE_SYSLOG
#   include <syslog.h>
#endif

#ifdef LOGFAULT_USE_ANDROID_NDK_LOG
#   include <android/log.h>
#endif

#ifdef LOGFAULT_USE_QT_LOG
#   include <QDebug>
#endif

#ifdef LOGFAULT_USE_WINDOWS_EVENTLOG
#	include <windows.h>

// Thank you soo much Microsoft!
#	undef ERROR
#endif

#ifndef LOGFAULT_LOCATION__
#   if defined(LOGFAULT_ENABLE_LOCATION) && LOGFAULT_ENABLE_LOCATION
#       define LOGFAULT_LOCATION__ << logfault::Handler::ShortenPath(__FILE__) << ':' << __LINE__ << " {" << __func__ << "} "
#       ifndef LOGFAULT_LOCATION_LEVELS
#           define LOGFAULT_LOCATION_LEVELS 3
#       endif
#   else
#       define LOGFAULT_LOCATION__
#   endif
#endif

// Internal implementation detail
#define LOGFAULT_LOG__(level) \
    ::logfault::LogManager::Instance().IsRelevant(level) && \
    ::logfault::Log(level).Line() LOGFAULT_LOCATION__

#define LFLOG_ERROR LOGFAULT_LOG__(logfault::LogLevel::ERROR)
#define LFLOG_WARN LOGFAULT_LOG__(logfault::LogLevel::WARN)
#define LFLOG_NOTICE LOGFAULT_LOG__(logfault::LogLevel::NOTICE)
#define LFLOG_INFO LOGFAULT_LOG__(logfault::LogLevel::INFO)
#define LFLOG_DEBUG LOGFAULT_LOG__(logfault::LogLevel::DEBUGGING)
#define LFLOG_TRACE LOGFAULT_LOG__(logfault::LogLevel::TRACE)

#ifdef LOGFAULT_ENABLE_ALL
#   define LFLOG_IFALL_ERROR(msg) LFLOG_ERROR << msg
#   define LFLOG_IFALL_WARN(msg) LFLOG_WARN << msg
#   define LFLOG_IFALL_NOTICE(msg) LFLOG_NOTICE << msg
#   define LFLOG_IFALL_INFO(msg) LFLOG_INFO << msg
#   define LFLOG_IFALL_DEBUG(msg) LFLOG_DEBUG << msg
#   define LFLOG_IFALL_TRACE(msg) LFLOG_TRACE << msg
# else
#   define LFLOG_IFALL_ERROR(msg)
#   define LFLOG_IFALL_WARN(msg)
#   define LFLOG_IFALL_NOTICE(msg)
#   define LFLOG_IFALL_INFO(msg)
#   define LFLOG_IFALL_DEBUG(msg)
#   define LFLOG_IFALL_TRACE(msg)
#endif

namespace logfault {

    enum class LogLevel { DISABLED, ERROR, WARN, NOTICE, INFO, DEBUGGING, TRACE };

    struct Message {
        Message(const std::string && msg, const LogLevel level)
        : msg_{std::move(msg)}, level_{level} {}

        const std::string msg_;
        const std::chrono::system_clock::time_point when_ = std::chrono::system_clock::now();
        const LogLevel level_;
    };

    class Handler {
    public:
        Handler(LogLevel level = LogLevel::INFO) : level_{level} {}
        virtual ~Handler() = default;
        using ptr_t = std::unique_ptr<Handler>;

        virtual void LogMessage(const Message& msg) = 0;
        const LogLevel level_;

        static const std::string& LevelName(const LogLevel level) {
            static const std::array<std::string, 7> names =
                {{"DISABLED", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUGGING", "TRACE"}};
            return names.at(static_cast<size_t>(level));
        }

        void PrintMessage(std::ostream& out, const logfault::Message& msg) {
            auto tt = std::chrono::system_clock::to_time_t(msg.when_);
            auto when_rounded = std::chrono::system_clock::from_time_t(tt);
            if (when_rounded > msg.when_) {
                --tt;
                when_rounded -= std::chrono::seconds(1);
            }
            if (const auto tm = (LOGFAULT_USE_UTCZONE ? std::gmtime(&tt) : std::localtime(&tt))) {
                const int ms = std::chrono::duration_cast<std::chrono::duration<int, std::milli>>(msg.when_ - when_rounded).count();

                out << std::put_time(tm, LOGFAULT_TIME_FORMAT)
#if LOGFAULT_TIME_PRINT_MILLISECONDS
                    << std::setw(3) << std::setfill('0') << ms
#endif
#if LOGFAULT_TIME_PRINT_TIMEZONE
#   if LOGFAULT_USE_UTCZONE
                    << " UTC";
#   else
                    << std::put_time(tm, " %Z")
#   endif
#endif
                    ;
            } else {
                out << "0000-00-00 00:00:00.000";
            }

            out << ' ' << LevelName(msg.level_)
                << ' ' << std::this_thread::get_id()
                << ' ' << msg.msg_;
        }

#if defined(LOGFAULT_ENABLE_LOCATION) && LOGFAULT_ENABLE_LOCATION
        static const char *ShortenPath(const char *path) {
            assert(path);
            if (LOGFAULT_LOCATION_LEVELS <= 0) {
                return path;
            }
            std::vector<const char *> seperators;
            for(const char *p = path; *p; ++p) {
                if (((*p == '/')
#if defined(WIN32) || defined(__WIN32) || defined(MSC_VER) || defined(WINDOWS)
                    || (*p == '\\')
#endif
                ) && p[1]) {
                    if (seperators.size() > LOGFAULT_LOCATION_LEVELS) {
                        seperators.erase(seperators.begin());
                    }
                    seperators.push_back(p + 1);
                }
            }
            return seperators.empty() ? path : seperators.front();
        }
#endif

    };

    class StreamHandler : public Handler {
    public:
        StreamHandler(std::ostream& out, LogLevel level) : Handler(level), out_{out} {}
        StreamHandler(std::string& path, LogLevel level, const bool truncate = false) : Handler(level)
        , file_{new std::ofstream{path, truncate ? std::ios::trunc : std::ios::app}}, out_{*file_} {}

        void LogMessage(const Message& msg) override {
            PrintMessage(out_, msg);
            out_ << std::endl;
        }

    private:
        std::unique_ptr<std::ostream> file_;
        std::ostream& out_;
    };

    class ProxyHandler : public Handler {
    public:
        using fn_t = std::function<void(const Message&)>;

        ProxyHandler(const fn_t& fn, LogLevel level) : Handler(level), fn_{fn} {
            assert(fn_);
        }

        void LogMessage(const logfault::Message& msg) override {
            fn_(msg);
        }

    private:
        const fn_t fn_;
    };

#ifdef LOGFAULT_USE_SYSLOG
    class SyslogHandler : public Handler {
    public:
        SyslogHandler(LogLevel level, int facility = LOG_USER)
        : Handler(level) {
            static std::once_flag syslog_opened;
            std::call_once(syslog_opened, [facility] {
                openlog(nullptr, 0, facility);
            });
        }

        void LogMessage(const logfault::Message& msg) override {
            static const std::array<int, 6> syslog_priority =
                { LOG_ERR, LOG_WARNING, LOG_NOTICE, LOG_INFO, LOG_DEBUG, LOG_DEBUG };

            syslog(syslog_priority.at(static_cast<int>(level_)), "%s", msg.msg_.c_str());
        }
    };
#endif

#ifdef LOGFAULT_USE_ANDROID_NDK_LOG
    class AndroidHandler : public Handler {
    public:
        AndroidHandler(const std::string& name, LogLevel level)
        : Handler(level), name_{name} {}

        void LogMessage(const logfault::Message& msg) override {
            static const std::array<int, 6> android_priority =
                { ANDROID_LOG_ERROR, ANDROID_LOG_WARN, ANDROID_LOG_INFO,
                  ANDROID_LOG_INFO, ANDROID_LOG_DEBUG, ANDROID_LOG_VERBOSE };
            __android_log_write(android_priority.at(static_cast<int>(level_)),
                                name_.c_str(), msg.msg_.c_str());
        }

    private:
        const std::string name_;
    };
#endif

#ifdef LOGFAULT_USE_QT_LOG
    class QtHandler : public Handler
    {
    public:
        QtHandler(LogLevel level)
            : Handler(level) {}

        void LogMessage(const logfault::Message &msg) override
        {
            switch (msg.level_)
            {
            case LogLevel::ERROR:
                qCritical() << msg.msg_.c_str();
                break;
            case LogLevel::WARN:
                qWarning() << msg.msg_.c_str();
                break;
            case LogLevel::INFO:
            case LogLevel::NOTICE:
                qInfo() << msg.msg_.c_str();
                break;
            case LogLevel::DEBUGGING:
            case LogLevel::TRACE:
                qDebug() << msg.msg_.c_str();
                break;
            }
        }
    };
#endif

#if defined(LOGFAULT_USE_COCOA_NLOG) || defined(LOGFAULT_USE_COCOA_NLOG_IMPL)
    class CocoaHandler : public Handler {
    public:
        CocoaHandler(LogLevel level)
        : Handler(level) {}

        void LogMessage(const logfault::Message& msg) override;
    };

    // Must be defined once, when included to a .mm file
    #ifdef LOGFAULT_USE_COCOA_NLOG_IMPL
        void CocoaHandler::LogMessage(const logfault::Message& msg) {
            const std::string text = LevelName(msg.level_) + " " + msg.msg_;
            NSLog(@"%s", text.c_str());
        }
    #endif //LOGFAULT_USE_COCOA_NLOG_IMPL
#endif // LOGFAULT_USE_COCOA_NLOG

#ifdef LOGFAULT_USE_WINDOWS_EVENTLOG
        class WindowsEventLogHandler : public Handler {
        public:
            WindowsEventLogHandler(const std::string& name, LogLevel level)
                : Handler(level) {
                h_ = RegisterEventSource(0, name.c_str());
            }

            ~WindowsEventLogHandler() {
                DeregisterEventSource(h_);
            }

            void LogMessage(const logfault::Message& msg) override {
                if (!h_) {
                    return;
                }
                WORD wtype = EVENTLOG_SUCCESS;
                switch (msg.level_) {
                case LogLevel::ERROR:
                    wtype = EVENTLOG_ERROR_TYPE;
                    break;
                case LogLevel::WARN:
                    wtype = EVENTLOG_WARNING_TYPE;
                    break;
                default:
                    ;
                }

                LPCSTR buffer = reinterpret_cast<LPCSTR>(msg.msg_.c_str());
                ReportEventA(h_, wtype, 0, 0, 0, 1, 0, &buffer, 0);
            }
        private:
            HANDLE h_ = {};
    };
#endif // LOGFAULT_USE_WINDOWS_EVENTLOG

    class LogManager {
        LogManager() = default;
        LogManager(const LogManager&) = delete;
        LogManager(LogManager &&) = delete;
        void operator = (const LogManager&) = delete;
        void operator = (LogManager&&) = delete;
    public:

        static LogManager& Instance() {
            static LogManager instance;
            return instance;
        }

        void LogMessage(Message message) {
            std::lock_guard<std::mutex> lock{mutex_};
            for(const auto& h : handlers_) {
                if (h->level_ >= message.level_) {
                    h->LogMessage(message);
                }
            }
        }

        void AddHandler(Handler::ptr_t && handler) {
            std::lock_guard<std::mutex> lock{mutex_};

            // Make sure we log at the most detailed level used
            if (level_ < handler->level_) {
                level_ = handler->level_;
            }
            handlers_.push_back(std::move(handler));
        }

        /*! Set handler.
         *
         * Remove any existing handlers.
         */
        void SetHandler(Handler::ptr_t && handler) {
            std::lock_guard<std::mutex> lock{mutex_};
            handlers_.clear();
            level_ = handler->level_;
            handlers_.push_back(std::move(handler));
        }
        
         /*! Remove all existing handlers
          * 
          */
        void ClearHandlers() {
            std::lock_guard<std::mutex> lock{mutex_};
            handlers_.clear();
            level_ = LogLevel::DISABLED;
        }


        void SetLevel(LogLevel level) {
            level_ = level;
        }

        LogLevel GetLoglevel() const noexcept {
            return level_;
        }

        bool IsRelevant(const LogLevel level) const noexcept {
            return !handlers_.empty() && (level <= level_);
        }

    private:
        std::vector<Handler::ptr_t> handlers_;
        std::mutex mutex_;
        LogLevel level_ = LogLevel::ERROR;
    };

    class Log {
    public:
        Log(const LogLevel level) : level_{level} {}
        ~Log() {
            Message message(out_.str(), level_);
            LogManager::Instance().LogMessage(message);
        }

        std::ostringstream& Line() { return out_; }

private:
        const LogLevel level_;
        std::ostringstream out_;
    };

} // namespace

// stream operators for QT to make common datatypes simple to log
#ifdef QBYTEARRAY_H
inline std::ostream& operator << (std::ostream& out, const QByteArray& v) {
    return out << v.constData();
}
#endif

#ifdef QSTRING_H
inline std::ostream& operator << (std::ostream& out, const QString& v) {
    return out << v.toUtf8().constData();
}
#endif

#ifdef QHOSTADDRESS_H
inline std::ostream& operator << (std::ostream& out, const QHostAddress& v) {
   return out << v.toString().toUtf8().constData();
}
#endif




#endif // _LOGFAULT_H

