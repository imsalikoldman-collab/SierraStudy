#property copyright   "SierraStudy"
#property version     "1.0"
#property strict

#include <Trade\Trade.mqh>

/**
 * @brief Советник, который зеркалирует позиции из Sierra Chart в MT5.
 * @note Объём рассчитывается как процент от баланса согласно InpRiskPercent.
 */

#import "SierraStudyAdvisorBridgeMT5.dll"
int  SierraPipeConnect(string pipe_name);
void SierraPipeClose();
int  SierraPipeWrite(const uchar& data[], int size);
int  SierraPipeRead(uchar& buffer[], int size, int timeout_ms);
#import

input string InpPipeName        = "\\\\.\\pipe\\SierraStudyAdvisor"; // Имя pipe.
input int    InpPollIntervalMs  = 200;                               // Таймаут чтения.
input bool   InpAutoConnect     = true;                              // Подключаться автоматически.
input double InpRiskPercent     = 1.0;                               // Риск на сделку (% депозита).
input double InpSlippagePoints  = 5;                                 // Допустимое отклонение (пункты).
input long   InpMagicNumber     = 86001;                             // Magic для идентификации позиций.
input string InpFallbackSymbol  = "";                                // Символ по умолчанию (если пусто, берём _Symbol).
input double InpFallbackStopPoints = 50;                              // Резервный стоп (пункты MT5), если в сообщении stop отсутствует.

bool   g_connected = false;
uchar  g_buffer[4096];
CTrade g_trade;

//--- структуры
struct SierraSignal
{
   string symbol;
   string direction;
   string status;
   string note;
   double entry;
   double stop;
   double take;
   bool   has_take;
   bool   has_stop;
   double offset;
};

//--- утилиты JSON
int   SkipSpaces(const string text, int index);
bool  ExtractStringField(const string text, const string key, string &value);
bool  ExtractNumberField(const string text, const string key, double &value);
bool  ExtractObjectRange(const string text, const string key, string &objectText);

//--- логика
bool ParseSignal(const string payload, SierraSignal &signal);
void ProcessSignal(const SierraSignal &signal);
bool EnsureSymbol(string &symbol);
double CalculateVolume(const string symbol, double entryPrice, double stopPrice);
double NormalizeVolume(double volume, double step, double minLot, double maxLot);
ENUM_POSITION_TYPE DirectionToPositionType(const string direction);
bool PositionInfo(const string symbol, ENUM_POSITION_TYPE &type, double &volume);
bool EnsurePosition(const string symbol, ENUM_POSITION_TYPE targetType, double entryPrice, double stopPrice,
                    double takePrice, const string comment);
bool UpdateStops(const string symbol, double stopPrice, double takePrice);
bool ClosePosition(const string symbol);
string Trim(const string value);
string ToLowerCase(string value);
bool DoubleEquals(const double a, const double b, const double eps = 1e-6);

int OnInit()
{
   g_trade.SetExpertMagicNumber((int)InpMagicNumber);
   g_trade.SetDeviationInPoints((ulong)MathMax(0.0, InpSlippagePoints));

   if(InpAutoConnect)
      g_connected = (SierraPipeConnect(InpPipeName) == 1);

   PrintFormat("SierraStudy Advisor initialized (pipe=%s, connected=%s)",
               InpPipeName, g_connected ? "true" : "false");
   return(INIT_SUCCEEDED);
}

void OnTick()
{
   if(!g_connected && InpAutoConnect)
   {
      g_connected = (SierraPipeConnect(InpPipeName) == 1);
      if(!g_connected)
         return;
   }

   // читаем несколько сообщений за тик
   for(int i = 0; i < 4 && g_connected; ++i)
   {
      const int received = SierraPipeRead(g_buffer, ArraySize(g_buffer), InpPollIntervalMs);
      if(received <= 0)
      {
         if(received < 0)
         {
            Print("Pipe read failed, reconnect required.");
            g_connected = false;
         }
         break;
      }

      string payload = CharArrayToString(g_buffer, 0, received);
      payload = Trim(payload);
      if(payload == "")
         continue;

      SierraSignal signal;
      if(!ParseSignal(payload, signal))
      {
         PrintFormat("Failed to parse payload: %s", payload);
         continue;
      }
      ProcessSignal(signal);
   }
}

