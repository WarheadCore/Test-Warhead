/*
 * This file is part of the WarheadCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SystemLog.h"
#include "Log.h"
#include "Config.h"
#include "Util.h"
#include "StringConvert.h"
#include "Poco/PatternFormatter.h"
#include "Poco/FileChannel.h"
#include "Poco/SplitterChannel.h"
//#include "Poco/Logger.h"
#include "Poco/AutoPtr.h"
#include <sstream>

#if WARHEAD_PLATFORM == WARHEAD_PLATFORM_WINDOWS
#include "Poco/WindowsConsoleChannel.h"
#define CONSOLE_CHANNEL WindowsColorConsoleChannel
#else
#include "Poco/ConsoleChannel.h"
#define CONSOLE_CHANNEL ColorConsoleChannel
#endif

using namespace Poco;

Log::Log()
{
    Clear();
}

Log::~Log()
{
    Clear();
}

Log* Log::instance()
{
    static Log instance;
    return &instance;
}

void Log::Clear()
{
    // Clear all loggers
    Logger::shutdown();

    _channelStore.clear();
}

void Log::Initialize()
{
    LoadFromConfig();
}

void Log::LoadFromConfig()
{
    highestLogLevel = LogLevel::LOG_LEVEL_FATAL;

    Clear();
    InitLogsDir();
    ReadChannelsFromConfig();
    ReadLoggersFromConfig();
}

void Log::InitLogsDir()
{
    m_logsDir = sConfigMgr->GetStringDefault("LogsDir", "");

    if (!m_logsDir.empty())
        if ((m_logsDir.at(m_logsDir.length() - 1) != '/') && (m_logsDir.at(m_logsDir.length() - 1) != '\\'))
            m_logsDir.push_back('/');
}

void Log::ReadLoggersFromConfig()
{
    auto const& keys = sConfigMgr->GetKeysByString(PREFIX_LOGGER);
    if (!keys.size())
    {
        SYS_LOG_ERROR("Log::ReadLoggersFromConfig - Not found loggers, change config file!");
        return;
    }

    for (auto const& loggerName : keys)
        CreateLoggerFromConfig(loggerName);

    if (!Logger::has(LOGGER_ROOT))
        SYS_LOG_ERROR("Log::ReadLoggersFromConfig - Logger '%s' not found!\nPlease change or add 'Logger.%s' option in config file!\n", LOGGER_ROOT.c_str(), LOGGER_ROOT.c_str());
}

void Log::ReadChannelsFromConfig()
{
    auto const& keys = sConfigMgr->GetKeysByString(PREFIX_CHANNEL);
    if (keys.empty())
    {
        SYS_LOG_ERROR("Log::ReadChannelsFromConfig - Not found channels, change config file!");
        return;
    }

    for (auto const& channelName : keys)
        CreateChannelsFromConfig(channelName);
}

std::string Log::GetPositionOptions(std::string const& options, uint8 position, std::string const& _default /*= ""*/)
{
    auto const& tokens = Warhead::Tokenize(options, ',', true);
    if (tokens.size() < position + 1)
        return _default;

    return std::string(tokens[position]);
}

Logger* Log::GetLoggerByType(std::string const& type) const
{
    if (Logger::has(type))
        return &Logger::get(type);

    if (type == LOGGER_ROOT)
        return nullptr;

    std::string parentLogger = LOGGER_ROOT;
    size_t found = type.find_last_of('.');

    if (found != std::string::npos)
        parentLogger = type.substr(0, found);

    return GetLoggerByType(parentLogger);
}

bool Log::ShouldLog(std::string const& type, LogLevel level) const
{
    // Don't even look for a logger if the LogLevel is lower than lowest log levels across all loggers
    if (level > highestLogLevel)
        return false;

    Logger const* logger = GetLoggerByType(type);
    if (!logger)
        return false;

    LogLevel logLevel = LogLevel(logger->getLevel());
    return logLevel != LogLevel::LOG_LEVEL_DISABLED && logLevel >= level;
}

FormattingChannel* const* Log::GetFormattingChannel(std::string const& channelName)
{
    auto const& itr = _channelStore.find(channelName);
    if (itr != _channelStore.end())
        return &_channelStore.at(channelName);

    return nullptr;
}

void Log::AddFormattingChannel(std::string const& channelName, FormattingChannel* channel)
{
    if (GetFormattingChannel(channelName))
    {
        SYS_LOG_ERROR("> Formatting channel (%s) is already exist!", channelName.c_str());
        return;
    }

    _channelStore.insert(std::make_pair(channelName, channel));
}

std::string const Log::GetChannelsFromLogger(std::string const& loggerName)
{
    std::string loggerOptions = sConfigMgr->GetStringDefault(PREFIX_LOGGER + loggerName, "6, Console Server");
    
    auto const& tokensOptions = Warhead::Tokenize(loggerOptions, ',', true);
    if (tokensOptions.empty())
        return "";

    return std::string(tokensOptions[LOGGER_OPTIONS_CHANNELS_NAME]);
}

