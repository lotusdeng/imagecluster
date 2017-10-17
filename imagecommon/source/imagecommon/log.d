module imagecommon.log;
import std.stdio;
import std.experimental.logger;
import std.range;
import std.datetime;
import std.path;
import std.traits;
import std.format;
import std.string;
import std.file;
import std.concurrency;

class MyConsoleLogger : Logger
{
    this(LogLevel logLevel)
    {
        super(logLevel);
    }

    protected override @safe void writeLogMsg(ref LogEntry payload)
    {
        writeln(payload.timestamp, " ", payload.msg);
    }
}

class MyFileLogger : FileLogger
{

    static char[LogLevel] m_logLevelMap;
    static this()
    {
        foreach (immutable suit; [EnumMembers!LogLevel])
        {
            m_logLevelMap[suit] = format("%s", suit).toUpper()[0];
        }
    }

    this(FileLogger logger)
    {
        super(logger.file, logger.logLevel);
    }

    /* This function formates a $(D SysTime) into an $(D OutputRange).
    The $(D SysTime) is formatted similar to
    $(LREF std.datatime.DateTime.toISOExtString) except the fractional second part.
    The fractional second part is in milliseconds and is always 3 digits.
    */
    static void systimeToISOString(OutputRange)(OutputRange o, const ref SysTime time)
            if (isOutputRange!(OutputRange, string))
    {
        const auto dt = cast(DateTime) time;
        const auto fsec = time.fracSecs.total!"msecs";

        formattedWrite(o, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
    }

    /* This method overrides the base class method in order to log to a file
       without requiring heap allocated memory. Additionally, the $(D FileLogger)
       local mutex is logged to serialize the log calls.
     */
    override protected void beginLogMsg(string file, int line, string funcName, string prettyFuncName,
            string moduleName, LogLevel logLevel, Tid threadId, SysTime timestamp, Logger logger) @safe
    {
        import std.string : lastIndexOf;

        ptrdiff_t fnIdx = file.lastIndexOf(dirSeparator) + 1;
        ptrdiff_t funIdx = funcName.lastIndexOf('.') + 1;

        auto lt = this.file.lockingTextWriter();
        //formattedWrite(lt, "[%s]", m_logLevelMap[logLevel]);
        //systimeToISOString(lt, timestamp);
        //formattedWrite(lt, ":%s:%s:%u ", file[fnIdx .. $], funcName[funIdx .. $], line);
        systimeToISOString(lt, timestamp);
        formattedWrite(lt, " %s %s_%u: ", logLevel, file[fnIdx .. $], line);
    }

    /+
    /* This methods overrides the base class method and writes the parts of
       the log call directly to the file.
     */
    override protected void logMsgPart(const(char)[] msg)
    {
        formattedWrite(this.file_.lockingTextWriter(), "%s", msg);
    }
    /* This methods overrides the base class method and finalizes the active
       log call. This requires flushing the $(D File) and releasing the
       $(D FileLogger) local mutex.
     */
    override protected void finishLogMsg()
    {
        this.file_.lockingTextWriter().put("\n");
        this.file_.flush();
    }
    +/
}

void initLog(string logFileName)
{
	string logDirPath = buildPath("log");
	if(!exists(logDirPath) || !isDir(logDirPath))
	{
		info("create dir:", logDirPath);
		mkdirRecurse(logDirPath);
	}
	string logFilePath = buildPath("log", logFileName);
	auto multiLog = new MultiLogger();
	multiLog.insertLogger("fileLog", new MyFileLogger(new FileLogger(logFilePath)));
	multiLog.insertLogger("consoleLog", new MyConsoleLogger(LogLevel.info));
	sharedLog = multiLog;
}