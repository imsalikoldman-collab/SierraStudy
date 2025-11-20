#property copyright   "SierraStudy"
#property version     "1.1"
#property strict

#include <Trade\Trade.mqh>

/**
 * @brief Советник, который зеркалирует сигналы OPEN/CLOSE из Sierra Chart в MT5.
 * @note Объём рассчитывается как процент от депозита по размеру стопа из OPEN_SIGNAL.
 */

#import "SierraStudyAdvisorBridgeMT5.dll"
int  SierraPipeConnect(string pipe_name);
void SierraPipeClose();
int  SierraPipeWrite(const uchar& data[], int size);
int  SierraPipeRead(uchar& buffer[], int size, int timeout_ms);
#import

input string InpPipeName            = "\\\\.\\pipe\\SierraStudyAdvisor"; // Имя pipe.
input int    InpPollIntervalMs      = 200;                               // Таймаут чтения.
input bool   InpAutoConnect         = true;                              // Подключаться автоматически.
input double InpRiskPercent         = 2.0;                               // Риск на сделку (% депозита).
input double InpProtectiveSLPercent = 7.0;                               // Защитный стоп по equity (% депо).
input double InpProtectiveTPPercent = 21.0;                              // Защитный тейк по equity (% депо).
input double InpSlippagePoints      = 5;                                 // Допустимое отклонение (пункты).
input long   InpMagicNumber         = 86001;                             // Magic для идентификации позиций.
input string InpFallbackSymbol      = "";                                // Символ по умолчанию (если пусто, берём _Symbol).
input double InpFallbackStopPoints  = 50;                                // Резервный стоп (пункты MT5), если нет стопа.
input ENUM_BASE_CORNER InpHudCorner = CORNER_RIGHT_LOWER;                // Угол для справочной таблицы.

bool   g_connected = false;
uchar  g_buffer[4096];
CTrade g_trade;

string g_activeId     = "";
string g_activeSymbol = "";
double g_entryEquity  = 0.0;
string g_lastMessage  = "";
datetime g_lastMsgTime = 0;

//--- структуры
struct SierraSignal
{
   string type;
   string id;
   string symbol;
   string direction;
   string note;
   double entry_price;
   double stop_points;
   bool   has_stop_points;
   double close_price;
};

//--- утилиты JSON
int   SkipSpaces(const string text, int index);
bool  ExtractStringField(const string text, const string key, string &value);
bool  ExtractNumberField(const string text, const string key, double &value);
bool  ExtractObjectRange(const string text, const string key, string &objectText);

//--- логика
bool   ParseSignal(const string payload, SierraSignal &signal);
void   ProcessSignal(const SierraSignal &signal);
bool   EnsureSymbol(string &symbol);
double CalculateVolumeByRisk(const string symbol, double stopDistancePoints);
double NormalizeVolume(double volume, double step, double minLot, double maxLot);
ENUM_POSITION_TYPE DirectionToPositionType(const string direction);
bool   PositionInfo(const string symbol, ENUM_POSITION_TYPE &type, double &volume);
bool   EnsurePosition(const string symbol, ENUM_POSITION_TYPE targetType, double entryPrice,
                      double stopDistancePoints, const string comment);
bool   UpdateStops(const string symbol, double stopPrice);
bool   ClosePosition(const string symbol, bool clearState);
void   UpdateHud(const string status);
void   CheckProtectiveEquityStops();
string Trim(const string value);
string ToLowerCase(string value);
bool DoubleEquals(const double a, const double b, const double eps = 1e-6);

int OnInit()
{
  g_trade.SetExpertMagicNumber((int)InpMagicNumber);
  g_trade.SetDeviationInPoints((ulong)MathMax(0.0, InpSlippagePoints));

  if(InpAutoConnect)
  {
     const int res = SierraPipeConnect(InpPipeName);
     g_connected = (res == 1);
     if(!g_connected)
        PrintFormat("SierraStudy Advisor: pipe connect failed (res=%d)", res);
  }

  PrintFormat("SierraStudy Advisor initialized (pipe=%s, connected=%s)",
              InpPipeName, g_connected ? "true" : "false");
  UpdateHud("init");
  return(INIT_SUCCEEDED);
}

void OnTick()
{
  if(!g_connected && InpAutoConnect)
  {
      const int res = SierraPipeConnect(InpPipeName);
      g_connected = (res == 1);
      if(!g_connected)
      {
         UpdateHud("no-conn");
         return;
      }
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
      g_lastMsgTime = TimeCurrent();
      g_lastMessage = signal.type;
   }

   CheckProtectiveEquityStops();
   UpdateHud("ok");
}