void OnDeinit(const int reason)
{
   if(g_connected)
      SierraPipeClose();
   g_connected = false;
   Print("SierraStudy Advisor stopped.");
}

//--- обработка сигналов
void ProcessSignal(const SierraSignal &signal)
{
   string symbol = signal.symbol;
   if(symbol == "")
      symbol = InpFallbackSymbol;
   if(symbol == "")
      symbol = _Symbol;

   if(!EnsureSymbol(symbol))
      return;

   double entryPrice = signal.entry + signal.offset;
   double stopPrice  = signal.has_stop ? signal.stop + signal.offset : 0.0;
   double takePrice  = signal.has_take ? signal.take + signal.offset : 0.0;

   const string status = ToLowerCase(signal.status);

   if(status == "open")
   {
      const ENUM_POSITION_TYPE targetType = DirectionToPositionType(signal.direction);
      if(targetType == POSITION_TYPE_BUY || targetType == POSITION_TYPE_SELL)
      {
         if(MathAbs(entryPrice - stopPrice) <= 0.0)
         {
            const double point = SymbolInfoDouble(symbol, SYMBOL_POINT);
            const double fallback = MathMax(point, InpFallbackStopPoints * point);
            stopPrice = (targetType == POSITION_TYPE_BUY) ? (entryPrice - fallback)
                                                          : (entryPrice + fallback);
         }
         EnsurePosition(symbol, targetType, entryPrice, stopPrice, takePrice, signal.note);
      }
      else
         PrintFormat("Unknown direction '%s' in payload.", signal.direction);
   }
else if(status == "modify")
{
   if(MathAbs(entryPrice - stopPrice) <= 0.0)
   {
      const double point = SymbolInfoDouble(symbol, SYMBOL_POINT);
      const ENUM_POSITION_TYPE targetType = DirectionToPositionType(signal.direction);
      const double fallback = MathMax(point, InpFallbackStopPoints * point);
      if(targetType == POSITION_TYPE_SELL)
         stopPrice = entryPrice + fallback;
      else
         stopPrice = entryPrice - fallback;
   }

   UpdateStops(symbol, stopPrice, takePrice);
}
   else if(status == "close")
   {
      ClosePosition(symbol);
   }
   else
   {
      PrintFormat("Unsupported status '%s'.", signal.status);
   }
}

bool EnsureSymbol(string &symbol)
{
   if(SymbolInfoInteger(symbol, SYMBOL_SELECT))
      return true;
   if(SymbolSelect(symbol, true))
      return true;

   PrintFormat("Symbol %s not available, fallback to %s", symbol, _Symbol);
   symbol = _Symbol;
   return (SymbolInfoInteger(symbol, SYMBOL_SELECT) != 0);
}

bool EnsurePosition(const string symbol, ENUM_POSITION_TYPE targetType, double entryPrice,
                    double stopPrice, double takePrice, const string comment)
{
   ENUM_POSITION_TYPE currentType;
   double currentVolume = 0.0;
   if(PositionInfo(symbol, currentType, currentVolume))
   {
      if(currentType != targetType)
      {
         ClosePosition(symbol);
      }
      else
      {
         UpdateStops(symbol, stopPrice, takePrice);
         return true;
      }
   }

   const double volume = CalculateVolume(symbol, entryPrice, stopPrice);
   if(volume <= 0.0)
   {
      Print("Volume calculation returned zero, skip entry.");
      return false;
   }

   const ENUM_ORDER_TYPE orderType = (targetType == POSITION_TYPE_BUY ? ORDER_TYPE_BUY : ORDER_TYPE_SELL);
   if(!g_trade.PositionOpen(symbol, orderType, volume, 0.0, stopPrice, takePrice, comment))
   {
      PrintFormat("PositionOpen failed (%s): %d", symbol, _LastError);
      return false;
   }
   return true;
}

