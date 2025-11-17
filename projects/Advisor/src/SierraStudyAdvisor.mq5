#property copyright   "SierraStudy"
#property version     "1.0"
#property strict

/**
 * Minimal SierraStudy advisor for MetaTrader 5. The advisor delegates all
 * named pipe interactions to `SierraStudyAdvisorBridge.dll` via #import and
 * exposes only a couple of inputs to configure the connection.
 */

#import "SierraStudyAdvisorBridge.dll"
int  SierraPipeConnect(const char* pipe_name);
void SierraPipeClose();
int  SierraPipeWrite(const uchar& data[], int size);
int  SierraPipeRead(uchar& buffer[], int size, int timeout_ms);
#import

input string InpPipeName       = "\\\\.\\pipe\\SierraStudyAdvisor"; // Named pipe.
input int    InpPollIntervalMs = 200;                               // Poll interval.
input bool   InpAutoConnect    = true;                              // Connect on init.

bool g_connected = false;

int OnInit()
{
   if(InpAutoConnect)
      g_connected = (SierraPipeConnect(InpPipeName) == 1);

   PrintFormat("SierraStudy Advisor initialized (pipe=%s, connected=%s)",
               InpPipeName, g_connected ? "true" : "false");
   return(INIT_SUCCEEDED);
}

void OnTick()
{
   if(!g_connected)
      return;

   uchar heartbeat[];
   StringToCharArray("ping", heartbeat);
   SierraPipeWrite(heartbeat, ArraySize(heartbeat));

   uchar buffer[256];
   const int received = SierraPipeRead(buffer, ArraySize(buffer), InpPollIntervalMs);
   if(received > 0)
      PrintFormat("Received %d bytes from pipe.", received);
}

void OnDeinit(const int reason)
{
   if(g_connected)
      SierraPipeClose();
   g_connected = false;
   Print("SierraStudy Advisor stopped.");
}