void OnDeinit(const int reason)
{
   if(g_connected)
      SierraPipeClose();
   g_connected = false;
   ObjectDelete(0, "SierraStudyHud");
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

   const string type = ToLowerCase(signal.type);
   g_lastMessage     = signal.type;
   if(type == "open_signal")
   {
      if(signal.entry_price <= 0.0)
      {
         Print("Entry price missing, skip OPEN_SIGNAL.");
         return;
      }
      const ENUM_POSITION_TYPE targetType = DirectionToPositionType(signal.direction);
      if(targetType != POSITION_TYPE_BUY && targetType != POSITION_TYPE_SELL)
      {
         PrintFormat("Unknown direction '%s' in payload.", signal.direction);
         return;
      }

      const double stopDist = (signal.has_stop_points ? signal.stop_points
                                                      : InpFallbackStopPoints * SymbolInfoDouble(symbol, SYMBOL_POINT));
      if(stopDist <= 0.0)
      {
         Print("Stop distance is zero, skip entry.");
         return;
      }

      // Если уже есть позиция с другим направлением или id — закроем её.
      ENUM_POSITION_TYPE currentType;
      double currentVolume = 0.0;
      if(PositionInfo(symbol, currentType, currentVolume))
      {
         if(currentType != targetType || (g_activeId != "" && g_activeId != signal.id))
            ClosePosition(symbol, true);
      }

      if(EnsurePosition(symbol, targetType, signal.entry_price, stopDist, signal.note))
      {
         g_activeId      = signal.id;
         g_activeSymbol  = symbol;
         g_entryEquity   = AccountInfoDouble(ACCOUNT_EQUITY);
      }
   }
   else if(type == "stop_level")
   {
      if(!PositionSelect(symbol))
         return;
      // Для ENTRY_FIRST — обновление стопа.
      const ENUM_POSITION_TYPE targetType = DirectionToPositionType(signal.direction);
      if(targetType == POSITION_TYPE_BUY || targetType == POSITION_TYPE_SELL)
      {
         const double stopDist = signal.has_stop_points ? signal.stop_points : 0.0;
         if(stopDist > 0.0)
         {
            const double stopPrice = (targetType == POSITION_TYPE_BUY) ? (signal.entry_price - stopDist)
                                                                       : (signal.entry_price + stopDist);
            UpdateStops(symbol, stopPrice);
         }
      }
   }
   else if(type == "close_signal")
   {
      if(g_activeId == "" || g_activeId == signal.id || signal.id == "")
         ClosePosition(symbol, true);
   }
   else
   {
      PrintFormat("Unsupported type '%s'.", signal.type);
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
                    double stopDistancePoints, const string comment)
{
   ENUM_POSITION_TYPE currentType;
   double currentVolume = 0.0;
   if(PositionInfo(symbol, currentType, currentVolume))
   {
      if(currentType != targetType)
      {
         ClosePosition(symbol, true);
      }
      else
      {
         const double stopPrice = (targetType == POSITION_TYPE_BUY) ? (entryPrice - stopDistancePoints)
                                                                    : (entryPrice + stopDistancePoints);
         UpdateStops(symbol, stopPrice);
         return true;
      }
   }

   const double volume = CalculateVolumeByRisk(symbol, stopDistancePoints);
   if(volume <= 0.0)
   {
      Print("Volume calculation returned zero, skip entry.");
      return false;
   }

   const ENUM_ORDER_TYPE orderType = (targetType == POSITION_TYPE_BUY ? ORDER_TYPE_BUY : ORDER_TYPE_SELL);
   const double stopPrice = (targetType == POSITION_TYPE_BUY) ? (entryPrice - stopDistancePoints)
                                                              : (entryPrice + stopDistancePoints);
   if(!g_trade.PositionOpen(symbol, orderType, volume, 0.0, stopPrice, 0.0, comment))
   {
      PrintFormat("PositionOpen failed (%s): %d", symbol, _LastError);
      return false;
   }
   return true;
}

bool UpdateStops(const string symbol, double stopPrice)
{
   if(!PositionSelect(symbol))
      return false;

   double currentSL = PositionGetDouble(POSITION_SL);

   if(stopPrice <= 0.0 || DoubleEquals(currentSL, stopPrice))
      return true;

   if(!g_trade.PositionModify(symbol, stopPrice, PositionGetDouble(POSITION_TP)))
   {
      PrintFormat("PositionModify failed (%s): %d", symbol, _LastError);
      return false;
   }
   return true;
}

bool ClosePosition(const string symbol, bool clearState)
{
   if(!PositionSelect(symbol))
      return true;

   if(!g_trade.PositionClose(symbol))
   {
      PrintFormat("PositionClose failed (%s): %d", symbol, _LastError);
      return false;
   }
   if(clearState)
   {
      g_activeId     = "";
      g_activeSymbol = "";
      g_entryEquity  = 0.0;
   }
   return true;
}

void CheckProtectiveEquityStops()
{
   if(g_activeId == "" || g_entryEquity <= 0.0 || g_activeSymbol == "")
      return;
   if(!PositionSelect(g_activeSymbol))
      return;

   const double equity      = AccountInfoDouble(ACCOUNT_EQUITY);
   const double lossTrigger = MathMax(0.0, InpProtectiveSLPercent) / 100.0;
   const double takeTrigger = MathMax(0.0, InpProtectiveTPPercent) / 100.0;

   const double drawdown = (equity - g_entryEquity) / g_entryEquity;
   if(lossTrigger > 0.0 && drawdown <= -lossTrigger)
   {
      PrintFormat("Protective equity stop triggered (%.2f%%).", lossTrigger * 100.0);
      ClosePosition(g_activeSymbol, true);
   }
   if(takeTrigger > 0.0 && drawdown >= takeTrigger)
   {
      PrintFormat("Protective equity take triggered (%.2f%%).", takeTrigger * 100.0);
      ClosePosition(g_activeSymbol, true);
   }
}

void UpdateHud(const string status)
{
   string lines = "";
   lines += StringFormat("SierraStudy MT5 (%s)\n", status);
   lines += StringFormat("Pipe: %s | Conn: %s\n", InpPipeName, g_connected ? "OK" : "OFF");
   lines += StringFormat("Last: %s at %s\n",
                         g_lastMessage == "" ? "-" : g_lastMessage,
                         (g_lastMsgTime == 0 ? "-" : TimeToString(g_lastMsgTime, TIME_MINUTES | TIME_SECONDS)));
   lines += StringFormat("ID: %s\n", g_activeId == "" ? "-" : g_activeId);

   string posLine = "Pos: none";
   if(g_activeSymbol != "" && PositionSelect(g_activeSymbol))
   {
      const ENUM_POSITION_TYPE pt = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
      const double vol = PositionGetDouble(POSITION_VOLUME);
      const double sl  = PositionGetDouble(POSITION_SL);
      posLine = StringFormat("Pos: %s %.2f sl=%.2f", (pt == POSITION_TYPE_BUY ? "BUY" : "SELL"), vol, sl);
   }
   lines += posLine + "\n";

   lines += StringFormat("Equity guard: SL=%.2f%% TP=%.2f%%", InpProtectiveSLPercent, InpProtectiveTPPercent);

   const string objName = "SierraStudyHud";
   if(ObjectFind(0, objName) == -1)
   {
      ObjectCreate(0, objName, OBJ_LABEL, 0, 0, 0);
      ObjectSetInteger(0, objName, OBJPROP_FONTSIZE, 8);
      ObjectSetString(0, objName, OBJPROP_FONT, "Consolas");
      ObjectSetInteger(0, objName, OBJPROP_COLOR, clrDimGray);
      ObjectSetInteger(0, objName, OBJPROP_XDISTANCE, 10);
      ObjectSetInteger(0, objName, OBJPROP_YDISTANCE, 10);
   }
   ObjectSetInteger(0, objName, OBJPROP_CORNER, InpHudCorner);
   ObjectSetString(0, objName, OBJPROP_TEXT, lines);
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

double CalculateVolumeByRisk(const string symbol, double stopDistancePoints)
{
   double riskPercent = InpRiskPercent;
   riskPercent = MathMin(5.0, MathMax(0.5, MathRound(riskPercent * 2.0) / 2.0)); // шаг 0.5
   const double riskFraction = MathMax(0.0, riskPercent) / 100.0;
   if(riskFraction <= 0.0)
      return 0.0;

   double stopDistance = MathAbs(stopDistancePoints);
   if(stopDistance <= 0.0)
      return 0.0;

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
   signal.has_stop_points = false;
   signal.stop_points     = 0.0;
   signal.entry_price     = 0.0;
   signal.close_price     = 0.0;
   signal.type            = "";
   signal.id              = "";

   ExtractStringField(payload, "type", signal.type);
   ExtractStringField(payload, "id", signal.id);
   ExtractStringField(payload, "symbol", signal.symbol);
   ExtractStringField(payload, "direction", signal.direction);
   ExtractStringField(payload, "note", signal.note);
   ExtractNumberField(payload, "entry_price", signal.entry_price);
   ExtractNumberField(payload, "close_price", signal.close_price);

   double stopPoints = 0.0;
   if(ExtractNumberField(payload, "stop_loss_points", stopPoints))
   {
      signal.has_stop_points = true;
      signal.stop_points     = stopPoints;
   }

   return (signal.type != "" && signal.direction != "");
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