bool UpdateStops(const string symbol, double stopPrice, double takePrice)
{
   if(!PositionSelect(symbol))
      return false;

   double currentSL = PositionGetDouble(POSITION_SL);
   double currentTP = PositionGetDouble(POSITION_TP);

   bool needModify = false;
   if(stopPrice > 0.0 && !DoubleEquals(currentSL, stopPrice))
      needModify = true;
   if(takePrice > 0.0 && !DoubleEquals(currentTP, takePrice))
      needModify = true;

   if(!needModify)
      return true;

   if(!g_trade.PositionModify(symbol, stopPrice, takePrice))
   {
      PrintFormat("PositionModify failed (%s): %d", symbol, _LastError);
      return false;
   }
   return true;
}

bool ClosePosition(const string symbol)
{
   if(!PositionSelect(symbol))
      return true;

   if(!g_trade.PositionClose(symbol))
   {
      PrintFormat("PositionClose failed (%s): %d", symbol, _LastError);
      return false;
   }
   return true;
}

bool PositionInfo(const string symbol, ENUM_POSITION_TYPE &type, double &volume)
{
   if(!PositionSelect(symbol))
      return false;

   if((long)PositionGetInteger(POSITION_MAGIC) != InpMagicNumber)
      return false;

   type   = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
   volume = PositionGetDouble(POSITION_VOLUME);
   return (volume > 0.0);
}

double CalculateVolume(const string symbol, double entryPrice, double stopPrice)
{
   const double riskFraction = MathMax(0.0, InpRiskPercent) / 100.0;
   if(riskFraction <= 0.0)
      return 0.0;

   double stopDistance = MathAbs(entryPrice - stopPrice);
   if(stopDistance <= 0.0)
   {
      const double point = SymbolInfoDouble(symbol, SYMBOL_POINT);
      stopDistance = MathMax(point, InpFallbackStopPoints * point);
   }

   if(stopDistance <= 0.0)
      return 0.0;

   const double tickSize  = SymbolInfoDouble(symbol, SYMBOL_TRADE_TICK_SIZE);
   const double tickValue = SymbolInfoDouble(symbol, SYMBOL_TRADE_TICK_VALUE);
   if(tickSize <= 0.0 || tickValue <= 0.0)
      return 0.0;

   const double lossPerLot = (stopDistance / tickSize) * tickValue;
   if(lossPerLot <= 0.0)
      return 0.0;

   const double balance   = AccountInfoDouble(ACCOUNT_BALANCE);
   const double riskMoney = balance * riskFraction;

   double volume = riskMoney / lossPerLot;
   const double step   = SymbolInfoDouble(symbol, SYMBOL_VOLUME_STEP);
   const double minLot = SymbolInfoDouble(symbol, SYMBOL_VOLUME_MIN);
   const double maxLot = SymbolInfoDouble(symbol, SYMBOL_VOLUME_MAX);
   volume = NormalizeVolume(volume, step, minLot, maxLot);

   if(volume < minLot - 1e-8)
      return 0.0;

   return volume;
}

double NormalizeVolume(double volume, double step, double minLot, double maxLot)
{
   if(step <= 0.0)
      step = 0.01;
   if(minLot <= 0.0)
      minLot = step;
   if(maxLot <= 0.0)
      maxLot = minLot;

   volume = MathMax(minLot, MathMin(maxLot, volume));
   volume = MathFloor(volume / step) * step;

   const int digits = (int)MathMax(0.0, MathRound(-MathLog10(step)));
   return NormalizeDouble(volume, digits);
}

ENUM_POSITION_TYPE DirectionToPositionType(const string direction)
{
   string dir = ToLowerCase(direction);
   if(dir == "long" || dir == "buy")
      return POSITION_TYPE_BUY;
   if(dir == "short" || dir == "sell")
      return POSITION_TYPE_SELL;
   return (ENUM_POSITION_TYPE)-1;
}