void Log::CreateLoggerFromConfig(std::string const& configLoggerName)
{
    if (configLoggerName.empty())
        return;

    std::string options = sConfigMgr->GetStringDefault(configLoggerName, "");
    std::string loggerName = configLoggerName.substr(PREFIX_LOGGER.length());

    if (loggerName == "system")
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Is forbidden to use logger name %s\n", loggerName.c_str());
        return;
    }

    if (options.empty())
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Missing config option Logger.%s", loggerName.c_str());
        return;
    }

    auto const& tokens = Warhead::Tokenize(options, ',', true);
    if (!tokens.size() || tokens.size() < LOGGER_OPTIONS_CHANNELS_NAME + 1)
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Bad config options for Logger (%s)", loggerName.c_str());
        return;
    }

    LogLevel level = LogLevel(Warhead::StringTo<uint8>(GetPositionOptions(options, LOGGER_OPTIONS_LOG_LEVEL)).value_or((uint8)LogLevel::LOG_LEVEL_MAX));
    if (level >= LogLevel::LOG_LEVEL_MAX)
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Wrong Log Level for logger %s", loggerName.c_str());
        return;
    }

    if (level > highestLogLevel)
        highestLogLevel = level;

    AutoPtr<SplitterChannel> splitterChannel(new SplitterChannel);
    auto const& channelsName = GetPositionOptions(options, LOGGER_OPTIONS_CHANNELS_NAME);

    for (auto const& tokensFmtChannels : Warhead::Tokenize(channelsName, ' ', false))
    {
        std::string channelName = std::string(tokensFmtChannels);

        auto fmtChannel = GetFormattingChannel(channelName);
        if (!fmtChannel)
        {
            SYS_LOG_ERROR("Log::CreateLoggerFromConfig - Not found channel (%s)", channelName.c_str());
            return;
        }

        splitterChannel->addChannel(*fmtChannel);
    }

    try
    {
        Logger::create(loggerName, splitterChannel, (uint8)level);
    }
    catch (const std::exception& e)
    {
        SYS_LOG_ERROR("Log::CreateLogger - %s", e.what());
    }
}

