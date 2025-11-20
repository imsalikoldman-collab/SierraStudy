#property copyright   "SierraStudy"
#property version     "1.0"
#property script_show_inputs

/**
 * @brief Диагностический скрипт MT5 для проверки подключения к pipe и чтения JSON от Sierra.
 * @note Не торгует. Просто подключается к pipe через DLL и выводит входящие строки в журнал Experts.
 * @warning Для корректной работы должен лежать рядом с SierraStudyAdvisorBridgeMT5.dll в MQL5\Libraries.
 */

#import "SierraStudyAdvisorBridgeMT5.dll"
int  SierraPipeConnect(string pipe_name);
void SierraPipeClose();
int  SierraPipeRead(uchar& buffer[], int size, int timeout_ms);
#import

input string  InpPipeName       = "\\\\.\\pipe\\SierraStudyAdvisor"; ///< Имя pipe.
input int     InpDurationSec    = 30;                                  ///< Время чтения (сек).
input int     InpPollMs         = 200;                                 ///< Таймаут чтения (мс).
input int     InpMaxMessages    = 50;                                  ///< Ограничение по количеству сообщений (0 = без лимита).

int OnStart()
{
  uchar buffer[4096];

  const int res = SierraPipeConnect(InpPipeName);
  if(res != 1)
  {
    PrintFormat("[diag] Connect failed (res=%d, pipe=%s)", res, InpPipeName);
    return(INIT_FAILED);
  }
  PrintFormat("[diag] Connected to pipe %s", InpPipeName);

  datetime start = TimeCurrent();
  int count = 0;
  while(true)
  {
    const int received = SierraPipeRead(buffer, ArraySize(buffer), InpPollMs);
    if(received > 0)
    {
      string payload = CharArrayToString(buffer, 0, received);
      PrintFormat("[diag] msg[%d]: %s", count, payload);
      count++;
      if(InpMaxMessages > 0 && count >= InpMaxMessages)
        break;
    }
    else if(received < 0)
    {
      PrintFormat("[diag] Read failed (res=%d), break.", received);
      break;
    }

    if((TimeCurrent() - start) >= InpDurationSec)
      break;
  }

  SierraPipeClose();
  PrintFormat("[diag] Finished: %d messages, duration ~%d sec", count, (int)(TimeCurrent() - start));
  return(INIT_SUCCEEDED);
}