//--- JSON helpers
bool ParseSignal(const string payload, SierraSignal &signal)
{
   signal.has_take = false;
   signal.has_stop = false;
   signal.offset   = 0.0;

   ExtractStringField(payload, "symbol", signal.symbol);
   ExtractStringField(payload, "direction", signal.direction);
   ExtractStringField(payload, "status", signal.status);
   ExtractStringField(payload, "note", signal.note);
   ExtractNumberField(payload, "offset", signal.offset);

   string entryObj;
   if(ExtractObjectRange(payload, "entry", entryObj) && ExtractNumberField(entryObj, "price", signal.entry))
   {
      // ok
   }
   else
   {
      return false;
   }

   string stopObj;
   if(ExtractObjectRange(payload, "stop", stopObj) && ExtractNumberField(stopObj, "price", signal.stop))
   {
      signal.has_stop = true;
   }

   string takeObj;
   if(ExtractObjectRange(payload, "take", takeObj) && ExtractNumberField(takeObj, "price", signal.take))
      signal.has_take = true;

   return (signal.direction != "" && signal.status != "");
}

int SkipSpaces(const string text, int index)
{
   const int len = StringLen(text);
   while(index < len)
   {
      const ushort ch = StringGetCharacter(text, index);
      if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
         index++;
      else
         break;
   }
   return index;
}

bool ExtractStringField(const string text, const string key, string &value)
{
   const string pattern = "\"" + key + "\"";
   int pos = StringFind(text, pattern);
   if(pos == -1)
      return false;

   int colon = StringFind(text, ":", pos + StringLen(pattern));
   if(colon == -1)
      return false;

   int idx = SkipSpaces(text, colon + 1);
   const int len = StringLen(text);
   if(idx >= len || StringGetCharacter(text, idx) != '\"')
      return false;

   idx++;
   string result = "";
   bool   escape = false;
   for(; idx < len; ++idx)
   {
      const ushort ch = StringGetCharacter(text, idx);
      if(!escape && ch == '\\')
      {
         escape = true;
         continue;
      }
      if(!escape && ch == '\"')
         break;
      escape = false;
      result += StringSubstr(text, idx, 1);
   }
   value = result;
   return true;
}

bool ExtractNumberField(const string text, const string key, double &value)
{
   const string pattern = "\"" + key + "\"";
   int pos = StringFind(text, pattern);
   if(pos == -1)
      return false;

   int colon = StringFind(text, ":", pos + StringLen(pattern));
   if(colon == -1)
      return false;

   int idx = SkipSpaces(text, colon + 1);
   const int len = StringLen(text);
   string number = "";
   for(; idx < len; ++idx)
   {
      const ushort ch = StringGetCharacter(text, idx);
      if((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.')
         number += StringSubstr(text, idx, 1);
      else if(number != "")
         break;
      else if(ch == ' ')
         continue;
      else
         break;
   }

   if(number == "")
      return false;

   value = StringToDouble(number);
   return true;
}

bool ExtractObjectRange(const string text, const string key, string &objectText)
{
   const string pattern = "\"" + key + "\"";
   int pos = StringFind(text, pattern);
   if(pos == -1)
      return false;

   int brace = StringFind(text, "{", pos + StringLen(pattern));
   if(brace == -1)
      return false;

   int depth = 0;
   const int len = StringLen(text);
   for(int idx = brace; idx < len; ++idx)
   {
      const ushort ch = StringGetCharacter(text, idx);
      if(ch == '{')
         depth++;
      else if(ch == '}')
      {
         depth--;
         if(depth == 0)
         {
            objectText = StringSubstr(text, brace, idx - brace + 1);
            return true;
         }
      }
   }
   return false;
}

string Trim(const string value)
{
   string tmp = value;
   StringTrimLeft(tmp);
   StringTrimRight(tmp);
   return tmp;
}

string ToLowerCase(string value)
{
   StringToLower(value);
   return value;
}

bool DoubleEquals(const double a, const double b, const double eps)
{
   return MathAbs(a - b) <= eps;
}