void Log::CreateChannelsFromConfig(std::string const& logChannelName)
{
    if (logChannelName.empty())
        return;

    std::string options = sConfigMgr->GetStringDefault(logChannelName, "");
    std::string channelName = logChannelName.substr(PREFIX_CHANNEL.length());

    if (options.empty())
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Missing config option LogChannel.%s", channelName.c_str());
        return;
    }

    auto const& tokens = Warhead::Tokenize(options, ',', true);
    if (tokens.size() < CHANNEL_OPTIONS_PATTERN + 1)
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Wrong config option (< CHANNEL_OPTIONS_PATTERN) LogChannel.%s=%s\n", channelName.c_str(), options.c_str());
        return;
    }

    if (tokens.size() > CHANNEL_OPTIONS_MAX)
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Wrong config option (> CHANNEL_OPTIONS_MAX) LogChannel.%s=%s\n", channelName.c_str(), options.c_str());
        return;
    }

    auto channelType = Warhead::StringTo<uint8>(GetPositionOptions(options, CHANNEL_OPTIONS_TYPE).c_str());
    if (!channelType || (channelType && channelType.value() > (uint8)FormattingChannelType::FORMATTING_CHANNEL_TYPE_FILE))
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Wrong channel type for LogChannel.%s\n", channelName.c_str());
        return;
    }

    std::string times = GetPositionOptions(options, CHANNEL_OPTIONS_TIMES);
    if (times.empty())
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Empty times for LogChannel.%s", channelName.c_str());
        return;
    }
    
    std::string pattern = GetPositionOptions(options, CHANNEL_OPTIONS_PATTERN);
    if (pattern.empty())
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Empty pattern for LogChannel.%s", channelName.c_str());
        return;
    }

    // Start configuration pattern channel
    AutoPtr<PatternFormatter> _pattern(new PatternFormatter);

    try
    {
        _pattern->setProperty("pattern", pattern);
        _pattern->setProperty("times", times);
    }
    catch (const std::exception& e)
    {
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: %s\n", e.what());
    }

    if (channelType.value() == (uint8)FormattingChannelType::FORMATTING_CHANNEL_TYPE_CONSOLE)
    {
        // Configuration console channel
        AutoPtr<CONSOLE_CHANNEL> _channel(new CONSOLE_CHANNEL);

        // Init Colors
        std::string const& colorOptions = GetPositionOptions(options, CHANNEL_OPTIONS_OPTION_1);

        if (!colorOptions.empty())
        {
            auto const& tokens = Warhead::Tokenize(colorOptions, ' ', true);
            if (tokens.size() == 8)
            {
                try
                {
                    _channel->setProperty("fatalColor", std::string(tokens[0]));
                    _channel->setProperty("criticalColor", std::string(tokens[1]));
                    _channel->setProperty("errorColor", std::string(tokens[2]));
                    _channel->setProperty("warningColor", std::string(tokens[3]));
                    _channel->setProperty("noticeColor", std::string(tokens[4]));
                    _channel->setProperty("informationColor", std::string(tokens[5]));
                    _channel->setProperty("debugColor", std::string(tokens[6]));
                    _channel->setProperty("traceColor", std::string(tokens[7]));
                }
                catch (const std::exception& e)
                {
                    printf("Log::CreateLoggerFromConfig: %s\n\n", e.what());
                }
            }
            else
                _channel->setProperty("enableColors", "false");
        }
        else
            _channel->setProperty("enableColors", "false");

        AddFormattingChannel(channelName, new FormattingChannel(_pattern, _channel));
    }
    else if (channelType.value() == (uint8)FormattingChannelType::FORMATTING_CHANNEL_TYPE_FILE)
    {
        if (tokens.size() < CHANNEL_OPTIONS_OPTION_1 + 1)
        {
            SYS_LOG_ERROR("Bad file name for LogChannel.%s", channelName.c_str());
            ABORT();
        }

        std::string fileName = GetPositionOptions(options, CHANNEL_OPTIONS_OPTION_1);
        std::string rotateOnOpen = GetPositionOptions(options, CHANNEL_OPTIONS_OPTION_2, "false");
        std::string rotation = GetPositionOptions(options, CHANNEL_OPTIONS_OPTION_3, "daily");
        std::string flush = GetPositionOptions(options, CHANNEL_OPTIONS_OPTION_4, "false");
        std::string purgeAge = GetPositionOptions(options, CHANNEL_OPTIONS_OPTION_5, "1 months");
        std::string archive = GetPositionOptions(options, CHANNEL_OPTIONS_OPTION_6);

        // Configuration file channel
        AutoPtr<FileChannel> _fileChannel(new FileChannel);

        try
        {
            _fileChannel->setProperty("path", m_logsDir + fileName);
            _fileChannel->setProperty("times", times);

            if (!rotateOnOpen.empty())
                _fileChannel->setProperty("rotateOnOpen", rotateOnOpen);

            if (!rotation.empty())
                _fileChannel->setProperty("rotation", rotation);

            if (!flush.empty())
                _fileChannel->setProperty("flush", flush);

            if (!purgeAge.empty())
                _fileChannel->setProperty("purgeAge", purgeAge);

            if (!archive.empty())
                _fileChannel->setProperty("archive", archive);
        }
        catch (const std::exception& e)
        {
            SYS_LOG_ERROR("Log::CreateLoggerFromConfig: %s", e.what());
        }

        AddFormattingChannel(channelName, new FormattingChannel(_pattern, _fileChannel));
    }
    else
        SYS_LOG_ERROR("Log::CreateLoggerFromConfig: Invalid channel type (%u)", channelType.value());
}

void Log::_Write(std::string const& filter, LogLevel const level, std::string const& message)
{
    Logger* logger = GetLoggerByType(filter);
    if (!logger)
        return;

    try
    {
        switch (level)
        {
        case LogLevel::LOG_LEVEL_FATAL:
            logger->fatal(message);
            break;
        case LogLevel::LOG_LEVEL_CRITICAL:
            logger->critical(message);
            break;
        case LogLevel::LOG_LEVEL_ERROR:
            logger->error(message);
            break;
        case LogLevel::LOG_LEVEL_WARNING:
            logger->warning(message);
            break;
        case LogLevel::LOG_LEVEL_NOTICE:
            logger->notice(message);
            break;
        case LogLevel::LOG_LEVEL_INFO:
            logger->information(message);
            break;
        case LogLevel::LOG_LEVEL_DEBUG:
            logger->debug(message);
            break;
        case LogLevel::LOG_LEVEL_TRACE:
            logger->trace(message);
            break;
        default:
            break;
        }
    }
    catch (const std::exception& e)
    {
        SYS_LOG_ERROR("Log::_Write - %s", e.what());
    }
}

void Log::_writeCommand(std::string const message, std::string const accountid)
{
    if (!Logger::has(LOGGER_GM))
        return;

    _Write(LOGGER_GM, LogLevel::LOG_LEVEL_INFO, message);
}

void Log::outMessage(std::string const& filter, LogLevel const level, std::string&& message)
{
    _Write(filter, level, message);
}

void Log::outCommand(std::string&& AccountID, std::string&& message)
{
    _writeCommand(message, AccountID);
}

void Log::outCharDump(std::string const& str, uint32 accountId, uint64 guid, std::string const& name)
{
    if (str.empty())
        return;

    _Write(LOGGER_PLAYER_DUMP, LogLevel::LOG_LEVEL_INFO, Warhead::StringFormat("== START DUMP ==\n(Account: %u. Guid: %u. Name: %s)\n%s\n== END DUMP ==\n", accountId, guid, name.c_str(), str.c_str()));
}
